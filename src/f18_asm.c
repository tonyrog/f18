#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18_sym.h"
#include "f18_voc.h"
#include "f18_asm.h"
#include "f18_strings.h"

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
	case '\n': return ptr;
	default: ptr++; break;
	}
    }
    return ptr;
}

// <hex> = [0-9a-fA-F]
// check and return number 0x<hex>+ | 0<hex>+) | [0-9]+
// return 0 if not number and 1 otherwise

static int is_number(char* ptr, int len, int base, uint18_t* valp)
{
    uint18_t value = 0;
    char* ptr_end = ptr + len;
    int n = 0;

    if (base == 0) {
	if ((len >= 2) && (ptr[0]=='0') && (ptr[1] == 'x')) {
	    ptr += 2;
	    base = 16;
	}
	else if ((len >= 1) && (ptr[0] == '0')) { // in C lingua this is 8
	    base = 16;
	}
	else
	    base = 10;
    }
    while(ptr < ptr_end) {
	int c = *ptr++;
	if ((c >= '0') && (c <= '9'))
	    c -= '0';
	else if ((c >= 'A') && (c <= 'F'))
	    c = (c - 'A') + 10;
	else if ((c >= 'a') && (c <= 'f'))
	    c = (c - 'a') + 10;
	if (c >= base)
	    return 0;
	n++;
	value = value*base + c;
    }
    *valp = value;
    return (n > 0);
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
  
//
// parse:
//   org  <number>
//   node <number>
//   ':'  <name> ...
//   call: dest
//   jump: dest
//   next: dest
//   if: dest
//   -if: dest
//   call:dest
//   jump:dest
//   next:dest
//   if:dest
//   -if:dest
//   dest             ( == call: dest )
//   dest ';'         ( == jump: dest )
//   <number>
//   <blank>
//

void print_word(char* msg, char* ptr, char* ptr_end)
{
    fwrite(msg, sizeof(char), strlen(msg), stdout);
    fprintf(stdout, "[");
    fwrite(ptr, sizeof(char), ptr_end - ptr, stdout);
    fprintf(stdout, "]\n");
}

int parse_ins(char** pptr,uint18_t* insp,
	      int slot, uint18_t addr,
	      uint18_t* dstp,
	      f18_voc_t voc)
{
    char* ptr = *pptr;
    char* ptr1;
    uint18_t value = 0;
    symindex_t si;
    int len = 0;
    int ins;

    ptr = next_non_blank(ptr);
    ptr1 = next_blank(ptr);
    print_word("INS", ptr, ptr1);
    if ((len = ptr1-ptr) == 0) {
	*pptr = ptr1;
	return TOKEN_EMPTY;
    }

    // NUMBER
    if (is_number(ptr, len, 0, &value)) {
	*pptr = ptr1;	
	*insp = value;
	return TOKEN_VALUE;
    }
    // CONSTANT
    else if ((si=sym_find_by_namelen(ptr,len,&io_symbols)) != NOSYM) {
	*pptr = ptr1;		
	*insp = io_symbols.symbol[si].value;
	return TOKEN_VALUE;
    }
    // WORD
    if ((si = sym_find_by_namelen(ptr,len,&ins_symbols)) != NOSYM) {
	// INSTRUCTION
	*pptr = ptr1;
	ptr = ptr1;
	ins = f18_ins[si].value;
	len = 0;
	switch(ins) {
	case INS_PJUMP:
	case INS_PCALL:
	case INS_NEXT:
	case INS_IF:
	case INS_MINUS_IF:
	    goto dest;
	case META_DEF:
	    *insp = ins;
	    return TOKEN_MNEMONIC2;
	case META_ORG:
	case META_NODE:
	    goto dest;
	default:  // regular instruction
	    *insp = ins;
	    *dstp = value;
	    return TOKEN_MNEMONIC1;
	}
    }
    ins = INS_PCALL;
    goto dest1;
    
dest:
    // scan name constant
    ptr = next_non_blank(ptr);
    ptr1 = next_blank(ptr);
    print_word("DST", ptr, ptr1);
    len = ptr1 - ptr;
dest1:
    if (is_number(ptr,len,0,&value)) {
	printf("NUMBER %d\n", value);
	*pptr = ptr1;	
	*insp = ins;
	*dstp = value;
	return TOKEN_MNEMONIC2;
    }
    else if ((si=sym_find_by_namelen(ptr,len,&io_symbols)) != NOSYM) {
	*pptr = ptr1;
	*insp = ins;
	*dstp = io_symbols.symbol[si].value;
	return TOKEN_MNEMONIC2;
    }

    // WORD
    if ((si = voc_find_by_namelen(ptr, len, voc)) != NOSYM) {
	if (VOC_SYMTYP(voc, si) == 'R') {
	    *pptr = ptr1;
	    *dstp = VOC_SYMVAL(voc, si);
	    *insp = INS_PCALL;
	    return TOKEN_MNEMONIC2;
	}
	else if (VOC_SYMTYP(voc, si)== 'U') { // unresolved
	    voc_insert_patch(si, slot, addr, voc);
	    *pptr = ptr1;	    
	    *insp = INS_PCALL;
	    *dstp = 0;
	    return TOKEN_MNEMONIC2;
	}
	return TOKEN_ERROR;	
    }
    else {
	if ((si = voc_insert(ptr, len, voc)) != NOSYM) {
	    voc_insert_patch(si, slot, addr, voc);
	    *pptr = ptr1;
	    *insp = INS_PCALL;
	    *dstp = 0;
	    return TOKEN_MNEMONIC2;
	}
	return TOKEN_ERROR;
    }
    *pptr = ptr1;
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
int f18_asm_line(int fd, int* line_ptr, char* line_buf, size_t line_buf_size,
		 uint18_t* addr_ptr,uint18_t* node_ptr,
		 uint18_t* mem_ptr, f18_voc_t voc)
{
    int i, r;
    char* ptr;
    uint18_t dest;    
    uint18_t ins = 0;
    uint18_t insx;
    int enc = 1;
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
    printf("LINE: %d: %s\n", *line_ptr, line_buf);
    i = parse_ins(&ptr, &insx, 0, addr, &dest, voc);
    printf("i=%d,s=0,insx=%03x, dest=%05x\n", i, insx, dest);
    switch(i) {
    case TOKEN_EMPTY:
	goto again;
    case TOKEN_MNEMONIC1:
	ins = (insx << 13);
	break;
    case TOKEN_MNEMONIC2:
	switch(insx) {
	case META_DEF: {
	    char* name;
	    int len = 0;
	    ptr = next_non_blank(ptr);
	    name = ptr;
	    ptr = next_blank(ptr);
	    len = ptr - name;
	    if (voc_add(name, len, addr, mem_ptr, voc) == NOSYM)
		printf("warning: could not add symbol %-*s to symtab\n",
		       len, name);
	    goto again;
	}
	case META_ORG:
	    *addr_ptr = dest;
	    addr = dest & MASK6;
	    break;
	case META_NODE: // 000 - 717
	    if ( ((dest / 100) > 7) ||
		 ((dest % 100) > 17))
		return -1;
	    *node_ptr = dest;
	    break;
	default:
	    ins |= (insx << 13);
	    mem_ptr[addr] = encode_dest(enc, ins, MASK13, addr, dest);	    
	    break;
	}
	return insx;
    case  TOKEN_VALUE:
	mem_ptr[addr] = (insx & MASK18); // value not encoded
	return META_VALUE;
    default:
	return -1;
    }
    i = parse_ins(&ptr,&insx,1,addr,&dest,voc);
    printf("i=%d,s=1,insx=%03x, dest=%05x\n", i, insx, dest);    
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
    i = parse_ins(&ptr,&insx,2,addr,&dest,voc);
    printf("i=%d,s=2,insx=%03x, dest=%05x\n", i, insx, dest);        
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
    i = parse_ins(&ptr,&insx,3,addr,&dest,voc);
    printf("i=%d,s=3,insx=%03x, dest=%05x\n", i, insx, dest);        
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
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	printf("mem_ptr=%p,addr=%03x, ins=%05x\n", mem_ptr, addr, ins);
	mem_ptr[addr] = ins;
	return insx;
    default:
	return -1;
    }
}

