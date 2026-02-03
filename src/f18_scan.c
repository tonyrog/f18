#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "f18.h"
#include "f18_scan.h"

struct {
    char* name;
    uint18_t value;
} symbol[] =  {
    { "stdio",   IOREG_STDIO },
    { "stdin",   IOREG_STDIN },
    { "stdout",  IOREG_STDOUT },
    { "tty",     IOREG_TTY },

    { "io",      IOREG_IO },
    { "data",    IOREG_DATA },
    { "---u",    IOREG____U },
    { "--l-",    IOREG___L_ },
    { "--lu",    IOREG___LU },
    { "-d--",    IOREG__D__ },
    { "-d--u",   IOREG__D_U },
    { "-dl-",    IOREG__DL_ },
    { "-dlu",    IOREG__DLU },
    { "r---",    IOREG_R___ },
    { "r--u",    IOREG_R__U },
    { "r-l-",    IOREG_R_L_ },
    { "r-lu",    IOREG_R_LU },
    { "rd--",    IOREG_RD__ },
    { "rd-u",    IOREG_RD_U },
    { "rdl-",    IOREG_RDL_ },
    { "rdlu",    IOREG_RDLU },
    { NULL, 0 }
};

int parse_symbol(char** pptr, uint18_t* valuep)
{
    int i = 0;
    char*ptr = *pptr;

    while(symbol[i].name != NULL) {
	int n = strlen(symbol[i].name);
	if (strncmp(ptr, symbol[i].name, n) == 0) {
	    if ((ptr[n] == '\0') || isblank(ptr[n])) {
		*pptr = ptr + n;
		*valuep = symbol[i].value;
		return 1;
	    }
	}
	i++;
    }
    return 0;
}

int parse_mnemonic(char* word, int n)
{
    int i;
    for (i=0; i<32; i++) {
	int len = strlen(f18_ins_name[i]);
	if ((n == len) && (memcmp(word, f18_ins_name[i], n) == 0))
	    return i;
    }
    return -1;
}

//
// parse:
//   
//   ( '(' .* ')' )* <mnemonic>
//   ( '(' .* ')' )* <mnemonic>':'<dest>
//   ( '(' .* ')' )* <hex>
//   ( '(' .* ')' )* \<blank> .*
//

int parse_ins(char** pptr, uint18_t* insp, uint18_t* dstp)
{
    char* ptr = *pptr;
    char* word;
    uint18_t value = 0;
    int n = 0;
    int ins;
    int has_dest = 0;

    while(isblank(*ptr) || (*ptr == '(')) {
	while(isblank(*ptr)) ptr++;
	if (*ptr == '(') {
	    ptr++;
	    while(*ptr && (*ptr != ')'))
		ptr++;
	    if (*ptr) ptr++;
	}
    }

    if ( (*ptr == '\\') && (isblank(*(ptr+1)) || (*(ptr+1)=='\0')) ) {
	while(*ptr != '\0') ptr++;  // skip rest
    }
    word = ptr;
    // fprintf(stderr, "WORD [%s]", word);
    while (*ptr && !isblank(*ptr) && (*ptr != ':')) { ptr++; n++; }
    if (n == 0) return TOKEN_EMPTY;
    // first check mnemonic
    ins = parse_mnemonic(word, n);
    // check reset is destination
    switch(ins) {
    case -1:
	has_dest = 0;
	ptr = word;
	break;
    case INS_PJUMP:
    case INS_PCALL:
    case INS_NEXT:
    case INS_IF:
    case INS_MINUS_IF:
	if (*ptr == ':') { // force?
	    has_dest = 1;
	    ptr++;
	}
	break;
    default:
	has_dest = 0;
	break;
    }

    // parse number or dest
    if (parse_symbol(&ptr, &value))
	n = 1;
    else {
	n = 0;
	while(isxdigit(*ptr)) {
	    value <<= 4;
	    if ((*ptr >= '0') && (*ptr <= '9'))
		value += (*ptr-'0');
	    else if ((*ptr >= 'A') && (*ptr <= 'F'))
		value += ((*ptr-'A')+10);
	    else
		value += ((*ptr-'a')+10);
	    ptr++;
	    n++;
	}
    }
    *pptr = ptr;
    if (ins >= 0) {
	*insp = ins;
	*dstp = value;
	if (has_dest && (n > 0))
	    return TOKEN_MNEMONIC2;
	return TOKEN_MNEMONIC1;
    }
    if ((n == 0) || !(isblank(*ptr) || (*ptr=='\0'))  )
	return TOKEN_ERROR;
    *insp = value;
    return TOKEN_VALUE;
}

// parse an instruction line or numer
int scan_line(int fd, uint18_t* rdata)
{
    int i, r;
    char* ptr;
    uint18_t dest;    
    char buf[256];
    uint18_t ins;    
    uint18_t insx;
    
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
	else if (buf[i] == '\n')
	    break;
	i++;
    }
    buf[i] = '\0';
    ptr = buf;
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY: goto again;
    case TOKEN_MNEMONIC1:
	ins = (insx << 13);
	break;
    case TOKEN_MNEMONIC2:
	// instruction part is encoded (^IMASK) but dest is not (why)
	ins = (insx << 13) ^ IMASK;                // encode instruction
	ins = (ins & ~MASK10) | (dest & MASK10);  // set address bits
	*rdata = ins;
	return 0;
    case  TOKEN_VALUE:
	*rdata = (insx & MASK18); // value not encoded
	return 0;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY: // assume rest of opcode are nops (warn?)
	ins = (ins | (INS_NOP<<8) | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	*rdata = ins;
	return 0;
    case TOKEN_MNEMONIC1:
	ins |= (insx  << 8);
	break;
    case TOKEN_MNEMONIC2:
	ins = (ins | (insx << 8)) ^ IMASK;      // encode instruction
	ins = (ins & ~MASK8) | (dest & MASK8);  // set address bits
	*rdata = ins;
	return 0;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	*rdata = ins;
	return 0;
    case TOKEN_MNEMONIC1:
	ins |= (insx << 3);
	break;
    case TOKEN_MNEMONIC2:
	ins = (ins | (insx << 3)) ^ IMASK;      // encode instruction
	ins = (ins & ~MASK3) | (dest & MASK3);  // set address bits
	*rdata = ins;
	return 0;
    default:
	return -1;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP>>2)) ^ IMASK;
	*rdata = ins;
	return 0;
    case TOKEN_MNEMONIC1:
	if ((insx & 3) != 0)
	    fprintf(stderr, "scan error: bad slot3 instruction used %s\n",
		    f18_ins_name[insx]);
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	*rdata = ins;
	return 0;
    default:
	return -1;
    }
}
