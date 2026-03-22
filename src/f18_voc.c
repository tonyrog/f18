
#include <string.h>
#include "f18_voc.h"

void voc_setup(f18_voc_t voc,
	       f18_symbol_table_t* ram_syms,
	       const f18_symbol_table_t* rom_syms)
{
    voc[RAM_SYMBOLS] = ram_syms;
    voc[ROM_SYMBOLS] = (f18_symbol_table_t*) rom_syms;
    voc[IO_SYMBOLS]  = (f18_symbol_table_t*) &io_symbols;
    // FIXME: how to treat?
    voc[INS_SYMBOLS] = NULL; // (f18_symbol_table_t*) &ins_symbols;
    voc[EOF_SYMBOLS] = NULL;
}

symindex_t voc_find_by_namelen(const char* name, int len,
			       const f18_voc_t voc)
{
    int vi = 0;
    while(voc[vi]) {
	symindex_t si;
	if ((si = sym_find_by_namelen(name, len, voc[vi])) != NOSYM) {
	    return MAKE_SYM(vi,si);
	}
	vi++;
    }
    return NOSYM;
}

symindex_t voc_find_by_name(const char* name, const f18_voc_t voc)
{
    return voc_find_by_namelen(name, strlen(name), voc);
}

symindex_t voc_find_by_value(uint18_t addr, const f18_voc_t voc)
{
    int vi = 0;
    while(voc[vi]) {
	int si;
	if ((si = sym_find_by_value(addr, voc[vi])) != NOSYM) {
	    return MAKE_SYM(vi,si);
	}
	vi++;
    }
    return NOSYM;
}

symindex_t voc_find_by_addr(uint18_t addr, const f18_voc_t voc)
{
    int vi = 0;
    while(voc[vi]) {
	int si;
	if ((si = sym_find_by_addr(addr, voc[vi])) != NOSYM) {
	    return MAKE_SYM(vi,si);
	}
	vi++;
    }
    return NOSYM;
}

int voc_insert(char* word, int len, f18_voc_t voc)
{
    return sym_insert(word, len, voc[RAM_SYMBOLS]);
}

int voc_insert_patch(symindex_t si, int slot, uint18_t addr,
			    f18_voc_t voc)
{
    return sym_insert_patch(SYM_INDEX(si), slot, addr, voc[RAM_SYMBOLS]);
}

symindex_t voc_add(char* name, int len, uint18_t addr,
		   uint18_t* ram, f18_voc_t voc)
{
    return sym_add(name, len, addr, ram, voc[RAM_SYMBOLS]);
}
