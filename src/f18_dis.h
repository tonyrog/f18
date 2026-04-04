#ifndef __F18_DIS_H__
#define __F18_DIS_H__

#include "f18.h"
#include "f18_sym.h"

extern char* f18_disasm_uins(int slot, uint18_t addr, uint18_t I,
			     f18_voc_t voc,
			     char** pptr, size_t maxlen);
extern int f18_disasm_instruction(uint18_t addr, uint18_t I,
				  f18_voc_t voc,
				  char* ptr, size_t maxlen);
extern void f18_disasm(FILE* fout, const uint18_t* insp, f18_voc_t voc,
		       uint18_t addr, size_t n);

#endif
