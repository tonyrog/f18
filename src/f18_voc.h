#ifndef __VOC_H__
#define __VOC_H__

#include "f18_sym.h"

#define RAM_SYMBOLS  0
#define ROM_SYMBOLS  1
#define IO_SYMBOLS   2
#define INS_SYMBOLS  3
#define EOF_SYMBOLS  4

#define VOC_SYMBOL(voc, si) ((voc)[VOC_INDEX(si)]->symbol[SYM_INDEX(si)])
#define VOC_SYMNAM(voc, si) SYMNAM(&VOC_SYMBOL((voc),(si)))
#define VOC_SYMTYP(voc, si) SYMTYP(&VOC_SYMBOL((voc),(si)))
#define VOC_SYMLEN(voc, si) SYMLEN(&VOC_SYMBOL((voc),(si)))
#define VOC_SYMSTR(voc, si) SYMSTR(&VOC_SYMBOL((voc),(si)))
#define VOC_SYMVAL(voc, si) (VOC_SYMBOL((voc),(si)).value)


// array of symbols tables stack to search in
typedef f18_symbol_table_t* f18_voc_t[5];

extern void voc_setup(f18_voc_t voc,
		      f18_symbol_table_t* ram_syms,
		      const f18_symbol_table_t* rom_syms);
extern symindex_t voc_find_by_namelen(const char* name, int len,
				      const f18_voc_t voc);
extern symindex_t voc_find_by_name(const char* name,
				   const f18_voc_t voc);

extern symindex_t voc_find_by_value(uint18_t addr,
				    const f18_voc_t voc);
extern symindex_t voc_find_by_addr(uint18_t addr,
				   const f18_voc_t voc);

extern int voc_insert(char* word, int len, f18_voc_t voc);
extern int voc_insert_patch(symindex_t si, int slot, uint18_t addr,
			    f18_voc_t voc);
extern symindex_t voc_add(char* name, int len, uint18_t addr,
			  uint18_t* ram, f18_voc_t voc);

#endif
