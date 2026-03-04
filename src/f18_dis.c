// F18 disassemble instructions

#include <string.h>
#include "f18.h"

#define HEXADECIMAL_ADDR

#define dec(d) ((d)+'0')
#define hex(d) (((d)<10) ? ((d)+'0') : (((d)-10)+'a'))

// wrap addresses into regular ROM/RAM/IO addresses
uint18_t normalize_addr(uint18_t addr)
{
    if (addr <= RAM_END2)
	return (addr & MASK6);
    else if (addr <= ROM_END2)
	return ROM_START + ((addr-ROM_START) & MASK6);
    else if (addr <= IOREG_END)
	return addr; // io-address
    return addr & MASK9;
}

int lookup_sym(const f18_symbol_t* sym, uint18_t addr)
{
    int s = 0;
    uint18_t a;

    a = normalize_addr(addr);

    if (sym == NULL)
	return -1;
    while (sym[s].name != NULL) {
	uint9_t b = normalize_addr(sym[s].value);
	if (a == b) {
	    return s;
	}
	s++;
    }
    return -1;
}


//  <<slot0:5>> <<slot1:5> <<<slot2:5> <<slot3:3>>
// 

int f18_disasm_instruction(uint18_t addr, uint18_t I, const f18_symbol_t* sym,
			   char* ptr, size_t maxlen)
{
    char* ptr0 = ptr;
    char* ptr_end = ptr + maxlen;
    uint18_t I0 = I;
    int i;
    int n = 4;
    int d;
    
    I <<= 2;
    for (i = 0; i < 4; i++) {
	uint5_t ins = (I >> 15) & 0x1f;
	const char* ins_name = f18_ins_name[ins];
	int l = strlen(ins_name);
	int r = (ptr_end - ptr);
	int m = (r > l) ? l : r;
	memcpy(ptr, ins_name, m);
	ptr += m;
	switch(ins) {
	case INS_RETURN:
	case INS_EXECUTE:
	    goto done;
	case INS_PJUMP:
	case INS_PCALL:
	case INS_NEXT:
	case INS_IF:
	case INS_MINUS_IF:
	    goto load;
	}
	if (ptr < ptr_end) *ptr++ = ' ';
	I <<= 5;
	n--;
    }
    if (ptr < ptr_end) *(ptr-1) = '\0';
    return ptr - ptr0;
load:
    switch(n) {
    case 4: I = (addr & ~MASK10) | ((I0 ^ IMASK) & MASK10); break;
    case 3: I = (addr & ~MASK8)  | ((I0 ^ IMASK) & MASK8); break;
    case 2: I = (addr & ~MASK3)  | ((I0 ^ IMASK) & MASK3); break;
    }
    if (ptr < ptr_end) *ptr++ = ':';
    if ((i = lookup_sym(sym, I)) != -1) {
	int l = SYMLEN(&sym[i]);
	int r = (ptr_end - ptr);
	int m = (r > l) ? l : r;
	memcpy(ptr, sym[i].name, m);
	ptr += m;
    }
    else if ((i = lookup_sym(iosym, I)) != -1) {
	int l = SYMLEN(&iosym[i]);
	int r = (ptr_end - ptr);
	int m = (r > l) ? l : r;
	memcpy(ptr, iosym[i].name, m);
	ptr += m;
    }    
    else { // decimal. configure? 3, 8, 10-bit
	// I = 0..1023
#ifdef HEXADECIMAL_ADDR
	d = (I >> 8) & 0xf;
	if (ptr < ptr_end) *ptr++ = hex(d);
	d = (I >> 4) & 0xf;
	if (ptr < ptr_end) *ptr++ = hex(d);
	d = (I & 0xf);
	if (ptr < ptr_end) *ptr++ = hex(d);
#else
	d = (I / 1000); I %= 1000;	
	if (d != 0) { if (ptr < ptr_end) *ptr++ = dec(d); }
	d = (I / 100); I %= 100;	
	if (ptr < ptr_end) *ptr++ = dec(d);
	d = (I / 10); I %= 10;		
	if (ptr < ptr_end) *ptr++ = dec(d);
	d = I;
	if (ptr < ptr_end) *ptr++ = dec(d);
#endif
    }

done:
    if (ptr < ptr_end) *ptr++ = '\0';
    return ptr - ptr0;    
}

void f18_disasm(const uint18_t* insp, const f18_symbol_t* sym,
		uint18_t addr, size_t n)
{
    while(n--) {
	char ins_buf[32];
	uint32_t val = *insp++ ^ IMASK;
	int i = 0;

	f18_disasm_instruction(addr+1, val, sym, ins_buf, sizeof(ins_buf));
	if ((i = lookup_sym(sym, addr)) != -1) {
	    fprintf(stdout, "%s%s:\n", sym[i].name,(addr & 0x200)?".p":"");
	}
	fprintf(stdout, "%03x: %05x: %s\n", addr, val, ins_buf);
	addr++;
    }
}
