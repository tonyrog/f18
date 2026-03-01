#ifndef __F18_SCAN__

#include "f18.h"

#define TOKEN_ERROR     -1
#define TOKEN_EMPTY     0
#define TOKEN_MNEMONIC1 1
#define TOKEN_MNEMONIC2 2
#define TOKEN_VALUE     3

extern int parse_symbol(char** pptr, uint18_t* valuep,  f18_symbol_t* symtab);
extern int parse_mnemonic(char* word, int n);
extern int parse_ins(char** pptr, uint18_t* insp, uint18_t* dstp,
		     f18_symbol_table_t* symtab);
extern int find_symbol(char* name, f18_symbol_table_t* symtab);
extern int f18_scan_line(int fd,
			 int* line_ptr,
			 uint18_t* addr_ptr,
			 uint18_t* node_ptr,
			 uint18_t* data_ptr,
			 f18_symbol_table_t* symtab);

#endif
