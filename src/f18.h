#ifndef __F18_H__
#define __F18_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "f18_types.h"
#include "f18_sym.h"

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
#define INS_INV        0x13   // ~
#define INS_PLUS       0x14   // +
#define INS_AND        0x15   // and
#define INS_XOR        0x16   // xor    ALU 	1.5 	(exclusive or)
#define INS_DROP       0x17   // drop   ALU 	1.5
#define INS_DUP        0x18   // dup 	ALU 	1.5
#define INS_FROM_R     0x19   // pop 	ALU 	1.5
#define INS_OVER       0x1a   // over 	ALU 	1.5
#define INS_A          0x1b   // a 	ALU 	1.5 	(A to T)
#define INS_NOP        0x1c   // . 	ALU 	1.5 	“nop”
#define INS_TO_R       0x1d   // push 	ALU 	1.5 	(from T to R)
#define INS_B_STORE    0x1e   // b! 	ALU 	1.5 	“b-store” (store into B)
#define INS_A_STORE    0x1f   // a! 	ALU 	1.5 	“a-store” (store into A)

#define META_ORG       0x20   // arg = address
#define META_NODE      0x21   // arg = node number
#define META_DEF       0x22   // ':' arg = symbol name
#define META_VALUE     0x80   // value

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

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

#define IOREG__D_U    0x105  // 0000
#define IOREG__D__    0x115  // 0001
#define IOREG__DLU    0x125  // 0010
#define IOREG__DL_    0x135  // 0011
#define IOREG_DATA    0x141  // up-port with no handshake
#define IOREG____U    0x145  // 0100
#define IOREG_IO      0x15D  // i/o control and status
#define IOREG___LU    0x165  // 0110
#define IOREG_LDATA   0x171  // left-port with no handshake
#define IOREG___L_    0x175  // 0111
#define IOREG_RD_U    0x185  // 1000
#define IOREG_RD__    0x195  // 1001
#define IOREG_RDLU    0x1A5  // 1010
#define IOREG_RDL_    0x1B5  // 1011
#define IOREG_R__U    0x1C5  // 1100
#define IOREG_R___    0x1D5  // 1101
#define IOREG_R_LU    0x1E5  // 1110
#define IOREG_R_L_    0x1F5  // 1111

// IO register number coding
#define F18_DIR_BITS      0x105  // Direction pattern
#define F18_DIR_MASK      0x10F  // Direction pattern
#define F18_RIGHT_BIT     0x080  // right when 1
#define F18_DOWN_BIT      0x040  // down when  0
#define F18_LEFT_BIT      0x020  // left when  1
#define F18_UP_BIT        0x010  // up when    0

// port status bits in io register
#define F18_IO_RIGHT_RD   0x10000
#define F18_IO_RIGHT_WR   0x08000
#define F18_IO_DOWN_RD    0x04000
#define F18_IO_DOWN_WR    0x02000
#define F18_IO_LEFT_RD    0x01000
#define F18_IO_LEFT_WR    0x00800
#define F18_IO_UP_RD      0x00400
#define F18_IO_UP_WR      0x00200
#define F18_IO_MASK_WR    0xAA000
#define F18_IO_MASK_RD    0x15400

// GPIO read ior (one bit)
#define F18_IO_PIN17      0x20000
#define F18_IO_PIN5       0x00020
#define F18_IO_PIN3       0x00008
#define F18_IO_PIN1       0x00002

// Wakeup control (iow)
#define F18_IO_WD         0x00800
#define F18_IO_PHAN9      0x00200
#define F18_IO_PHAN7      0x00080

// GPIO write (iow)
#define F18_IO_CTRL_PIN17 0x30000
#define F18_IO_CTRL_PIN5  0x00030
#define F18_IO_CTRL_PIN3  0x0000C
#define F18_IO_CTRL_PIN1  0x00003

#define GPIO  4
#define RIGHT 3
#define DOWN  2
#define LEFT  1
#define UP    0
#define DIR_BIT(n)     (1 << (n))            // RDLU => 1,2,4,8,16
#define F18_DIR_BIT(n) (DIR_BIT((n)) << 8)   // RDLU => F18_<dir>_BIT
#define F18_IO_DIR_WR(n) ((1<<(2*(n)))<<9)   // RDLU => F18_IO_<dir>_WR
#define F18_IO_DIR_RD(n) ((1<<(2*(n)+1))<<9) // RDLU => F18_IO_<dir>_RD

#define P9          0x200    // P9 bit
#define SIGN_BIT    0x20000  // 18 bit sign bit
#define MASK3       0x7
#define MASK5       0x1f
#define MASK6       0x3f
#define MASK7       0x7f
#define MASK8       0xff
#define MASK9       0x1ff
#define MASK10      0x3ff
#define MASK13      0x1fff
#define MASK18      0x3ffff
#define MASK19      0x7ffff
#define IMASK       0x15555  // exeucte/compiler mask

#define SIGNED18(v)  (((int32_t)((v)<<14))>>14)

#define FLAG_VERBOSE      0x00001
#define FLAG_TRACE        0x00002
#define FLAG_TERMINATE    0x00004
#define FLAG_DUMP_ROM     0x00008
#define FLAG_DUMP_REG     0x00010
#define FLAG_DUMP_RAM     0x00020
#define FLAG_DUMP_RS      0x00040
#define FLAG_DUMP_DS      0x00080
#define FLAG_DUMP_BITS    0x000F8
#define FLAG_RD_BIN_RIGHT 0x00800
#define FLAG_RD_BIN_DOWN  0x00400
#define FLAG_RD_BIN_LEFT  0x00200
#define FLAG_RD_BIN_UP    0x00100
#define FLAG_WR_BIN_RIGHT 0x08000
#define FLAG_WR_BIN_LEFT  0x04000
#define FLAG_WR_BIN_DOWN  0x02000
#define FLAG_WR_BIN_UP    0x01000

#define MAKE_ID(i,j)     ((i)*100+(j))
#define ID_TO_ROW(id)    ((id)/100)
#define ID_TO_COLUMN(id) ((id)%100)

// Physical directions
#define EAST  0
#define NORTH 1
#define WEST  2
#define SOUTH 3

// turn CCW (+1)
//
//     N
//   W   E
//     S
//

#define TURN90(x)  (((x)+1) & 3)
#define TURN180(x) (((x)+2) & 3)
#define TURN270(x) (((x)+3) & 3)

// portmap from direction
#define east_io(i,j)  (((j)&1) ? IOREG___L_ : IOREG_R___)
#define north_io(i,j) (((i)&1) ? IOREG____U : IOREG__D__)
#define west_io(i,j)  (((j)&1) ? IOREG_R___ : IOREG___L_)
#define south_io(i,j) (((i)&1) ? IOREG__D__ : IOREG____U)

#define east_bit(i,j)  (((j)&1) ? DIR_BIT(LEFT) : DIR_BIT(RIGHT))
#define north_bit(i,j) (((i)&1) ? DIR_BIT(UP) : DIR_BIT(DOWN))
#define west_bit(i,j)  (((j)&1) ? DIR_BIT(RIGHT) : DIR_BIT(LEFT))
#define south_bit(i,j) (((i)&1) ? DIR_BIT(UP) : DIR_BIT(DOWN))

#define east(id)  MAKE_ID(ID_TO_ROW(id),ID_TO_COLUMN(id)+1)
#define north(id) MAKE_ID(ID_TO_ROW(id)-1,ID_TO_COLUMN(id))
#define west(id)  MAKE_ID(ID_TO_ROW(id),ID_TO_COLUMN(id)-1)
#define south(id) MAKE_ID(ID_TO_ROW(id)+1,ID_TO_COLUMN(id))

typedef struct {
    uint18_t t;            // top of data stack
    uint18_t s;            // second of data stack
    uint3_t  sp;           // data stack pointer
    uint18_t r;            // return top of return stack
    uint3_t  rp;           // return stack pointer
    uint18_t  i;           // instruction register
    uint18_t  a;           // address register
    uint9_t   b;           // write only register = io after reset    
    uint10_t  p;           // program counter
    uint8_t   c;           // carry flag    
} f18_regs_t;

typedef enum {
    basic,
    serdes_boot,
    sdram_data,
    sdram_control,
    sdram_addr,
    eForth_bitsy,
    eForth_stack,
    sdram_mux,
    sdram_idle,
    analog,    
    one_wire,
    sync_boot,
    spi_boot,
    async_boot
} f18_rom_type_t;

typedef enum {
    none = 0,
    serdes,
    gpio_x1,
    gpio_x2,
    gpio_x4,
    analog_pin,
    parallel_bus,
} f18_io_type_t;

typedef struct {
    f18_rom_type_t type;
    char* name;
    char* vers;
    const uint18_t* addr;
    size_t size;
} f18_rom_t;
    
typedef struct {
    f18_rom_type_t rom;
    f18_io_type_t  io_type;
    uint9_t comm;           // com ports present
    uint9_t io_addr;
    uint9_t reset;          // reset address "cold" | ioreg
    // io_type      n
    // gpio_x1:     1   { 20 }
    // gpio_x2:     2   { 14, 15 }
    // gpio_x4:     4   { 80, 81, 84, 85 }
    // analog:      2   { 76, 77 }          -- adc/dac
    // async_booot: 2   { 78, 69 }          -- rx/tx
    // serdes_boot: 2   { 26, 27 }          -- clk/data
    // 
    uint8_t io_pin[4];      // upto 4 pins io_type=gpio/analog/serdes ...
    uint18_t trigger[4];    // analog trigger for ...
} f18_config_t;
//
// sizeof(node_t) = 656 bytes (update me now and then)
// total ram usage for threads 93888 bytes.
// with page size of 4096*144 =  589824 bytes
//
typedef struct _node_t {
    uint18_t       ram[64];
    f18_rom_type_t rom_type;
    const uint18_t* rom;
    f18_symbol_table_t* symtab; // loaded symbols, if present
    uint18_t       ior;     // io status register read
    uint18_t       iow;     // io status register write
    uint18_t       id;      // id 000 - 717 (decimal)
    useconds_t delay;       // delay between instructions
    uint18_t flags;         // flags,debug,trace...
    uint9_t io_addr;        // io_addr for gpio or 0 if not used
    uint5_t wins;           // instruction during wait FETCH/STORE (NOP)
    
    // System dependent functions
    void* user;  // user data pointer
    uint18_t (*read_ioreg)(struct _node_t* np, uint18_t reg);
    void     (*write_ioreg)(struct _node_t* np, uint18_t reg, uint18_t val);

    f18_regs_t reg;        // saved registers    
    uint18_t ds[8];        // data stack
    uint18_t rs[8];        // return stack

    // trace buffer
    char buf[32];
} node_t;


#ifdef DEBUG
#define VERBOSE(np,fmt,...) do {			\
	if (((node_t*)(np))->flags & FLAG_VERBOSE)			\
	    fprintf(stderr,"[%03d]: "fmt,((node_t*)(np))->id,__VA_ARGS__); \
    } while(0)
#define TRACE(np,fmt, ...) do {				\
	if (((node_t*)(np))->flags & FLAG_TRACE)			\
	    fprintf(stderr, "[%03d]: "fmt,((node_t*)(np))->id,__VA_ARGS__); \
    } while(0)
#define DELAY(np) do {					\
	if (((node_t*)(np))->delay)			\
	    usleep(((node_t*)(np))->delay);		\
    } while(0)
#else
#define VERBOSE(np,fmt, ...)
#define TRACE(np,fmt, ...)
#define DELAY(np)
#endif

#define ERROR(np,fmt,...) do {			\
	if (((node_t*)(np))->flags & FLAG_VERBOSE)			\
	    fprintf(stderr,"[%03d]: "fmt,((node_t*)(np))->id,__VA_ARGS__); \
    } while(0)


extern void f18_emu(node_t* p);
extern int f18_disasm_instruction(uint18_t addr, uint18_t I,
				  const f18_symbol_table_t* symtab,
				  char* ptr, size_t maxlen);
extern void f18_disasm(const uint18_t* insp, const f18_symbol_table_t* symtab,
		       uint18_t addr, size_t n);


// System thread state tracking
extern void sys_thread_started(void);
extern void sys_thread_terminated(void);
extern void sys_enter_blocked_port(void);
extern void sys_leave_blocked_port(void);
extern void sys_enter_blocked_ext(void);
extern void sys_leave_blocked_ext(void);

// f8_rom_type_t => f18_rom_t
extern const f18_rom_t RomMap[];
// node-id => f8_rom_type_t
extern const f18_rom_type_t RomTypeMap[8][18];
// node-id => f18_symbol_t[]
// extern const f18_symbol_t* SymMap[8][18];
extern const f18_symbol_table_t* SymTabMap[8][18];
// node-id => f18_config_t[]
extern const f18_config_t ConfigMap[8][18];
// extern const f18_symbol_t iosym[];
extern const f18_symbol_table_t io_symtab;

// Convert ioreg into "dir" bits

static inline uint9_t dirbits(int i, int j, uint9_t ioreg)
{
    uint9_t dir = ((ioreg>>4) ^ 0x5) & 0xf;
    if (!(i & 1))
	dir = (dir & 0xA) | ((dir & 1) << 2) | ((dir & 4) >> 2);
    if ((j & 1))
	dir = (dir & 0x5) | ((dir & 2) << 2) | ((dir & 8) >> 2);
    return dir;
}

#endif
