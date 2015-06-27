//
//  Simple F18 emulator
//
#include "f18.h"

char* f18_ins_name[] = {
    ";", "ex", "jump", "call", "unext", "next", "if", "-if",
    "@p", "@+", "@b", "@", "!p", "!+", "!b", "!",
    "+*","2*","2/","-","+","and","or", "drop",
    "dup","pop","over","a",".","push","b!","a!"
};

// I do not think it matters in the emulator which way the posh and pop goes
#define PUSH_ds(np,val) (np)->ds[(np)->sp++ & 0x7] = (val)
#define POP_ds(np)      (np)->ds[--(np)->sp & 0x7]

#define PUSH_s(np, val) do { 			\
    PUSH_ds((np), (np)->s);			\
    (np)->s = (np)->t;				\
    (np)->t = (val);				\
    } while(0)

#define POP_s(np) do {				\
    (np)->t = (np)->s;				\
    (np)->s = POP_ds(np);			\
    } while(0)

#define PUSH_rs(np,val) (np)->rs[(np)->rp++ & 0x7] = (val)
#define POP_rs(np)      (np)->rs[--(np)->rp & 0x7]

#define PUSH_r(np, val) do { 			\
    PUSH_rs((np), (np)->r);			\
    (np)->r = (val);				\
    } while(0)

#define POP_r(np) do {				\
    (np)->r = POP_rs(np);			\
    } while(0)

#define swap18(a,b) do {			\
    uint18_t _swap18_t1 = (a);			\
    (a) = (b);					\
    (b) = _swap18_t1;				\
    } while(0)

#ifdef DEBUG
#define DUMP(np) do {					\
    if ((np)->flags & FLAG_DUMP_REG) dump_reg((np));	\
    if ((np)->flags & FLAG_DUMP_DS)  dump_ds((np));	\
    if ((np)->flags & FLAG_DUMP_RS)  dump_rs((np));	\
    if ((np)->flags & FLAG_DUMP_RAM) dump_ram((np));	\
    } while(0)
#else
#define DUMP(np)
#endif



static void dump_ram(node_t* np)
{
    int i;
    for (i = RAM_START; i<=RAM_END; i++)
	fprintf(stdout, "ram[%d]=%05x\n", i, np->ram[i]);
}

static void dump_ds(node_t* np)
{
    int i;
    fprintf(stdout, "t=%05x,s=%05x",np->t,np->s);
    for (i=0; i<8; i++)
	fprintf(stdout, ",%05x", np->ds[(np->sp+i-1)&0x7]);
    fprintf(stdout, "\n");
}


static void dump_rs(node_t* np)
{
    int i;
    fprintf(stdout, "r=%05x",np->r);
    for (i=0; i<8; i++)
	fprintf(stdout, ",%05x", np->rs[(np->rp+i-1)&0x7]);
    fprintf(stdout, "\n");
}

static void dump_reg(node_t* np)
{
    fprintf(stdout, "t=%05x,a=%05x,b=%03x,c=%x,p=%x,i=%x,s=%05x,r=%05x\n", 
	    np->t, np->a, np->b, np->c, np->p, np->i, np->s, np->r);
}

// read value of P return the current value and
// perform auto increment if needed.

static uint9_t p_auto(node_t* np)
{
    uint9_t p = np->p & MASK9;  // strip P(9)
    if ((p >= RAM_START) && (p <= RAM_END2))
	np->p = ((p + 1) & 0x7f) | (np->p & P9);
    else if ((p >= ROM_START) && (p <= ROM_END2))
	np->p = (ROM_START + (((p - ROM_START) + 1) & 0x7f)) | (np->p & P9);
    return p;
}

static uint9_t a_auto(node_t* np)
{
    uint9_t a = np->a & MASK9;

    if ((a >= RAM_START) && (a <= RAM_END2))
	np->a = ((a + 1) & 0x7f);
    else if ((a >= ROM_START) && (a <= ROM_END2))
	np->a = (ROM_START + (((a - ROM_START) + 1) & 0x7f));
    return a;
}

static uint18_t read_mem(node_t* np, uint18_t addr)
{
    uint18_t value;
    if (addr <= RAM_END2) {
	value = np->ram[addr & 0x3f];
	VERBOSE(np,"read ram[%x] = %x\n", addr, value);
    }
    else if (addr <= ROM_END2) {
	value = np->rom[(addr-ROM_START) & 0x3f];
	VERBOSE(np,"read rom[%x] = %x\n", addr, value);
    }
    else {
	value = (*np->read_ioreg)(np, addr);
	VERBOSE(np,"read ioreg[%x] = %x\n", addr, value);
    }
    return value;
}

static void write_mem(node_t* np, uint18_t addr, uint18_t val)
{
    if (addr <= RAM_END2) {
	np->ram[addr & 0x3f] = val;
	VERBOSE(np,"write ram[%04x] = %02x %02x %02x %02x = %x\n",
		addr & 0x3f,
		(val >> 13) & 0x1f,
		(val >> 8) & 0x1f,
		(val >> 3) & 0x1f,
		(val << 2) & 0x1f,
		val);
    }
    else if (addr <= ROM_END2) {
	fprintf(stderr, "warning: try to write in ROM area %x, value=%d\n",
		addr, val);
	// np->rom[(addr-ROM_START) & 0x3f] = val;
    }
    else {
	(*np->write_ioreg)(np, addr, val);
	VERBOSE(np,"write ioreg[%04x] = %x\n", addr, val);
    }
}

void f18_emu(node_t* np)
{
    uint18_t I;
    uint32_t II;
    int n;
    uint5_t ins;

next:
    DUMP(np);
    np->i = read_mem(np, p_auto(np));
    if (np->flags & FLAG_TERMINATE)
	return;
    I = np->i ^ IMASK; // "decode" instruction (why?)

restart:
    II = I << 2;
    n = 4;
unext:
    DUMP(np);
    ins = (II >> 15) & 0x1f;
    TRACE(np, "execute %s\n", f18_ins_name[ins]);
    DELAY(np);

    switch(ins) {
    case INS_RETURN:
	np->p = np->r;
	POP_r(np);
	goto next;

    case INS_EXECUTE:
	swap18(np->p, np->r);
	np->p &= MASK10;   // maske sure P is 10 bits
	goto next;

    case INS_PJUMP:
	goto load_p;

    case INS_PCALL:
	PUSH_r(np, np->p);
	goto load_p;

    case INS_UNEXT:
	if (np->r == 0)
	    POP_r(np);
	else {
	    np->r--;
	    goto restart;
	}
	break;

    case INS_NEXT:
	if (np->r == 0)
	    POP_r(np);
	else {
	    np->r--;
	    goto load_p;
	}
	goto next;

    case INS_IF:  // if   ( x -- x ) jump if x == 0
	if (np->t == 0)
	    goto load_p;
	goto next;

    case INS_MINUS_IF:  // -if  ( x -- x ) jump if x >= 0
	if (SIGNED18(np->t) >= 0)
	    goto load_p;
	goto next;

    case INS_FETCH_P:  //  @p ( -- x ) fetch via P auto-increament
	PUSH_s(np, read_mem(np, p_auto(np)));
	break;

    case INS_FETCH_PLUS:  // @+ ( -- x ) fetch via A auto-increament
	PUSH_s(np, read_mem(np, a_auto(np)));
	break;

    case INS_FETCH_B:  // @b ( -- x ) fetch via B
	PUSH_s(np, read_mem(np, np->b));
	break;

    case INS_FETCH:    // @ ( -- x ) fetch via A
	PUSH_s(np, read_mem(np, np->a));
	break;

    case INS_STORE_P:  // !p ( x -- ) store via P auto increment
	write_mem(np, p_auto(np), np->t);
	POP_s(np);
	break;

    case INS_STORE_PLUS: // !+ ( x -- ) \ write T in [A] pop data stack, inc A
	write_mem(np, a_auto(np), np->t);
	POP_s(np);
	break;

    case INS_STORE_B:  // !b ( x -- ) \ store T into [B], pop data stack
	write_mem(np, np->b, np->t);
	POP_s(np);
	break;

    case INS_STORE:    // ! ( x -- ) \ store T info [A], pop data stack
	write_mem(np, np->a, np->t);
	POP_s(np);
	break;

    case INS_MULT_STEP: { // t:a * s
	int32_t t = SIGNED18(np->t);
	if (np->a & 1) { // sign-extend and add s and t
	    t += SIGNED18(np->s);
	    if (np->p & P9) {
		t += np->c;
		np->c = (t >> 18) & 1;
	    }
	}
	np->a = (np->a >> 1) | ((t & 1) << 17);
	np->t = ((t >> 1) | (t & SIGN_BIT)) & MASK18;
	break;
    }

    case INS_TWO_STAR:   np->t = (np->t << 1) & MASK18; break;

    case INS_TWO_SLASH:  np->t = (np->t >> 1) | (np->t & SIGN_BIT); break;

    case INS_NOT:        np->t = (~np->t) & MASK18; break;

    case INS_PLUS: {  // + or +c  ( x y -- (x+y) ) | ( x y -- (x+y+c) )
	int32_t t = SIGNED18(np->t) + SIGNED18(np->s);
	if (np->p & P9) {
	    t += np->c;
	    np->c = (t >> 18) & 1;
	}
	np->t = t & MASK18;
	np->s = POP_ds(np);
	break;
    }

    case INS_AND: // ( x y -- ( x & y) )
	np->t &= np->s;
	np->s = POP_ds(np);
	break;

    case INS_OR:  // ( x y -- ( x ^ y) )  why not named XOR????
	np->t ^= np->s;
	np->s = POP_ds(np);
	break;

    case INS_DROP:
	np->t = np->s;
	np->s = POP_ds(np);
	break;

    case INS_DUP:  // ( x -- x x )
	PUSH_ds(np, np->s);
	np->s = np->t;
	break;

    case INS_POP:  // push R onto data stack and pop return stack
	PUSH_s(np, np->r);
	np->r = POP_rs(np);
	break;

    case INS_OVER:  // ( x y -- x y x )
	PUSH_ds(np, np->s);
	swap18(np->t, np->s);
	break;

    case INS_A:  // ( -- A )  push? A onto data stack
	PUSH_s(np, np->a);
	break;

    case INS_NOP:
	break;

    case INS_PUSH:  // push T onto return stack and pop data stack
	PUSH_r(np, np->t);
	POP_s(np);
	break;

    case INS_B_STORE:  // b! ( x -- ) store into B
	np->b = np->t;
	POP_s(np);
	break;

    case INS_A_STORE:  // a! ( x -- ) store into A
	np->a = np->t;
	POP_s(np);
	break;
    }
    if (--n == 0)
	goto next;
    II <<= 5;
    goto unext;

load_p:
    // destination addresses are unencoded and must are retrieved
    // from the "original" i register
    switch(n) {
    case 4: np->p = np->i & MASK10; break;
    case 3: np->p = np->i & MASK8; break;
    case 2: np->p = np->i & MASK3; break;
    }
    goto next;
}
