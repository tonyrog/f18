// F18 disassemble instructions

#include <string.h>
#include "f18_dis.h"

#define dec(d) ((d)+'0')
#define hex(d) (((d)<10) ? ((d)+'0') : (((d)-10)+'a'))

// disassemble one micro instruction
// disassemble slot i in instruction I att address addr
// pptr contains a pointer to the start of the output buffer
// symtab is currenlty a rom symbol table.
// return the pointer to the start of the disassembled output
// the output is 0 terminated
char* f18_disasm_uins(int slot, uint18_t addr, uint18_t I,
		      f18_voc_t voc,
		      char** pptr, size_t maxlen)
{
    char* ptr = *pptr;
    char* ptr0 = ptr;
    char* ptr_end = ptr + maxlen;
    uint18_t I0 = I;
    int d;
    uint5_t ins;
    const char* ins_name;
    symindex_t si;
    int l, r, m;

    I = ((I << 2) << slot*5);
    ins = (I >> 15) & 0x1f;
    ins_name = f18_ins[ins].name;
    l = SYMLEN(&f18_ins[ins]);
    r = (ptr_end - ptr);
    m = (r > l) ? l : r;
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
	goto dest;
    }
    if (ptr < ptr_end) *ptr = '\0';
    *pptr = ptr;
    return ptr0;

dest:
    switch(slot) {
    case 0: I = (addr & ~MASK10) | ((I0 ^ IMASK) & MASK10); break;
    case 1: I = (addr & ~MASK8)  | ((I0 ^ IMASK) & MASK8); break;
    case 2: I = (addr & ~MASK3)  | ((I0 ^ IMASK) & MASK3); break;
    }
    if (ptr < ptr_end) *ptr++ = ':';
    if ((si = voc_find_by_addr(I, voc)) != NOSYM) {
	int l = VOC_SYMLEN(voc, si);
	int r = (ptr_end - ptr);
	int m = (r > l) ? l : r;
	memcpy(ptr, VOC_SYMNAM(voc, si), m);
	ptr += m;
    }
    else {
	d = (I >> 8) & 0xf;
	if (ptr < ptr_end) *ptr++ = hex(d);
	d = (I >> 4) & 0xf;
	if (ptr < ptr_end) *ptr++ = hex(d);
	d = (I & 0xf);
	if (ptr < ptr_end) *ptr++ = hex(d);
    }
done:
    if (ptr < ptr_end) *ptr = '\0';
    *pptr = ptr;
    return ptr0;
}

//
//  <<slot0:5>> <<slot1:5> <<<slot2:5> <<slot3:3>>
//  return number of @p found in instruction
//  -1 on error

int f18_disasm_instruction(uint18_t addr, uint18_t I,
			   f18_voc_t voc,
			   char* ptr, size_t maxlen)
{
    char* ptr_end = ptr + maxlen;
    int slot;
    int np = 0;

    for (slot = 0; slot < 4; slot++) {
	uint5_t ins = (((I << 2) << slot*5) >> 15) & 0x1f;
	if (f18_disasm_uins(slot, addr, I, voc, &ptr, (ptr_end-ptr)) == NULL)
	    return -1;
	if (ins == INS_FETCH_P)
	    np++;
	if (is_last_uinst(ins))
	    return 0;
	if (ptr < ptr_end) *ptr++ = ' ';
    }
    if (ptr < ptr_end) *ptr++ = '\0';
    return np;
}

void f18_disasm(FILE* fout, const uint18_t* insp, f18_voc_t voc,
		uint18_t addr, size_t n)
{
    int np = 0;
    
    while(n--) {
	char ins_buf[32];
	uint32_t val0 = *insp++;
	uint32_t val = val0 ^ IMASK;
	symindex_t si;

	if (np > 0) {
	    fprintf(fout, "%03x: %05x: %05x\n", addr, val, val0);
	    np--;
	}
	else {
	    np = f18_disasm_instruction(addr+1, val, voc,
					ins_buf, sizeof(ins_buf));
	    if ((si = voc_find_by_addr(addr, voc)) != NOSYM) {
		fprintf(fout, "%s%s:\n", VOC_SYMNAM(voc,si),
			(addr & 0x200)?".p":"");
	    }
	    fprintf(fout, "%03x: %05x: %s\n", addr, val, ins_buf);
	}
	addr++;
    }
}
