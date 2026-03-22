//  symbol handling

#include <stdio.h>
#include <string.h>
#include "f18.h"
#include "f18_sym.h"

const f18_symbol_t no_symbol[1];
const f18_symbol_table_t no_symbols = SYMTAB_INITALIZER(&no_symbol);

extern uint18_t normalize_addr(uint18_t);

// Look for symbols from that latest to the first
symindex_t sym_find_by_namelen(const char* name, int len,
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
    return NOSYM;  // not found
}

symindex_t sym_find_by_name(const char* name, const f18_symbol_table_t* symtab)
{
    return sym_find_by_namelen(name, strlen(name), symtab);
}

symindex_t sym_find_by_value(uint18_t addr, const f18_symbol_table_t* symtab)
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

symindex_t sym_find_by_addr(uint18_t addr, const f18_symbol_table_t* symtab)
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
    return NOSYM;
}

symindex_t sym_insert(char* word, int len, f18_symbol_table_t* symtab)
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
    return NOSYM;
}

static char* align_dp(f18_symbol_table_t* symtab)
{
    return (char*)(((uintptr_t)(symtab->dp+1))&~(sizeof(uintptr_t)*8-1));
}

int sym_insert_patch(symindex_t si, int slot, uint18_t addr, f18_symbol_table_t* symtab)
{
    char* dp = align_dp(symtab);
    f18_symbol_patch_t* patch = (f18_symbol_patch_t*) dp;
    
    patch->addr = addr;
    patch->slot = slot;
    patch->next = symtab->symbol[si].value;
    symtab->dp -= sizeof(f18_symbol_patch_t);
    symtab->symbol[si].value = (dp - (char*) symtab->heap);
    return 0;
}

void sym_resolve(symindex_t si, uint18_t addr, uint18_t* ram,
		 f18_symbol_table_t* symtab)
{
    f18_symbol_patch_t* patch;
    uint18_t patch_addr;
    uint18_t word;
    uint18_t mask;
    uint9_t p;

    p = symtab->symbol[si].value;
    if (p == 0)  // no patches to apply
	return;
    if (SYMTYP(&symtab->symbol[si]) != 'U')   // double check symbol type
	return;

    while(p != 0) {
	patch = (f18_symbol_patch_t*) (symtab->heap + p);
	patch_addr = patch->addr & MASK6;  // RAM is 64 words
	word = ram[patch_addr];

	// Select mask based on slot (how many bits available for destination)
	switch(patch->slot) {
	case 0: mask = MASK10; break;  // slot 0: 10 bits
	case 1: mask = MASK8;  break;  // slot 1: 8 bits
	case 2: mask = MASK3;  break;  // slot 2: 3 bits
	default: mask = 0; break;      // slot 3: no address bits
	}

	// Check if address fits in available bits
	if ((addr & ~mask) != (patch_addr & ~mask)) {
	    fprintf(stderr, "warning: forward ref '%s' at %03x slot %d: "
		    "dest %03x doesn't fit in %d bits\n",
		    symtab->symbol[si].name, patch_addr, patch->slot,
		    addr, (patch->slot == 0) ? 10 : (patch->slot == 1) ? 8 : 3);
	}

	// Patch the instruction: clear old dest bits, set new ones
	// Destination bits are stored raw (not XOR'd with IMASK)
	word = (word & ~mask) | (addr & mask);
	ram[patch_addr] = word;

	printf("resolve_symbol: %s -> %03x (patched %03x slot %d)\n",
	       symtab->symbol[si].name, addr, patch_addr, patch->slot);

	p = patch->next;
    }

    // Mark symbol as resolved and store the address
    SYMTYP(&symtab->symbol[si]) = 'R';
    symtab->symbol[si].value = addr;
}

symindex_t sym_add(char* name, int len, uint18_t addr,
		   uint18_t* ram, f18_symbol_table_t* symtab)
{
    symindex_t si;

    if ((si = sym_find_by_namelen(name, len, symtab)) != NOSYM) {
	if (SYMTYP(&symtab->symbol[si]) == 'U') // unresolved - patch it!
	    sym_resolve(si, addr, ram, symtab);
	// else: symbol already resolved, ignore redefinition
    }
    else {
	// New symbol - add it as resolved
	if ((si = sym_insert(name, len, symtab)) != NOSYM) {
	    SYMTYP(&symtab->symbol[si]) = 'R';  // resolved RAM address
	    symtab->symbol[si].value = addr;
	}
    }
    return si;
}

f18_symbol_table_t* sym_copy_table(f18_symbol_table_t* src)
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

