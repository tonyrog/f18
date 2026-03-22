#ifndef __F18_SYM_H__
#define __F18_SYM_H__

#include <stdlib.h>
#include "f18_types.h"

#define SYMNAM(sym) ((sym)->name)
#define SYMLEN(sym) ((sym)->name[-1])
#define SYMTYP(sym) ((sym)->name[-2])
#define SYMSTR(Name) ((char*)CAT2(SN_,Name)+2)

#define SYMTYP_RESOLVED   'R'
#define SYMTYP_UNRESOLVED 'U'

typedef struct {
    uint18_t value;
    char* name;      // [typ][len]name:len[0] [-1]=len, [-2]=typ
} f18_symbol_t;

typedef struct {
    uint9_t addr;  // patch address
    uint8_t slot;  // 0..2 (3 can not be used)
    uint9_t next;  // next patch address 0 = end
} f18_symbol_patch_t;

typedef struct {
    uint8_t* heap;        // start heap memory
    size_t   heap_size;   // total size of heap
    f18_symbol_t* symbol;
    f18_symbol_t* next;   // next slot to insert to
    char*    dp;        // name pointer from low heap to high
} f18_symbol_table_t;

#define RESET_SYMTAB(sp) do {						\
	(sp)->symbol = (f18_symbol_t*) ((sp)->heap);			\
	(sp)->next   =  (f18_symbol_t*) ((sp)->heap);			\
	(sp)->dp     = (char*)(((sp)->heap)) + (((sp)->heap_size));	\
    } while(0)

#define INIT_SYMTAB(sp, mem, memsize) do {		\
	(sp)->heap   = (mem);				\
	(sp)->heap_size = (memsize);			\
	RESET_SYMTAB(sp);				\
    } while(0)

// init of fixed (const) f18_symbols array
#define SYMTAB_INITALIZER(sarr) { 		\
  .heap = NULL,					\
  .heap_size = 0,				\
  .dp = NULL,				        \
  .symbol = (f18_symbol_t*)(sarr),		\
  .next = ((f18_symbol_t*)(sarr))+(sizeof((sarr))/sizeof(f18_symbol_t)), }

extern const f18_symbol_t f18_ins[32];
extern const f18_symbol_table_t ins_symbols;
extern const f18_symbol_table_t no_symbols;
extern const f18_symbol_table_t io_symbols;

// 18 bit symbol index value
//                  0:1 0:4   index:13    from sym_xxx functions
//                  1:1 voc:4 index:13    from voc_xxx functions
//
// index = 3ffff  = not found
//
#define NOSYM 0x3ffff
#define SYM_INDEX(sym) ((sym) & 0x1fff)
#define VOC_INDEX(sym) (((sym) >> 13) & 0xf)
#define HAS_VOC(sym)   (((sym) >> 17) & 0x1)
#define MAKE_SYM(vi,si) ((((vi) & 0xf)<<13) | ((si) & 0x1fff))
typedef uint18_t symindex_t;

extern symindex_t sym_find_by_namelen(const char* name, int len,
				  const f18_symbol_table_t* symtab);
extern symindex_t sym_find_by_name(const char* name, const f18_symbol_table_t* symtab);
extern symindex_t sym_find_by_value(uint18_t value, const f18_symbol_table_t* symtab);
extern symindex_t sym_find_by_addr(uint18_t addr, const f18_symbol_table_t* symtab);

extern symindex_t sym_insert(char* word, int len, f18_symbol_table_t* symtab);

extern int sym_insert_patch(symindex_t si, int slot, uint18_t addr,
			    f18_symbol_table_t* symtab);

extern void sym_resolve(symindex_t si, uint18_t addr, uint18_t* ram,
			f18_symbol_table_t* symtab);

extern symindex_t sym_add(char* name, int len, uint18_t addr,
			  uint18_t* ram, f18_symbol_table_t* symtab);

extern f18_symbol_table_t* sym_copy_table(f18_symbol_table_t* src);

#endif
