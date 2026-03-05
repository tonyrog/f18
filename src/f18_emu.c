//
//  Simple F18 emulator
//
#include "f18.h"
#include "f18_debug.h"
#include "f18_node.h"
#include "f18_strings.h"

const f18_symbol_t f18_ins[32] = {
    { 0x00,    SYMSTR(SEMI) },      // slot 3
    { 0x01,    SYMSTR(ex) },
    { 0x02,    SYMSTR(jump) },
    { 0x03,    SYMSTR(call)},
    { 0x04,   SYMSTR(unext)},  // slot 3
    { 0x05,   SYMSTR(next)},
    { 0x06,   SYMSTR(if)},
    { 0x07,   SYMSTR(DASH_if)},
    { 0x08,   SYMSTR(AT_p)},     // slot 3
    { 0x09,   SYMSTR(AT_PLUS)},
    { 0x0a,   SYMSTR(AT_b)},
    { 0x0b,   SYMSTR(AT)},
    { 0x0c,   SYMSTR(BANG_p)},     // slot 3
    { 0x0d,   SYMSTR(BANG_PLUS)},
    { 0x0e,   SYMSTR(BANG_b)},
    { 0x0f,   SYMSTR(BANG)},
    { 0x10,   SYMSTR(PLUS_STAR)},     // slot 3 (need wait)
    { 0x11,   SYMSTR(2_STAR)},
    { 0x12,   SYMSTR(2_SLASH)},
    { 0x13,   SYMSTR(inv)},
    { 0x14,   SYMSTR(PLUS)},     // slot 3 (need wait)
    { 0x15,   SYMSTR(and)},
    { 0x16,   SYMSTR(xor)},
    { 0x17,   SYMSTR(drop)},
    { 0x18,   SYMSTR(dup)},   // slot 3
    { 0x19,   SYMSTR(r_GT)},
    { 0x1a,   SYMSTR(over)},
    { 0x1b,   SYMSTR(a)},
    { 0x1c,   SYMSTR(DOT)},     // slot 3
    { 0x1d,   SYMSTR(GT_r)},
    { 0x1e,   SYMSTR(b_BANG)},
    { 0x1f,   SYMSTR(a_BANG)}
};

const f18_symbol_table_t f18_ins_symtab = SYMTAB_INITALIZER(f18_ins);

// NOTE! Addresses are sorted!
const f18_symbol_t iosym[] = {
    { IOREG__D_U, SYMSTR(DASH_d_DASH_u) },
    { IOREG__D__, SYMSTR(DASH_d_DASH_DASH) },
    { IOREG__DLU, SYMSTR(DASH_dlu) },
    { IOREG__DL_, SYMSTR(DASH_dl_DASH) },
    { IOREG_DATA, SYMSTR(data) },
    { IOREG____U, SYMSTR(DASH_DASH_DASH_u) },
    { IOREG_IO,   SYMSTR(io) },
    { IOREG___LU, SYMSTR(DASH_DASH_lu) },
    { IOREG___L_, SYMSTR(DASH_DASH_l_DASH) },
    { IOREG_RD_U, SYMSTR(rd_DASH_u) },
    { IOREG_RD__, SYMSTR(rd_DASH_DASH) },
    { IOREG_RDLU, SYMSTR(rdlu) },
    { IOREG_RDL_, SYMSTR(rdl_DASH) },
    { IOREG_R__U, SYMSTR(r_DASH_DASH_u) },
    { IOREG_R___, SYMSTR(r_DASH_DASH_DASH) },
    { IOREG_R_LU, SYMSTR(r_DASH_lu) },
    { IOREG_R_L_, SYMSTR(r_DASH_l_DASH) },
};

const f18_symbol_table_t io_symtab  = SYMTAB_INITALIZER(iosym);

 #define CHECK_DS_OVERFLOW(np) check_overflow((np),SP, 8,  "data stack"),
 #define CHECK_DS_UNDERFLOW(np) check_underflow((np),SP, 0, "data stack"),

 #define CHECK_RS_OVERFLOW(np) check_overflow((np), RP, 8,  "return stack"),
 #define CHECK_RS_UNDERFLOW(np) check_underflow((np), RP, 0, "return stack"),

 // I do not think it matters in the emulator which way the push and pop goes
 #define PUSH_ds(np,val) (CHECK_DS_OVERFLOW((np)) (np)->ds[SP++ & 0x7] = (val))
 #define POP_ds(np)      (CHECK_DS_UNDERFLOW((np)) (np)->ds[--SP & 0x7])

 #define PUSH_s(np, val) do {		\
	 PUSH_ds((np), S);		\
	 S = T;				\
	 T = (val);			\
     } while(0)

 #define POP_s(np) do {		\
	 T = S;			\
	 S = POP_ds(np);		\
     } while(0)

 #define PUSH_rs(np,val) (CHECK_RS_OVERFLOW((np)) (np)->rs[RP++ & 0x7] = (val))
 #define POP_rs(np)      (CHECK_RS_UNDERFLOW((np)) (np)->rs[--RP & 0x7])

 #define PUSH_r(np, val) do {			\
	 PUSH_rs((np), R);			\
	 R = (val);				\
     } while(0)

 #define POP_r(np) do {			\
	 R = POP_rs(np);			\
     } while(0)

 #define swap18(a,b) do {			\
     uint18_t _swap18_t1 = (a);			\
     (a) = (b);					\
     (b) = _swap18_t1;				\
     } while(0)

 #define p_inc() do {						       \
	 if ((P0 >= RAM_START) && (P0<= RAM_END2))		       \
	     P = ((P0 + 1) & MASK7) | (P & P9);			       \
	 else if ((P0 >= ROM_START) && (P0 <= ROM_END2))		       \
	     P = (ROM_START + (((P0 - ROM_START) + 1) & MASK7)) | (P & P9); \
     } while(0)

 #define a_inc() do {				\
     if ((A0 >= RAM_START) && (A0 <= RAM_END2))	\
	 A = ((A0 + 1) & MASK7);				\
     else if ((A0 >= ROM_START) && (A0 <= ROM_END2))		\
	 A = (ROM_START + (((A0 - ROM_START) + 1) & MASK7));	\
     } while(0)

 #define SWAP_IN(np) do {			\
	 T = np->reg.t;				\
	 S = np->reg.s;				\
	 SP = np->reg.sp;				\
	 R  = np->reg.r;				\
	 RP = np->reg.rp;				\
	 P  = np->reg.p;				\
	 I  = np->reg.i;				\
	 A  = np->reg.a;				\
	 B  = np->reg.b;				\
	 C  = np->reg.c;				\
     } while(0)

 #define SWAP_OUT(np) do {			\
	 np->reg.t = T;				\
	 np->reg.s = S;				\
	 np->reg.sp = SP;				\
	 np->reg.r  = R;				\
	 np->reg.rp = RP;				\
	 np->reg.p  = P;			\
	 np->reg.i = I;				\
	 np->reg.a = A;				\
	 np->reg.b = B;				\
	 np->reg.c = C;				\
     } while(0)

 static uint18_t read_mem(node_t* np, uint18_t addr);


// wrap addresses into regular ROM/RAM/IO addresses
uint18_t normalize_addr(uint18_t addr)
{
    if (addr <= RAM_END2)
	return (addr & MASK6);
    else if (addr <= ROM_END2)
	return ROM_START + ((addr-ROM_START) & MASK6);
    else if (addr <= IOREG_END)
	return addr; // io-address
    return addr & MASK9;
}


 static int check_overflow(node_t* np,int val,int maxval,const char *msg)
 {
     if (val >= maxval) {
	 VERBOSE(np, "%s overflow %u max %u\n", msg, val, maxval);
	 abort();
	 return 1;
     }
     return 0;
 }

 static int check_underflow(node_t* np,int val,int minval,const char *msg)
 {
     if (val <= minval) {
	 VERBOSE(np, "%s underflow %u min %u\n", msg, val, minval);
	 abort();	 
	 return 1;
     }
     return 0;
 }


 static void dump_ram(node_t* np)
 {
     f18_disasm(np->ram, NULL, RAM_START, 64);
 }

 static void dump_rom(node_t* np)
 {
     int i = ID_TO_ROW(np->id);
     int j = ID_TO_COLUMN(np->id);
     f18_disasm(np->rom, SymTabMap[i][j], ROM_START, 64);
 }

 static void dump_ds(node_t* np)
 {
     int i;
     fprintf(stdout, "t=%05x,s=%05x",np->reg.t,np->reg.s);
     for (i=0; i<8; i++)
	 fprintf(stdout, ",%05x", np->ds[(np->reg.sp+i-1)&0x7]);
     fprintf(stdout, "\n");
 }

 static void dump_rs(node_t* np)
 {
     int i;
     fprintf(stdout, "r=%05x",np->reg.r);
     for (i=0; i<8; i++)
	 fprintf(stdout, ",%05x", np->rs[(np->reg.rp+i-1)&0x7]);
     fprintf(stdout, "\n");
 }

 static void dump_reg(node_t* np)
 {
     fprintf(stdout, "t=%05x,a=%05x,b=%03x,c=%x,p=%03x,i=%x,s=%05x,r=%05x\n",
	     np->reg.t, np->reg.a, np->reg.b, np->reg.c,
	     np->reg.p, np->reg.i, np->reg.s, np->reg.r);
 }

 #ifdef DEBUG
 #define DUMP(np) do {					\
     if ((np)->flags & (FLAG_DUMP_BITS)) {		\
	 SWAP_OUT(np);					\
	 fprintf(stdout, "[%03d] DUMP BEGIN\n", (np)->id);	\
	 if ((np)->flags & FLAG_DUMP_REG) dump_reg((np));	\
	 if ((np)->flags & FLAG_DUMP_DS)  dump_ds((np));		\
	 if ((np)->flags & FLAG_DUMP_RS)  dump_rs((np));		\
	 if ((np)->flags & FLAG_DUMP_RAM) dump_ram((np));	\
	 if ((np)->flags & FLAG_DUMP_ROM) dump_rom((np));	\
	 fprintf(stdout, "[%03d] DUMP END\n", (np)->id);		\
     }								\
     } while(0)
 #else
 #define DUMP(np)
 #endif

 static uint18_t read_mem(node_t* np, uint18_t addr)
 {
     uint18_t value;
     if (addr <= RAM_END2) {
	 value = np->ram[addr & MASK6];
	 VERBOSE(np,"read ram[%x] = %x\n", addr, value);
     }
     else if (addr <= ROM_END2) {
	 value = np->rom[(addr-ROM_START) & MASK6];
	 VERBOSE(np,"read rom[%x] = %x\n", addr, value);
     }
     else {
	 value = (*np->read_ioreg)(np, addr & MASK9);
	 VERBOSE(np,"read ioreg[%x] = %x\n", addr & MASK9, value);
     }
     return value;
 }

 static void write_mem(node_t* np, uint18_t addr, uint18_t val)
 {
     if (addr <= RAM_END2) {
	 np->ram[addr & MASK6] = val;
	 VERBOSE(np,"write ram[%04x] = %02x %02x %02x %02x = %x\n",
		 addr & MASK6,
		 (val >> 13) & 0x1f,
		 (val >> 8) & 0x1f,
		 (val >> 3) & 0x1f,
		 (val << 2) & 0x1f,
		 val);
     }
     else if (addr <= ROM_END2) {
	 fprintf(stderr, "warning: try to write in ROM area %x, value=%d\n",
		 addr, val);
     }
     else {
	 VERBOSE(np,"write ioreg[%04x] = %x\n", addr & MASK9, val);
	 (*np->write_ioreg)(np, addr & MASK9, val);
     }
 }

 void f18_emu(node_t* np)
 {
     // registers
     uint18_t  T;           // top of data stack
     uint18_t  S;           // second of data stack
     uint3_t  SP;           // data stack pointer
     uint18_t  R;           // top of return stack
     uint3_t  RP;           // return stack pointer
     uint18_t  I;           // instruction register
     uint18_t  A;           // address register
     uint10_t  P;           // program counter
     uint9_t   B;           // write only register = io after reset
     uint8_t   C;           // carry flag
     // tmp
     uint10_t  P0;           // p_inc
     uint10_t  A0;           // a_inc
     uint32_t II;
     int n;

     SWAP_IN(np);

     DUMP(np);
 next:
     // Debug barrier: pause at instruction boundary if stepping
     if (np->flags & FLAG_DEBUG_ENABLE) {
	 SWAP_OUT(np);  // Save registers before barrier
	 if (debug_pre_instruction(np)) {
	     return;    // Debugger requested exit
	 }
	 SWAP_IN(np);   // Restore registers after barrier
     }

     P0 = P & MASK9;
     p_inc();
     I = read_mem(np, P0);
     if (np->flags & FLAG_TERMINATE)
	 return;
     // Track instruction address and word for debugger display
     if (np->flags & FLAG_DEBUG_ENABLE)
	 debug_set_current_instruction(P0, I);
 restart:
     II = I ^ IMASK;  // decode
     II = II << 2;
     n = 4;
 unext:
     // Slot-level debug barrier (micro-step)
     if (np->flags & FLAG_DEBUG_ENABLE) {
	 SWAP_OUT(np);
	 if (debug_slot_barrier(np, 4 - n)) {  // slot 0-3
	     return;
	 }
	 SWAP_IN(np);
     }

     TRACE(np, "%03x: T=%05x,S=%05x,R=%05x,SP=%d,RP=%d, execute %s\n",
	   P0, T,S,R,SP,RP, f18_ins[(II >> 15) & MASK5].name);
     DELAY(np);

     switch((II >> 15) & MASK5) {
     case INS_RETURN:
	 P = R;
	 POP_r(np);
	 goto next;

     case INS_EXECUTE:
	 swap18(P, R);
	 P &= MASK10;   // maske sure P is 10 bits
	 goto next;

     case INS_PJUMP:
	 goto load_p;

     case INS_PCALL:
	 PUSH_r(np, P);
	 goto load_p;

     case INS_UNEXT:
	 if (R == 0)
	     POP_r(np);
	 else {
	     R--;
	     goto restart;
	 }
	 break;

     case INS_NEXT:
	 if (R == 0)
	     POP_r(np);
	 else {
	     R--;
	     goto load_p;
	 }
	 goto next;

     case INS_IF:  // if   ( x -- x ) jump if x == 0
	 if (T == 0)
	     goto load_p;
	 goto next;

     case INS_MINUS_IF:  // -if  ( x -- x ) jump if x >= 0
	 if (SIGNED18(T) >= 0)
	     goto load_p;
	 goto next;

     case INS_FETCH_P:  //  @p ( -- x ) fetch via P auto-increament
	 P0 = P & MASK9;	
	 p_inc();	
	 PUSH_s(np, read_mem(np, P0));
	 break;

     case INS_FETCH_PLUS:  // @+ ( -- x ) fetch via A auto-increament
	 A0 = A & MASK9;
	 a_inc();	
	 PUSH_s(np, read_mem(np, A0));
	 break;

     case INS_FETCH_B:  // @b ( -- x ) fetch via B
	 PUSH_s(np, read_mem(np, B));
	 break;

     case INS_FETCH:    // @ ( -- x ) fetch via A
	 PUSH_s(np, read_mem(np, A));
	 break;

     case INS_STORE_P:  // !p ( x -- ) store via P auto increment
	 P0 = P & MASK9;
	 p_inc();
	 write_mem(np, P0, T);
	 POP_s(np);
	 break;

     case INS_STORE_PLUS: // !+ ( x -- ) \ write T in [A] pop data stack, inc A
	 A0 = A & MASK9;	
	 a_inc();	
	 write_mem(np, A0, T);
	 POP_s(np);
	 break;

     case INS_STORE_B:  // !b ( x -- ) \ store T into [B], pop data stack
	 write_mem(np, B, T);
	 POP_s(np);
	 break;

     case INS_STORE:    // ! ( x -- ) \ store T info [A], pop data stack
	 write_mem(np, A, T);
	 POP_s(np);
	 break;

     case INS_MULT_STEP: { // t:a * s
	 // FIXME: check that S,T not P9 was changed (use nop otherwise)
	 int32_t t = SIGNED18(T);
	 if (A & 1) { // sign-extend and add s and t
	     t += SIGNED18(S);
	     if (P & P9) {
		 t += C;
		 C = (T >> 18) & 1;
	     }
	 }
	 A = (A >> 1) | ((T & 1) << 17);
	 T = ((T >> 1) | (T & SIGN_BIT)) & MASK18;
	 break;
     }

     case INS_TWO_STAR:   T = (T << 1) & MASK18; break;

     case INS_TWO_SLASH:  T = (T >> 1) | (T & SIGN_BIT); break;

     case INS_INV:        T = (~T) & MASK18; break;

     case INS_PLUS: {  // + or +c  ( x y -- (x+y) ) | ( x y -- (x+y+c) )
	 // FIXME: check that S,T not P9 was changed (use nop otherwise)
	 // expect in slot 3 then the prefetch will guarantee that
	 int32_t t = SIGNED18(T) + SIGNED18(S);
	 if (P & P9) {
	     T += C;
	     C = (T >> 18) & 1;
	 }
	 T = t & MASK18;
	 S = POP_ds(np);
	 break;
     }

     case INS_AND: // ( x y -- ( x & y) )
	 T &= S;
	 S = POP_ds(np);
	 break;

     case INS_XOR:  // ( x y -- ( x ^ y) )
	 T ^= S;
	 S = POP_ds(np);
	 break;

     case INS_DROP:
	 POP_s(np);
	 break;

    case INS_DUP:  // ( x -- x x )
	PUSH_ds(np, S);
	S = T;
	break;

    case INS_FROM_R:  // push R onto data stack and pop return stack
	PUSH_s(np, R);
	POP_r(np);
	break;

    case INS_OVER:  // ( x y -- x y x )
	PUSH_ds(np, S);
	swap18(T, S);
	break;

    case INS_A:  // ( -- A )  push? A onto data stack
	PUSH_s(np, A);
	break;

    case INS_NOP:
	break;

    case INS_TO_R:  // push T onto return stack and pop data stack
	PUSH_r(np, T);
	POP_s(np);
	break;

    case INS_B_STORE:  // b! ( x -- ) store into B
	B = T;
	POP_s(np);
	break;

    case INS_A_STORE:  // a! ( x -- ) store into A
	A = T;
	POP_s(np);
	break;
    }

    // Debug post-instruction hook for tracking
    if (np->flags & FLAG_DEBUG_ENABLE) {
	SWAP_OUT(np);
	debug_post_instruction(np, P0, (II >> 15) & MASK5);
	SWAP_IN(np);
    }

    if (--n == 0)
	goto next;
    II <<= 5;
    goto unext;

load_p:
    // destination addresses are unencoded and must be retrieved
    // from the "original" i register
    switch(n) {
    case 4: P = (P & ~MASK10) | (I & MASK10); break;
    case 3: P = (P & ~MASK8)  | (I & MASK8); break;
    case 2: P = (P & ~MASK3)  | (I & MASK3); break;
    }
    goto next;
}
