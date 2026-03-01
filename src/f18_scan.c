#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18_scan.h"

// sort on address
const f18_symbol_t iosym[] = {
    { IOREG__D_U, "-d--u"     },
    { IOREG__D__, "-d--"      },
    { IOREG__DLU, "-dlu"      },
    { IOREG__DL_, "-dl-"      },
    { IOREG_DATA, "data"      },
    { IOREG____U, "---u"      },
    { IOREG_IO,   "io"        },
    { IOREG___LU, "--lu"      },
    { IOREG___L_, "--l-"      },    
    { IOREG_RD_U, "rd-u"      },    
    { IOREG_RD__, "rd--"      },
    { IOREG_RDLU, "rdlu"      },    
    { IOREG_RDL_, "rdl-"      },
    { IOREG_R__U, "r--u"      },
    { IOREG_R___, "r---"      },
    { IOREG_R_LU, "r-lu"      },
    { IOREG_R_L_, "r-l-"      },
};

const f18_symbol_table_t iosym_tab  =
{
    (f18_symbol_t*) iosym,        // first
    (f18_symbol_t*) iosym + 17,   // next
    NULL, NULL,
    0, NULL,  // no heap
};

int find_symbol(char* name, f18_symbol_table_t* symtab)
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

int lookup_symbol(char** pptr, f18_symbol_table_t* symtab)
{
    char* ptr = *pptr;
    f18_symbol_t* sp = symtab->next - 1;
    int n = symtab->next - symtab->symbol;

    while(n) {
	int len = strlen(sp->name);
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
    char* nptr = symtab->nptr - (len+1);

    sp->name = nptr;
    memcpy(sp->name, word, len);
    sp->name[len] = '\0';
    sp->value = 0;

    symtab->nptr = nptr;
    symtab->next++;
    return sp - symtab->symbol;
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
    if (enc)
	return ((instr ^ IMASK) & ~mask) |
	    (addr & ~mask) | (dest & mask);
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

int parse_ins(char** pptr,uint18_t* insp,uint18_t* dstp,
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
	// printf("%s = %03x\n", iosym_tab.symbol[i].name, value);
	len = 1;
	goto done;
    }
    else if (( i = lookup_symbol(&ptr, symtab)) >= 0) {
	value = symtab->symbol[i].value;
	// printf("%s = %03x\n", symtab->symbol[i].name, value);
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
    if ((ptr[0] == '0') && (ptr[1] == 'b')) {
	ptr += 2;
	goto bin;
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
bin:
    while((ptr[0] == '0') || (ptr[0] == '1')) {
	value = (value << 1) + (*ptr - '0');
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
		  uint18_t* addr_ptr,uint18_t* node_ptr,
		  uint18_t* data_ptr, f18_symbol_table_t* symtab)
{
    int i, r;
    char* ptr;
    uint18_t dest;    
    char buf[256];
    uint18_t ins = 0;
    uint18_t insx;
    int enc = 1;
    
again:
    i = 0;
    while(i < (sizeof(buf)-1)) {
	r = read(fd, &buf[i], 1);
	if (r == 0) {     // input stream has closed
	    if (fd == 0)  // it was stdin !
		return -2;
	    return -3;
	}
	else if (r < 0)
	    return -1;
	else if (buf[i] == '\n') {
	    (*line_ptr)++;
	    break;
	}
	i++;
    }
    buf[i] = '\0';
    ptr = buf;
    // printf("LINE: %d: %s\n", *line_ptr, buf);
    i = parse_ins(&ptr, &insx, &dest, symtab);
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
	    if ((i = insert_symbol(name, len, symtab)) >= 0) {
		symtab->symbol[i].value = *addr_ptr;
		// printf("WORD [%s] = %03x\n", symtab->symbol[i].name, symtab->symbol[i].value);
	    }
	    goto again;
	}
	if (insx == META_ORG)
	    *addr_ptr = dest;
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
	    *data_ptr = encode_dest(enc, ins, MASK13, *addr_ptr, dest);
	}
	return insx;
    case  TOKEN_VALUE:
	*data_ptr = (insx & MASK18); // value not encoded
	return META_VALUE;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY: // assume rest of opcode are nops (warn?)
	ins = (ins | (INS_NOP<<8) | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	*data_ptr = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	// printf("[%s]", f18_ins_name[insx]);	
	ins |= (insx << 8);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins_name[insx], dest);
	ins |= (insx<<8);
	*data_ptr = encode_dest(enc, ins, MASK8, *addr_ptr, dest);
	return insx;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	*data_ptr = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	// printf("[%s]", f18_ins_name[insx]);	
	ins |= (insx << 3);
	break;
    case TOKEN_MNEMONIC2:
	if (insx >= 0x20) return -1;
	// printf("[%s:%03x]", f18_ins_name[insx], dest);
	ins |= (insx<<3);
	*data_ptr = encode_dest(enc, ins, MASK3, *addr_ptr, dest);
	return insx;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest, symtab);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP>>2)) ^ IMASK;
	*data_ptr = ins;
	return insx;
    case TOKEN_MNEMONIC1:
	if ((insx & 3) != 0) {
	    fprintf(stderr, "scan error: bad slot3 instruction used %s\n",
		    f18_ins_name[insx]);
	    return -1;
	}
	// printf("[%s]", f18_ins_name[insx]);		
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	*data_ptr = ins;
	return insx;
    default:
	return -1;
    }
}

