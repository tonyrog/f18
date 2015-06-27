#ifndef __F18_H__
#define __F18_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define INS_RETURN     0x00   // ;
#define INS_EXECUTE    0x01   // ex
#define INS_PJUMP      0x02   // jump <10-bit>
#define INS_PCALL      0x03   // call <10-bit>
#define INS_UNEXT      0x04   // micronext
#define INS_NEXT       0x05   // next <10-bit>
#define INS_IF         0x06   //  if  <10-bit>
#define INS_MINUS_IF   0x07   // -if  <10-bit>
#define INS_FETCH_P    0x08   // @p
#define INS_FETCH_PLUS 0x09   // @+
#define INS_FETCH_B    0x0a   // @b
#define INS_FETCH      0x0b   // @
#define INS_STORE_P    0x0c   // !p
#define INS_STORE_PLUS 0x0d   // !+
#define INS_STORE_B    0x0e   // !b
#define INS_STORE      0x0f   // !
#define INS_MULT_STEP  0x10   // +*
#define INS_TWO_STAR   0x11   // 2*
#define INS_TWO_SLASH  0x12   // 2/
#define INS_NOT        0x13   // -
#define INS_PLUS       0x14   // +
#define INS_AND        0x15   // and
#define INS_OR         0x16   // or 	ALU 	1.5 	(exclusive or)
#define INS_DROP       0x17   // drop ALU 	1.5
#define INS_DUP        0x18   // dup 	ALU 	1.5
#define INS_POP        0x19   // pop 	ALU 	1.5
#define INS_OVER       0x1a   // over 	ALU 	1.5
#define INS_A          0x1b   // a 	ALU 	1.5 	(A to T)
#define INS_NOP        0x1c   // . 	ALU 	1.5 	“nop”
#define INS_PUSH       0x1d   // push 	ALU 	1.5 	(from T to R)
#define INS_B_STORE    0x1e   // b! 	ALU 	1.5 	“b-store” (store into B)
#define INS_A_STORE    0x1f   // a! 	ALU 	1.5 	“a-store” (store into A)


typedef uint32_t uint18_t;  // 18 bits packed into 32 bits
typedef uint16_t uint9_t;   // 9 bits packed into 16 bits
typedef uint16_t uint10_t;  // 10 bits packed into 16 bits
typedef uint8_t  uint5_t;   // 5 bits packed into 8 bits
typedef uint8_t  uint3_t;   // 3 bits packed into 8 bits

// address layout in binary
// 000000000 - 000111111    RAM
// 001000000 - 001111111    RAM*  (repeat)
// 010000000 - 010111111    ROM
// 011000000 - 011111111    ROM*  (repeat)
// 100000000 - 111111111    IOREG

#define RAM_START   0x000
#define RAM_END     0x03f
#define RAM_END2    0x07f
#define ROM_START   0x080
#define ROM_END     0x0BF
#define ROM_END2    0x0FF
#define IOREG_START 0x100
#define IOREG_END   0x1FF

#define IOREG_STDIN   0x100  // test/debug
#define IOREG_STDOUT  0x101  // test/debug
#define IOREG_STDIO   0x102  // test/debug
#define IOREG_TTY     0x103  // test/debug

#define IOREG_IO      0x15D  // i/o control and status
#define IOREG_DATA    0x141
#define IOREG____U    0x145  // up
#define IOREG___L_    0x175  // left
#define IOREG___LU    0x165  // left or up
#define IOREG__D__    0x115  // down
#define IOREG__D_U    0x105
#define IOREG__DL_    0x135
#define IOREG__DLU    0x125
#define IOREG_R___    0x1D5  // right
#define IOREG_R__U    0x1C5
#define IOREG_R_L_    0x1F5
#define IOREG_R_LU    0x1E5
#define IOREG_RD__    0x195
#define IOREG_RD_U    0x185
#define IOREG_RDL_    0x1B5
#define IOREG_RDLU    0x1A5

// IO register number coding
#define F18_DIR_BITS      0x105  // Direction pattern
#define F18_DIR_MASK      0x10F  // Direction pattern
#define F18_RIGHT_BIT     0x080  // right when 1
#define F18_DOWN_BIT      0x040  // down when 0
#define F18_LEFT_BIT      0x020  // left when 1
#define F18_UP_BIT        0x010  // up when 0

#define F18_IO_PIN17      0x20000
// port status bits in io register
#define F18_IO_RIGHT_RD   0x10000
#define F18_IO_RIGHT_WR   0x08000
#define F18_IO_DOWN_RD    0x04000
#define F18_IO_DOWN_WR    0x02000
#define F18_IO_LEFT_RD    0x01000
#define F18_IO_LEFT_WR    0x00800
#define F18_IO_UP_RD      0x00400
#define F18_IO_UP_WR      0x00200
// ...
#define F18_IO_PIN5       0x00020
#define F18_IO_PIN3       0x00008
#define F18_IO_PIN1       0x00002

#define P9          0x200    // P9 bit
#define SIGN_BIT    0x20000  // 18 bit sign bit
#define MASK3       0x7
#define MASK8       0xff
#define MASK9       0x1ff
#define MASK10      0x3ff
#define MASK18      0x3ffff
#define MASK19      0x7ffff
#define IMASK       0x15555  // exeucte/compiler mask

#define SIGNED18(v)  (((int32_t)((v)<<14))>>14)

#define FLAG_VERBOSE      0x00001
#define FLAG_TRACE        0x00002
#define FLAG_TERMINATE    0x00004
#define FLAG_DUMP_REG     0x00010
#define FLAG_DUMP_RAM     0x00020
#define FLAG_DUMP_RS      0x00040
#define FLAG_DUMP_DS      0x00080
#define FLAG_RD_BIN_RIGHT 0x00800
#define FLAG_RD_BIN_DOWN  0x00400
#define FLAG_RD_BIN_LEFT  0x00200
#define FLAG_RD_BIN_UP    0x00100
#define FLAG_WR_BIN_RIGHT 0x08000
#define FLAG_WR_BIN_LEFT  0x04000
#define FLAG_WR_BIN_DOWN  0x02000
#define FLAG_WR_BIN_UP    0x01000

//
// sizeof(node_t) = 652 bytes (update me now and then)
// total ram usage for threads 93888 bytes.
// with page size of 4096*144 =  589824 bytes
//
typedef struct _node_t {
    uint18_t       ram[64];
    const uint18_t rom[64];
    uint18_t       io;       // io status register (read)

    useconds_t delay;       // delay between instructions
    uint18_t flags;         // flags,debug,trace...

    // FILE/PIPE/SOCKETS
    int up_fd;
    int left_fd;
    int down_fd;
    int right_fd;

    int tty_fd;
    int stdin_fd;
    int stdout_fd;

    // System dependent functions
    uint18_t (*read_ioreg)(struct _node_t* np, uint18_t reg);
    void     (*write_ioreg)(struct _node_t* np, uint18_t reg, uint18_t val);

    uint18_t t;            // top of data stack
    uint18_t s;            // second of data stack
    uint18_t ds[8];        // data stack
    uint3_t  sp;           // data stack pointer

    uint18_t r;            // return top of return stack
    uint18_t rs[8];        // return stack
    uint3_t  rp;           // return stack pointer

    uint18_t  i;           // instruction register
    uint10_t  p;           // program counter
    uint18_t  a;           // address register
    uint9_t   b;           //
    uint8_t   c;           // carry flag
} node_t;


#ifdef DEBUG
#define VERBOSE(np,fmt,...) do {			\
	if ((np)->flags & FLAG_VERBOSE)			\
	    fprintf(stdout, fmt, __VA_ARGS__);		\
    } while(0)
#define TRACE(np,fmt, ...) do {				\
	if ((np)->flags & FLAG_TRACE)				\
	    fprintf(stdout, fmt, __VA_ARGS__);		\
    } while(0)
#define DELAY(np) do {				\
	if ((np)->delay)			\
	    usleep((np)->delay);		\
    } while(0)
#else
#define VERBOSE(np,fmt, ...)
#define TRACE(np,fmt, ...)
#define DELAY(np)
#endif

extern char* f18_ins_name[];

extern void     f18_emu(node_t* p);

#endif
