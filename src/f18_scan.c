#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18_sym.h"
#include "f18_scan.h"
#include "f18_strings.h"


/*
int lookup_symbol(char** pptr, const f18_symbol_table_t* symtab)
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
*/

int parse_mnemonic(char* word, int len)
{
    int i;
    if ((i = find_symbol_by_namelen(word, len, &f18_ins_symtab)) >= 0)
	return i;
    if ((len == 3) && (memcmp(word, "org", 3) == 0))
	return META_ORG;
    if ((len == 4) && (memcmp(word, "node", 4) == 0))
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
    const char* arg;
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
    arg = ptr;
    while(*ptr && !isblank(*ptr)) { len++; ptr++; }
    if ((i = find_symbol_by_namelen(arg, len, &io_symtab)) >= 0) {
	value = io_symtab.symbol[i].value;
	len = 1;
	goto done;
    }
    else if ((i = find_symbol_by_namelen(arg, len, symtab)) >= 0) {
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
    // dest was not a symbol, restore ptr
    ptr = (char*) arg;
number_arg: 	// 0xabcd | 0b1010 | 0abcd (hex) | 123 (dec)
    len = 0;
    while (*ptr && isblank(*ptr)) ptr++; // may enter here multiple ways
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
	// printf("[%s]", f18_ins[insx].name);
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
	    // printf("[%s:%03x]", f18_ins[insx].name, dest);
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
	// printf("[%s]", f18_ins[insx].name);	
	ins |= (insx << 8);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins[insx].name, dest);
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
	// printf("[%s]", f18_ins[insx].name);	
	ins |= (insx << 3);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins[insx].name, dest);
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
		    f18_ins[insx].name);
	    return -1;
	}
	// printf("[%s]", f18_ins[insx].name);		
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	mem_ptr[addr] = ins;
	return insx;
    default:
	return -1;
    }
}

