#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18_scan.h"

const char SN_IO[]   =  { 'R', 2, 'i', 'o', 0 };
const char SN_DATA[] =  { 'R', 4, 'd', 'a', 't', 'a', 0 };

const char SN__D__[] =  { 'R', 4, '-', 'd', '-', '-', 0 };
const char SN__D_U[] =  { 'R', 4, '-', 'd', '-', 'u', 0 };
const char SN__DLU[] =  { 'R', 4, '-', 'd', 'l', 'u', 0 };
const char SN__DL_[] =  { 'R', 4, '-', 'd', 'l', '-', 0 };
const char SN____U[] =  { 'R', 4, '-', '-', '-', 'u', 0 };
const char SN___LU[] =  { 'R', 4, '-', '-', 'l', 'u', 0 };
const char SN___L_[] =  { 'R', 4, '-', '-', 'l', '-', 0 };

const char SN_R___[] =  { 'R', 4, 'r', '_', '-', '-', 0 };
const char SN_RD__[] =  { 'R', 4, 'r', 'd', '-', '-', 0 };
const char SN_RD_U[] =  { 'R', 4, 'r', 'd', '-', 'u', 0 };
const char SN_RDLU[] =  { 'R', 4, 'r', 'd', 'l', 'u', 0 };
const char SN_RDL_[] =  { 'R', 4, 'r', 'd', 'l', '-', 0 };
const char SN_R__U[] =  { 'R', 4, 'r', '-', '-', 'u', 0 };
const char SN_R_LU[] =  { 'R', 4, 'r', '-', 'l', 'u', 0 };
const char SN_R_L_[] =  { 'R', 4, 'r', '-', 'l', '-', 0 };

// sort on address
const f18_symbol_t iosym[] = {
    { IOREG__D_U, SYMSTR(_D_U) },
    { IOREG__D__, SYMSTR(_D__) },
    { IOREG__DLU, SYMSTR(_DLU) },
    { IOREG__DL_, SYMSTR(_DL_) },
    { IOREG_DATA, SYMSTR(DATA) },
    { IOREG____U, SYMSTR(___U) },
    { IOREG_IO,   SYMSTR(IO)  },
    { IOREG___LU, SYMSTR(__LU) },
    { IOREG___L_, SYMSTR(__L_) },    
    { IOREG_RD_U, SYMSTR(RD_U) },    
    { IOREG_RD__, SYMSTR(RD__) },
    { IOREG_RDLU, SYMSTR(RDLU) },    
    { IOREG_RDL_, SYMSTR(RDL_) },
    { IOREG_R__U, SYMSTR(R__U) },
    { IOREG_R___, SYMSTR(R___) },
    { IOREG_R_LU, SYMSTR(R_LU) },
    { IOREG_R_L_, SYMSTR(R_L_) },
};

const f18_symbol_table_t iosym_tab  = SYMTAB_INITALIZER(iosym);


int find_symbol_by_name(char* name, f18_symbol_table_t* symtab)
{
    f18_symbol_t* sp = symtab->next - 1;
    int n = symtab->next - symtab->symbol;

    while(n) {
	if (strcmp(name, sp->name) == 0)
	    return n-1;
	sp--;
	n--;
    }
    return -1;  // not found
}

extern uint18_t normalize_addr(uint18_t);

int find_symbol_by_value(uint18_t addr, f18_symbol_table_t* symtab)
{
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
    return -1;
}


int lookup_symbol(char** pptr, f18_symbol_table_t* symtab)
{
    char* ptr = *pptr;
    f18_symbol_t* sp = symtab->next - 1;
    int n = symtab->next - symtab->symbol;

    while(n) {
	int len = SYMLEN(sp);
	if (strncmp(ptr, sp->name, len) == 0) {
	    if ((ptr[len] == '\0') || isblank(ptr[len])) {
		*pptr = ptr + len;
		return n-1;
	    }
	}
	sp--;
	n--;
    }
    return -1;  // not found
}

int insert_symbol(char* word, int len, f18_symbol_table_t* symtab)
{
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


int parse_mnemonic(char* word, int n)
{
    int i;
    for (i=0; i<32; i++) {
	int len = strlen(f18_ins_name[i]);
	if ((n == len) && (memcmp(word, f18_ins_name[i], n) == 0))
	    return i;
    }
    if ((n == 3) && (memcmp(word, "org", 3) == 0))
	return META_ORG;
    if ((n == 4) && (memcmp(word, "node", 4) == 0))
	return META_NODE;    
    return -1;
}

// fixme: have a encode := invert, that keep op but xor the dest!?
uint18_t encode_dest(int enc, uint18_t instr, uint18_t mask,
		     uint18_t addr, uint18_t dest)
{
    if (enc) {
	uint18_t d = ((instr^IMASK)&~mask)|(addr&~mask)|(dest&mask);
	// printf("inst=%02x, mask=%03x, addr=%03x, dest=%03x: d=%05x\n",
	// instr, mask, addr, dest, d);
	return d;
    }
    else
	return instr | (addr & ~mask) | (dest & mask);
}	    

//
// parse:
//   
//   ( '(' .* ')' )* <mnemonic>
//   ( '(' .* ')' )* <mnemonic>':'<dest>
//   ( '(' .* ')' )* <hex>
//   ( '(' .* ')' )* \<blank> .*
//

int parse_ins(char** pptr,uint18_t* insp,
	      int slot, uint18_t addr,
	      uint18_t* dstp,
	      f18_symbol_table_t* symtab)
{
    char* ptr = *pptr;
    char* word;
    uint18_t value = 0;
    int i;
    int len = 0;
    int ins;
    int want_arg = 0;
    int r = 0;

    
    while(r || isblank(*ptr) || (*ptr == '(')) {
	while(isblank(*ptr)) ptr++;
	// what abount '\' ? still skip to end-of-line?
	if (*ptr == '\\')
	    goto back;
	if (*ptr == '(') {
	    r++;
	    ptr++;
	    while(*ptr && (*ptr != ')'))
		ptr++;
	    if (*ptr == ')')
		r--;
	    if (*ptr) ptr++;
	}
    }
back:
    if ( (*ptr == '\\') && (isblank(*(ptr+1)) || (*(ptr+1)=='\0')) ) {
	while(*ptr != '\0') ptr++;  // skip rest
    }
    word = ptr;
    while (*ptr && !isblank(*ptr) && (*ptr != ':')) { ptr++; len++; }
    if ((len == 0) && (ptr[0] == ':')) {
	*insp = META_DEF;
	*dstp = 0;
	*pptr = ptr+1;
	return TOKEN_MNEMONIC2;
    }
    if (len == 0)
	return TOKEN_EMPTY;
    ins = parse_mnemonic(word, len);
    len = 0;
    switch(ins) {
    case -1:
	ptr = word;
	break;
    case INS_PJUMP:
    case INS_PCALL:
    case INS_NEXT:
    case INS_IF:
    case INS_MINUS_IF:
	want_arg = 1;
	if (*ptr == ':') { // force?
	    ptr++;
	    goto dest_arg;
	}
	break;
    case META_ORG:
	want_arg = 1;	
	goto number_arg;
	break;
    case META_NODE:
	want_arg = 1;
	goto number_arg;
	break;	
    default:
	goto done;
    }

dest_arg: // parse number or dest
    while (*ptr && isblank(*ptr)) ptr++;
    if ((i = lookup_symbol(&ptr,(f18_symbol_table_t*)&iosym_tab)) >= 0) {
	value = iosym_tab.symbol[i].value;
	// printf("dest: %s = %03x\n", iosym_tab.symbol[i].name, value);
	len = 1;
	goto done;
    }
    else if ((i = lookup_symbol(&ptr, symtab)) >= 0) {
	if (SYMTYP(&symtab->symbol[i]) == 'R') { // unresolved
	    value = symtab->symbol[i].value;
	    // printf("dest: %s = %03x\n", symtab->symbol[i].name, value);
	}	
	else if (SYMTYP(&symtab->symbol[i]) == 'U') { // unresolved
	    insert_patch(i, slot, addr, symtab);
	    value = 0;
	}
	len = 1;
	goto done;
    }
number_arg: 	// 0xabcd | 0b1010 | 0abcd (hex) | 123 (dec)
    len = 0;
    while (*ptr && isblank(*ptr)) ptr++;
    if ((ptr[0] == '0') && (ptr[1] == 'x')) {
	ptr += 2;
	goto hex;
    }
    if (ptr[0] =='0')
	goto hex;
// dec:   
    while(isdigit(*ptr)) {
	value = value*10 + (*ptr-'0');
	ptr++;
	len++;
    }
    goto done;
hex:
    while(isxdigit(*ptr)) {
	if ((*ptr >= '0') && (*ptr <= '9'))
	    value = (value << 4) + (*ptr-'0');
	else if ((*ptr >= 'A') && (*ptr <= 'F'))
	    value = (value << 4) + ((*ptr-'A')+10);
	else
	    value = (value << 4) + ((*ptr-'a')+10);
	ptr++;
	len++;
    }
    goto done;
done:
    if (want_arg && (len == 0)) {
	char* name = ptr;
	int i;
	while(*ptr && !isblank(*ptr)) { len++; ptr++; }
	if ((i = insert_symbol(name, len, symtab)) >= 0) {
	    insert_patch(i, slot, addr, symtab);
	}
    }
    *pptr = ptr;
    if (ins >= 0) {
	*insp = ins;
	*dstp = value;
	if (want_arg && (len > 0))
	    return TOKEN_MNEMONIC2;
	return TOKEN_MNEMONIC1;
    }
    if ((len == 0) || !(isblank(*ptr) || (*ptr=='\0'))  )
	return TOKEN_ERROR;
    *insp = value;
    return TOKEN_VALUE;
}

// parse an instruction line or number
// return:
//   ins LAST instruction (insx)
//   -1  ERROR
//   -2  EOF (stdio)
//   -3  EOF (closed)
//
//
int f18_scan_line(int fd,
		  int* line_ptr,
		  char* line_buf,
		  size_t line_buf_size,
		  uint18_t* addr_ptr,uint18_t* node_ptr,
		  uint18_t* mem_ptr, f18_symbol_table_t* symtab)
{
    int i, r;
    char* ptr;
    uint18_t dest;    
    uint18_t ins = 0;
    uint18_t insx;
    int enc = 1;
    int slot = 0;
    uint18_t addr = *addr_ptr & MASK6;
again:
    i = 0;
    line_buf[0] = '\0';
    while(i < line_buf_size-1) {
	r = read(fd, &line_buf[i], 1);
	// printf("buf[%d]=%c, r = %d\n", i, buf[i], r);
	if (r == 0) {     // end of file
	    if (i > 0) { // we have content
		break;
	    }
	    line_buf[i] = '\0';	    
	    if (fd == 0)  // it was stdin !
		return -2;
	    return -3;
	}
	else if (r < 0) {
	    line_buf[i] = '\0';	    
	    return -1;
	}
	else if (line_buf[i] == '\n') {
	    (*line_ptr)++;
	    break;
	}
	i++;
    }
    line_buf[i] = '\0';
    ptr = line_buf;
    // printf("LINE: %d: %s\n", *line_ptr, line_buf);
    i = parse_ins(&ptr, &insx, slot, addr, &dest, symtab);
    // printf("i = %d, insx=%03x, dest=%05x\n", i, insx, dest);
    switch(i) {
    case TOKEN_EMPTY:
	goto again;
    case TOKEN_MNEMONIC1:
	// printf("[%s]", f18_ins_name[insx]);
	ins = (insx << 13);
	break;
    case TOKEN_MNEMONIC2:
	if (insx == META_DEF) {
	    char* name;
	    int len = 0;
	    while(*ptr && isblank(*ptr)) ptr++;
	    name = ptr;
	    while(*ptr && !isblank(*ptr)) { len++; ptr++; }
	    if (add_symbol(name, len, slot, addr, symtab) < 0)
		printf("warning: could not add symbol %-*s to symtab\n",
		       len, name);
	    goto again;
	}
	if (insx == META_ORG) {
	    *addr_ptr = dest;
	    addr = dest & MASK6;
	}
	else if (insx == META_NODE) {
	    // 000 - 717
	    if ( ((dest / 100) > 7) ||
		 ((dest % 100) > 17))
		return -1;
	    *node_ptr = dest;
	}
	else {
	    // printf("[%s:%03x]", f18_ins_name[insx], dest);
	    ins |= (insx << 13);
	    mem_ptr[addr] = encode_dest(enc, ins, MASK13, addr, dest);
	}
	return insx;
    case  TOKEN_VALUE:
	mem_ptr[addr] = (insx & MASK18); // value not encoded
	return META_VALUE;
    default:
	return -1;
    }
    slot++;
    i = parse_ins(&ptr, &insx, slot, addr, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY: // assume rest of opcode are nops (warn?)
	ins = (ins | (INS_NOP<<8) | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	mem_ptr[addr] = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	// printf("[%s]", f18_ins_name[insx]);	
	ins |= (insx << 8);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins_name[insx], dest);
	ins |= (insx<<8);
	mem_ptr[addr] = encode_dest(enc, ins, MASK8, addr, dest);
	return insx;
    default:
	return -1;
    }
    slot++;
    i = parse_ins(&ptr, &insx, slot, addr, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	mem_ptr[addr] = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	// printf("[%s]", f18_ins_name[insx]);	
	ins |= (insx << 3);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins_name[insx], dest);
	ins |= (insx<<3);
	mem_ptr[addr] = encode_dest(enc, ins, MASK3, addr, dest);
	return insx;
    default:
	return -1;
    }
    slot++;
    i = parse_ins(&ptr, &insx, slot, addr, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP>>2)) ^ IMASK;
	mem_ptr[addr] = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	if ((insx & 3) != 0) {
	    fprintf(stderr, "scan error: bad slot3 instruction used %s\n",
		    f18_ins_name[insx]);
	    return -1;
	}
	// printf("[%s]", f18_ins_name[insx]);		
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	mem_ptr[addr] = ins;
	return insx;
    default:
	return -1;
    }
}

