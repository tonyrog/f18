#ifndef __F18_ASM_H__
#define __F18_ASM_H__

#include "f18.h"

#define TOKEN_ERROR     -1
#define TOKEN_EMPTY     0
#define TOKEN_MNEMONIC1 1
#define TOKEN_MNEMONIC2 2
#define TOKEN_VALUE     3

extern int parse_symbol(char** pptr, uint18_t* valuep,  f18_symbol_t* symtab);
extern int parse_mnemonic(char* word, int n);
extern int parse_ins(char** pptr, uint18_t* insp,
		     int slot, uint18_t addr,
		     uint18_t* dstp,
		     f18_symbol_table_t* symtab);

extern f18_symbol_table_t* copy_symbols(f18_symbol_table_t* symtab);
extern int f18_asm_line(int fd,
			int* line_ptr,
			char* line_buf,
			size_t line_buf_size,
			uint18_t* addr_ptr,
			uint18_t* node_ptr,
			uint18_t* data_ptr,
			f18_symbol_table_t* symtab);

#endif
