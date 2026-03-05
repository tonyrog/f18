//  symbol handling

#include <stdio.h>
#include <string.h>
#include "f18_sym.h"

extern uint18_t normalize_addr(uint18_t);

int find_symbol_by_namelen(const char* name, int len,
			   const f18_symbol_table_t* symtab)
{
    if (symtab != NULL) {
	f18_symbol_t* sp = symtab->next - 1;
	int n = symtab->next - symtab->symbol;

	while(n) {
	    if ((SYMLEN(sp) == len) &&
		(memcmp(sp->name, name, len) == 0))
		return n-1;
	    sp--;
	    n--;
	}
    }
    return -1;  // not found
}

int find_symbol_by_name(const char* name, const f18_symbol_table_t* symtab)
{
    return find_symbol_by_namelen(name, strlen(name), symtab);
}

int find_symbol_by_value(uint18_t addr, const f18_symbol_table_t* symtab)
{
    if (symtab != NULL) {
	f18_symbol_t* sp = symtab->next - 1;
	int n = symtab->next - symtab->symbol;

	while(n) {
	    if (sp->value == addr)
		return n-1;
	    sp--;
	    n--;
	}
    }
    return -1;
}

int find_symbol_by_addr(uint18_t addr, const f18_symbol_table_t* symtab)
{
    if (symtab != NULL) {
	f18_symbol_t* sp = symtab->next - 1;
	int n = symtab->next - symtab->symbol;
	uint18_t a;

	a = normalize_addr(addr);
	
	while(n) {
	    if (a == normalize_addr(sp->value))
		return n-1;
	    sp--;
	    n--;
	}
    }
    return -1;
}

int insert_symbol(char* word, int len, f18_symbol_table_t* symtab)
{
    if (symtab != NULL) {
	f18_symbol_t* sp = symtab->next;
	char* dp = symtab->dp - (len+3);  // [typ][len]<str>:len[0]

	dp[0] = 'U';       // mark as unresolved
	dp[1] = len;       // string length
	sp->name = dp+2;   // set the name
	memcpy(sp->name, word, len);
	sp->name[len] = '\0'; // and make C-compatible
	sp->value = 0;

	symtab->dp = dp;
	symtab->next++;
	return sp - symtab->symbol;
    }
    return -1;
}

static char* align_dp(f18_symbol_table_t* symtab)
{
    return (char*)(((uintptr_t)(symtab->dp+1))&~(sizeof(uintptr_t)*8-1));
}

int insert_patch(int i, int slot, uint18_t addr,  f18_symbol_table_t* symtab)
{
    char* dp = align_dp(symtab);
    f18_symbol_patch_t* patch = (f18_symbol_patch_t*) dp;
    
    patch->addr = addr;
    patch->slot = slot;
    patch->next = symtab->symbol[i].value;
    symtab->dp -= sizeof(f18_symbol_patch_t);
    symtab->symbol[i].value = (dp - (char*) symtab->heap);
    return 0;
}

void resolve_symbol(int i, int slot, uint18_t addr,  f18_symbol_table_t* symtab)
{
    f18_symbol_patch_t* patch;
    uint9_t p;
    
    if ((p = symtab->symbol[i].value) == 0)  // can not be patched
	return;
    if (SYMTYP(&symtab->symbol[i]) != 'U')   // double check symbol type
	return;
    while(p != 0) {
	patch = (f18_symbol_patch_t*) (symtab->heap + p);
	printf("resolve_symbol: %s addr=%d  to %03x:%d\n",
	       symtab->symbol[i].name, addr, patch->addr, patch->slot);
	// FIXME: patch the RAM
	p = patch->next;
    }
}

int add_symbol(char* name, int len, int slot, uint18_t addr, f18_symbol_table_t* symtab)
{
    int i;

    if ((i = find_symbol_by_name(name, symtab)) >= 0) {
	if (SYMTYP(&symtab->symbol[i]) == 'U') // unresolved
	    resolve_symbol(i, slot, addr, symtab);
    }
    else {
	if ((i = insert_symbol(name, len, symtab)) >= 0) {
	    SYMTYP(&symtab->symbol[i]) = 'R';  // resolved RAM address
	    symtab->symbol[i].value = addr;
	}
    }
    return i;
}

f18_symbol_table_t* copy_symbols(f18_symbol_table_t* src)
{
    int n = src->next - src->symbol;  // number of symbols;
    int i;
    size_t size = sizeof(f18_symbol_table_t) + n*sizeof(f18_symbol_t);
    f18_symbol_table_t *dst;

    for (i = 0; i < n; i++) {
	f18_symbol_t* sp = &src->symbol[i];
	size += (3 + SYMLEN(sp));
    }

    dst = (f18_symbol_table_t*) malloc(size);
    dst->heap = ((uint8_t*)dst) + sizeof(f18_symbol_table_t);
    dst->heap_size = size - sizeof(f18_symbol_table_t);
    dst->dp = (char*) dst->heap + dst->heap_size;
    dst->symbol = (f18_symbol_t*) dst->heap;
    dst->next   = dst->symbol + n;
    // copy symbol table
    memcpy(dst->symbol, src->symbol, n*sizeof(f18_symbol_t));
    // copy symbol names
    for (i = 0; i < n; i++) {
	f18_symbol_t* src_sp = &src->symbol[i];
	f18_symbol_t* dst_sp = &dst->symbol[i];
	size_t len = SYMLEN(src_sp);
	printf("copy symbol %s L:%d T:%c Value:%05x\n",
	       src_sp->name, SYMLEN(src_sp), SYMTYP(src_sp), src_sp->value);
	if (SYMTYP(src_sp) == 'U')
	    fprintf(stderr, "symbol %s is unresolved\n", src_sp->name);
	dst->dp -= (len + 3);
	memcpy(dst->dp, src_sp->name - 2, len+3);
	dst_sp->name = dst->dp + 2;
    }
    return dst;
}

