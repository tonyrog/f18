#ifndef __F18_SCAN__

#define TOKEN_ERROR     -1
#define TOKEN_EMPTY     0
#define TOKEN_MNEMONIC1 1
#define TOKEN_MNEMONIC2 2
#define TOKEN_VALUE     3

extern char* f18_ins_name[];

extern int parse_symbol(char** pptr, uint18_t* valuep);
extern int parse_mnemonic(char* word, int n);
extern int parse_ins(char** pptr, uint18_t* insp, uint18_t* dstp);
extern int scan_line(int fd, uint18_t* rdata);

#endif
