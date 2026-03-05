#ifndef __F18_SYM_H__
#define __F18_SYM_H__

#include <stdlib.h>
#include "f18_types.h"

#define SYMLEN(sym) (sym)->name[-1]
#define SYMTYP(sym) (sym)->name[-2]
#define SYMSTR(Name) ((char*)CAT2(SN_,Name)+2)

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

// array of symbols tables stack to search in 
typedef f18_symbol_t* f18_voc_t;

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
extern const f18_symbol_table_t f18_ins_symtab;

extern int find_symbol_by_namelen(const char* name, int len,
				  const f18_symbol_table_t* symtab);
extern int find_symbol_by_name(const char* name, const f18_symbol_table_t* symtab);
extern int find_symbol_by_value(uint18_t value, const f18_symbol_table_t* symtab);
extern int find_symbol_by_addr(uint18_t addr, const f18_symbol_table_t* symtab);

extern int insert_symbol(char* word, int len, f18_symbol_table_t* symtab);

extern int insert_patch(int i, int slot, uint18_t addr,
			f18_symbol_table_t* symtab);

extern void resolve_symbol(int i, int slot, uint18_t addr,  f18_symbol_table_t* symtab);

extern int add_symbol(char* name, int len, int slot, uint18_t addr, f18_symbol_table_t* symtab);

extern f18_symbol_table_t* copy_symbols(f18_symbol_table_t* src);

#endif
