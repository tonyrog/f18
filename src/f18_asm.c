#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18_sym.h"
#include "f18_voc.h"
#include "f18_asm.h"
#include "f18_strings.h"

//
// parse:
//   org  <number>
//   node <number>
//   ':'  <name> ...
//   <uins>
//   <uins-colon> <dest>
//   <word>           ( == call: <word-addr> )
//   <word> ';'       ( == jump: <word-addr> )
//   <number>
//   <blank>
//

int parse_mnemonic(char* word, int len)
{
    symindex_t si;
    if ((si = sym_find_by_namelen(word, len, &ins_symbols)) != NOSYM)
	return f18_ins[si].value;
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
	return d;
    }
    else
	return instr | (addr & ~mask) | (dest & mask);
}	    


// skip to next character, skip blank and comments
// (could be more FORTHy)
static char* next_non_blank(char* ptr)
{
    while(*ptr) {
	switch(*ptr) {
	case '\0': return ptr;
	case ' ':  ptr++; break;
	case '\t': ptr++; break;
	case '(':
	    ptr++; while(*ptr && (*ptr != ')')) ptr++;
	    if (*ptr == ')') ptr++;
	    break;
	case '\\': ptr++; while(*ptr) ptr++; return ptr;
	default: return ptr;
	}
    }
    return ptr;
}

// skip non blanks
static char* next_blank(char* ptr)
{
    while(*ptr) {
	switch(*ptr) {
	case '\0': return ptr;
	case ' ':  return ptr;
	case '\t': return ptr;
	default: ptr++; break;
	}
    }
    return ptr;
}

static int is_number(char* arg, int len, uint18_t* valp)
{
    uint18_t value = 0;
    char* ptr = arg;
    int n = 0;

    if ((ptr[0] == '0')) {
	if (ptr[1] == 'x') ptr += 2;	
	while(isxdigit(*ptr)) {
	    if ((*ptr >= '0') && (*ptr <= '9'))
		value = (value << 4) + (*ptr-'0');
	    else if ((*ptr >= 'A') && (*ptr <= 'F'))
		value = (value << 4) + ((*ptr-'A')+10);
	    else
		value = (value << 4) + ((*ptr-'a')+10);
	    ptr++;
	    n++;
	}
	*valp = value;
	return (n > 0);
    }
    else {
	while(isdigit(*ptr)) {
	    value = value*10 + (*ptr-'0');
	    ptr++;
	    n++;
	}
    }
    *valp = value;
    return (n > 0);    
}
  

int parse_ins(char** pptr,uint18_t* insp,
	      int slot, uint18_t addr,
	      uint18_t* dstp,
	      f18_voc_t voc)
{
    char* ptr = *pptr;
    char* word;
    char* arg;
    uint18_t value = 0;
    symindex_t si;
    int len = 0;
    int ins;
    int want_arg = 0;

    ptr = next_non_blank(ptr);
    // printf("INS: %s\n", ptr);
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
number_arg:
    ptr = next_non_blank(ptr);
    arg = ptr;
    ptr = next_blank(ptr);
    len = ptr - arg;
    if (is_number(arg, len, &value))
	goto done;
    if ((si = sym_find_by_namelen(arg, len, &io_symbols)) != NOSYM) {
	value = io_symbols.symbol[si].value;
	len = 1;
	goto done;
    }
    else if ((si = voc_find_by_namelen(arg, len, voc)) != NOSYM) {
	if (VOC_SYMTYP(voc, si) == 'R') { // unresolved
	    value = VOC_SYMVAL(voc, si);
	}	
	else if (VOC_SYMTYP(voc, si)== 'U') { // unresolved
	    voc_insert_patch(si, slot, addr, voc);
	    value = 0;
	}
	len = 1;
	goto done;
    }
    ptr = (char*) arg;

done:
    if (want_arg && (len == 0)) {
	char* name = ptr;
	while(*ptr && !isblank(*ptr)) { len++; ptr++; }
	if ((si = voc_insert(name, len, voc)) != NOSYM) {
	    voc_insert_patch(si, slot, addr, voc);
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
int f18_asm_line(int fd,
		 int* line_ptr,
		 char* line_buf,
		 size_t line_buf_size,
		 uint18_t* addr_ptr,uint18_t* node_ptr,
		 uint18_t* mem_ptr, f18_voc_t voc)
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
    i = parse_ins(&ptr, &insx, slot, addr, &dest, voc);
    // printf("i = %d, insx=%03x, dest=%05x\n", i, insx, dest);
    switch(i) {
    case TOKEN_EMPTY:
	goto again;
    case TOKEN_MNEMONIC1:
	ins = (insx << 13);
	break;
    case TOKEN_MNEMONIC2:
	if (insx == META_DEF) {
	    char* name;
	    int len = 0;
	    while(*ptr && isblank(*ptr)) ptr++;
	    name = ptr;
	    while(*ptr && !isblank(*ptr)) { len++; ptr++; }
	    if (voc_add(name, len, addr, mem_ptr, voc) == NOSYM)
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
    i = parse_ins(&ptr, &insx, slot, addr, &dest, voc);
    switch(i) {
    case TOKEN_EMPTY: // assume rest of opcode are nops (warn?)
	ins = (ins | (INS_NOP<<8) | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	mem_ptr[addr] = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	ins |= (insx << 8);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	ins |= (insx<<8);
	mem_ptr[addr] = encode_dest(enc, ins, MASK8, addr, dest);
	return insx;
    default:
	return -1;
    }
    slot++;
    i = parse_ins(&ptr, &insx, slot, addr, &dest, voc);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	mem_ptr[addr] = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	ins |= (insx << 3);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	ins |= (insx<<3);
	mem_ptr[addr] = encode_dest(enc, ins, MASK3, addr, dest);
	return insx;
    default:
	return -1;
    }
    slot++;
    i = parse_ins(&ptr, &insx, slot, addr, &dest, voc);
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

