// Generated with f18_rom:format_roms().
#include "f18.h"

#include "f18_strings.h"  // only once!!

const f18_symbol_t basic_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(poly)},
 { 0x0b0, SYMSTR(STAR_DOT_17)},
 { 0x0b7, SYMSTR(STAR_DOT)},
 { 0x0bc, SYMSTR(taps)},
 { 0x0c4, SYMSTR(interp)},
 { 0x0ce, SYMSTR(triangle)},
 { 0x0d3, SYMSTR(clc)},
 { 0x2d5, SYMSTR(DASH_DASH_u_SLASH_mod)},
 { 0x2d6, SYMSTR(DASH_u_SLASH_mod)},
 { 0x3ff, (char*) 0},
};
static const uint18_t basic_rom[] = {158394,60592,159933,10837,149690,133554,201130,256027,26288,208043,148402,217530,240816,87381,20329,65536,240117,239280,210261,151011,180717,79571,179642,17,150002,191920,151216,112350,240760,150005,132066,159960,150005,158234,195708,243624,159679,145057,39714,31602,141485,71093,158394,28338,194231,158232,143532,193877,179643,16,148402,217545,144054,239325,238797,79024,140395,239322,210261,238677,158394,32442,141490,79024};

const f18_symbol_t serdes_boot_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cold)},
 { 0x0aa, SYMSTR(poly)},
 { 0x0b0, SYMSTR(STAR_DOT_17)},
 { 0x0b7, SYMSTR(STAR_DOT)},
 { 0x0bc, SYMSTR(taps)},
 { 0x0c4, SYMSTR(interp)},
 { 0x0ce, SYMSTR(triangle)},
 { 0x0d3, SYMSTR(clc)},
 { 0x2d5, SYMSTR(DASH_DASH_u_SLASH_mod)},
 { 0x2d6, SYMSTR(DASH_u_SLASH_mod)},
 { 0x3ff, (char*) 0},
};
static const uint18_t serdes_boot_rom[] = {158394,60592,159933,10837,149690,133554,201130,256027,26288,208043,148402,217530,240816,87381,20329,65536,240117,239280,210261,151011,180717,79571,179642,17,150002,191920,151216,112350,240760,150005,132066,159960,150005,158234,195708,243624,159679,145057,39714,31602,141485,71093,18963,12609,262142,43442,79269,70826,179643,16,148402,217545,144054,239325,238797,79024,140395,239322,210261,238677,158394,32442,141490,79024};

const f18_symbol_t sdram_data_sym[] = {
 { 0x00b, SYMSTR(db_BANG)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(db_AT)},
 { 0x0ad, SYMSTR(inpt)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sdram_data_rom[] = {79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71093,13653,23338,87381,23381,83285,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017};

const f18_symbol_t sdram_control_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sdram_control_rom[] = {79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71093,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017};

const f18_symbol_t sdram_addr_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cmd)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sdram_addr_rom[] = {79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71093,23474,251221,15170,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017};

const f18_symbol_t eForth_bitsy_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(rp_DASH_DASH)},
 { 0x0ac, SYMSTR(APOS_else)},
 { 0x0ac, SYMSTR(bs_AT)},
 { 0x0b0, SYMSTR(rp_AT)},
 { 0x0b1, SYMSTR(pshbs)},
 { 0x0b3, SYMSTR(APOS_r_AT)},
 { 0x0b4, SYMSTR(AT_w)},
 { 0x0b6, SYMSTR(rfrom)},
 { 0x0b9, SYMSTR(popbs)},
 { 0x0bb, SYMSTR(pshr)},
 { 0x0bf, SYMSTR(ip_PLUS_PLUS)},
 { 0x0bf, SYMSTR(rp_PLUS_PLUS)},
 { 0x0c1, SYMSTR(tor)},
 { 0x0c4, SYMSTR(rp_BANG)},
 { 0x0c7, SYMSTR(APOS_con)},
 { 0x0c8, SYMSTR(APOS_var)},
 { 0x0c9, SYMSTR(APOS_exit)},
 { 0x0ce, SYMSTR(bitsy)},
 { 0x0d0, SYMSTR(xxt)},
 { 0x0d3, SYMSTR(APOS_ex)},
 { 0x0d5, SYMSTR(APOS_lit)},
 { 0x0d8, SYMSTR(APOS_if)},
 { 0x3ff, (char*) 0},
};
static const uint18_t eForth_bitsy_rom[] = {1,194233,79035,161109,194233,79039,161109,79020,153265,243370,153516,148589,244550,239709,153260,194239,160459,194235,161742,79033,194512,153260,194239,161713,79033,103643,243647,243628,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71077,18933,262143,23330,22186,23301,182710,136874,23333,22206,136874,79020,70833,136874,135090,70836,23301,54968,23331,18871,40226,79028,18933};

const f18_symbol_t eForth_stack_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(APOS_AT)},
 { 0x0aa, SYMSTR(x_AT)},
 { 0x0aa, SYMSTR(APOS_c_AT)},
 { 0x0ac, SYMSTR(cell_PLUS)},
 { 0x0ac, SYMSTR(sp_PLUS_PLUS)},
 { 0x0ac, SYMSTR(1_PLUS)},
 { 0x0ac, SYMSTR(char_PLUS)},
 { 0x0ae, SYMSTR(popt)},
 { 0x0b0, SYMSTR(char_DASH)},
 { 0x0b0, SYMSTR(1_DASH)},
 { 0x0b0, SYMSTR(sp_DASH_DASH)},
 { 0x0b0, SYMSTR(cell_DASH)},
 { 0x0b2, SYMSTR(psht)},
 { 0x0b4, SYMSTR(x_BANG)},
 { 0x0b6, SYMSTR(APOS_BANG)},
 { 0x0b6, SYMSTR(APOS_c_BANG)},
 { 0x0b7, SYMSTR(popts)},
 { 0x0b8, SYMSTR(pops)},
 { 0x0ba, SYMSTR(pshs)},
 { 0x0bc, SYMSTR(page_AT)},
 { 0x0be, SYMSTR(pshw)},
 { 0x0c0, SYMSTR(page_BANG)},
 { 0x0c3, SYMSTR(sp_AT)},
 { 0x0c6, SYMSTR(sp_BANG)},
 { 0x0c8, SYMSTR(APOS_drop)},
 { 0x0c9, SYMSTR(APOS_over)},
 { 0x0ca, SYMSTR(APOS_dup)},
 { 0x0cb, SYMSTR(APOS_swap)},
 { 0x0cd, SYMSTR(APOS_2_SLASH)},
 { 0x0cf, SYMSTR(um_PLUS)},
 { 0x0d2, SYMSTR(APOS_nc)},
 { 0x0d3, SYMSTR(APOS_cy)},
 { 0x0d8, SYMSTR(zless)},
 { 0x0db, SYMSTR(APOS_or)},
 { 0x0dc, SYMSTR(APOS_xor)},
 { 0x0dd, SYMSTR(APOS_and)},
 { 0x0de, SYMSTR(negate)},
 { 0x0df, SYMSTR(invert)},
 { 0x0e0, SYMSTR(zeq)},
 { 0x0e2, SYMSTR(APOS_PLUS)},
 { 0x0e3, SYMSTR(swap_DASH)},
 { 0x3ff, (char*) 0},
};
static const uint18_t eForth_stack_rom[] = {23330,245077,70840,79034,79026,153528,79034,70839,243640,137150,153534,133306,240797,202069,226645,131949,132016,111827,151013,21845,1,131951,251859,251858,111834,148429,148309,132858,235448,260024,79024,210261,103647,148309,251832,207346,210872,79017,79017,79017,79017,71077,78908,5461,18933,1,153260,137130,18933,262143,194224,159666,78910,38229,79028,79022,194222,161109,194226,161109,23298,153941,194234,161109};

const f18_symbol_t sdram_mux_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(a2rc)},
 { 0x0af, SYMSTR(row_BANG)},
 { 0x0bb, SYMSTR(sd_AT)},
 { 0x0c5, SYMSTR(sd_BANG)},
 { 0x0cf, SYMSTR(poll)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sdram_mux_rom[] = {19378,277,23298,14770,158501,79018,17378,132096,48426,23117,158466,19378,277,23333,22187,19202,349,222317,192434,79317,134322,16562,2048,103643,190898,79173,158130,243663,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71077,149698,198594,198594,198594,111801,16530,32767,17330,98304,48426,23112,16538,24576,16469,1023,131506,70831,79018,17378,164864,48426,23115};

const f18_symbol_t sdram_idle_sym[] = {
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(noop)},
 { 0x0ac, SYMSTR(cmd)},
 { 0x0ae, SYMSTR(idle)},
 { 0x0c0, SYMSTR(init)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sdram_idle_rom[] = {18610,4761,79018,128194,79018,22188,66560,79018,22188,32769,79018,79018,22188,32770,79018,79018,22188,33,79018,79018,22188,16384,79018,70830,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,79017,71077,23125,78848,23085,23112,23218,78848,22188,32771,79018,18610,120,23218,78848,100,10674,14890,128183,70830,241845,23218,78848,70830};

const f18_symbol_t analog_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(poly)},
 { 0x0b0, SYMSTR(STAR_DOT_17)},
 { 0x0b7, SYMSTR(STAR_DOT)},
 { 0x0bc, SYMSTR(DASH_dac)},
 { 0x0c4, SYMSTR(interp)},
 { 0x0ce, SYMSTR(triangle)},
 { 0x0d3, SYMSTR(clc)},
 { 0x2d5, SYMSTR(DASH_DASH_u_SLASH_mod)},
 { 0x2d6, SYMSTR(DASH_u_SLASH_mod)},
 { 0x3ff, (char*) 0},
};
static const uint18_t analog_rom[] = {159714,141746,121778,121637,149690,133554,201130,256027,26288,208043,148402,217530,240816,87381,20329,65536,240117,239280,210261,151011,180717,79571,179642,17,150002,191920,151216,112350,240760,150005,132066,159960,150005,158234,195708,243624,159679,145057,39714,31602,141485,71045,158394,28338,194231,158232,143532,193877,179643,16,148402,217545,144054,239325,238797,79024,140395,239322,210261,238677,149690,134346,247999,341};

const f18_symbol_t one_wire_sym[] = {
 { 0x09e, SYMSTR(rcv)},
 { 0x0a1, SYMSTR(bit)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cold)},
 { 0x0be, SYMSTR(triangle)},
 { 0x0c3, SYMSTR(STAR_DOT_17)},
 { 0x0ca, SYMSTR(STAR_DOT)},
 { 0x0cf, SYMSTR(clc)},
 { 0x0cf, SYMSTR(interp)},
 { 0x2d1, SYMSTR(DASH_DASH_u_SLASH_mod)},
 { 0x2d2, SYMSTR(DASH_u_SLASH_mod)},
 { 0x3ff, (char*) 0},
};
static const uint18_t one_wire_rom[] = {240117,239280,210261,179643,16,148402,217545,144073,239325,238797,79043,140398,239322,210261,238677,151011,180717,79567,179642,17,150002,191920,151216,112347,240818,128724,150005,132066,159956,150005,141459,179642,17,8706,111782,239322,209057,158293,238713,158293,79017,71045,23978,373,421,19347,349,19898,183,196026,16,8706,112005,240306,70817,194206,177822,192699,87381,79006,61627,87381,20329,65536};

const f18_symbol_t sync_boot_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cold)},
 { 0x0b6, SYMSTR(ser_DASH_exec)},
 { 0x0b9, SYMSTR(ser_DASH_copy)},
 { 0x0be, SYMSTR(sget)},
 { 0x0c0, SYMSTR(6in)},
 { 0x0c2, SYMSTR(2in)},
 { 0x0cc, SYMSTR(STAR_DOT_17)},
 { 0x0d3, SYMSTR(taps)},
 { 0x0db, SYMSTR(triangle)},
 { 0x3ff, (char*) 0},
};
static const uint18_t sync_boot_rom[] = {79042,79042,222354,180738,209603,212475,2,180738,185031,16578,2,230229,179643,16,148402,217545,144082,239325,238797,158394,32442,141490,79052,158394,60592,159956,10837,20334,65536,240117,239280,210261,79017,158234,195708,243624,159679,145057,39714,31602,141485,71045,18954,12709,2482,111795,19899,261509,245682,111798,128176,18666,389,193877,79038,194238,177854,192699,87381,79038,61627,87381,153280,79040};

const f18_symbol_t spi_boot_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cold)},
 { 0x0b0, SYMSTR(spi_DASH_boot)},
 { 0x0b6, SYMSTR(spi_DASH_exec)},
 { 0x0bc, SYMSTR(spi_DASH_copy)},
 { 0x0c2, SYMSTR(8obits)},
 { 0x0c7, SYMSTR(ibit)},
 { 0x0ca, SYMSTR(half)},
 { 0x0cc, SYMSTR(select)},
 { 0x0d0, SYMSTR(obit)},
 { 0x0d5, SYMSTR(rbit)},
 { 0x0d9, SYMSTR(18ibits)},
 { 0x3ff, (char*) 0},
};
static const uint18_t spi_boot_rom[] = {152917,79017,18610,7,79056,225476,87381,2409,239325,238797,36794,182645,22218,47,22474,43,111829,22218,58,22474,59,22218,42,22474,43,155066,17,79061,79047,209115,87381,204285,131071,158234,195708,243624,159679,145057,39714,31602,141485,71093,1714,112053,23999,497,0,3072,79052,79042,79042,240818,79042,79042,243417,18930,122880,112053,194265,177881,192702,87381,79065,61630};

const f18_symbol_t async_boot_sym[] = {
 { 0x0a1, SYMSTR(relay)},
 { 0x0a9, SYMSTR(warm)},
 { 0x0aa, SYMSTR(cold)},
 { 0x0ae, SYMSTR(ser_DASH_exec)},
 { 0x0b3, SYMSTR(ser_DASH_copy)},
 { 0x0bb, SYMSTR(wait)},
 { 0x0be, SYMSTR(sync)},
 { 0x0c5, SYMSTR(start)},
 { 0x0c8, SYMSTR(delay)},
 { 0x0cb, SYMSTR(18ibits)},
 { 0x0d0, SYMSTR(byte)},
 { 0x0d2, SYMSTR(4bits)},
 { 0x0d3, SYMSTR(2bits)},
 { 0x0d4, SYMSTR(1bit)},
 { 0x0d9, SYMSTR(lsh)},
 { 0x0db, SYMSTR(rsh)},
 { 0x3ff, (char*) 0},
};
static const uint18_t async_boot_rom[] = {2411,180856,71093,180890,206677,153275,134594,247986,2409,180856,5461,79038,79038,153285,79059,79056,243397,79058,79059,79060,190362,131858,131072,254850,194504,190898,225621,190898,201045,79017,79017,79017,79017,158234,195708,243624,159679,145057,39714,31602,141485,71093,18954,12709,2482,111800,79051,239794,79051,240306,79051,239741,87381,79051,243837,87381,245178,437,193877,180738,111803,180821,150859,231098};

const f18_rom_t RomMap[] = {
 [basic] = {basic, "basic", "1.2", basic_rom, 64},
 [serdes_boot] = {serdes_boot, "serdes_boot", "1.2", serdes_boot_rom, 64},
 [sdram_data] = {sdram_data, "sdram_data", "1.2", sdram_data_rom, 64},
 [sdram_control] = {sdram_control, "sdram_control", "1.2", sdram_control_rom, 64},
 [sdram_addr] = {sdram_addr, "sdram_addr", "1.2", sdram_addr_rom, 64},
 [eForth_bitsy] = {eForth_bitsy, "eForth_bitsy", "1.2", eForth_bitsy_rom, 64},
 [eForth_stack] = {eForth_stack, "eForth_stack", "1.2", eForth_stack_rom, 64},
 [sdram_mux] = {sdram_mux, "sdram_mux", "1.2", sdram_mux_rom, 64},
 [sdram_idle] = {sdram_idle, "sdram_idle", "1.2", sdram_idle_rom, 64},
 [analog] = {analog, "analog", "1.2", analog_rom, 64},
 [one_wire] = {one_wire, "one_wire", "1.2", one_wire_rom, 64},
 [sync_boot] = {sync_boot, "sync_boot", "1.2", sync_boot_rom, 64},
 [spi_boot] = {spi_boot, "spi_boot", "1.2", spi_boot_rom, 64},
 [async_boot] = {async_boot, "async_boot", "1.2", async_boot_rom, 64},
};

const f18_rom_type_t RomTypeMap[8][18] = {
  {basic,serdes_boot,basic,basic,basic,basic,basic,sdram_data,sdram_control,sdram_addr,basic,basic,basic,basic,basic,basic,basic,basic,},
  {basic,basic,basic,basic,basic,eForth_bitsy,eForth_stack,sdram_mux,sdram_idle,basic,basic,basic,basic,basic,basic,basic,basic,analog,},
  {one_wire,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,},
  {sync_boot,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,},
  {basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,},
  {basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,},
  {basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,basic,analog,},
  {basic,serdes_boot,basic,basic,basic,spi_boot,basic,basic,async_boot,analog,basic,basic,basic,analog,basic,basic,basic,analog,},
};

const f18_symbol_t* SymMap[8][18] = {
  {basic_sym,serdes_boot_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,sdram_data_sym,sdram_control_sym,sdram_addr_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,},
  {basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,eForth_bitsy_sym,eForth_stack_sym,sdram_mux_sym,sdram_idle_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,analog_sym,},
  {one_wire_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,},
  {sync_boot_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,},
  {basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,},
  {basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,},
  {basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,basic_sym,analog_sym,},
  {basic_sym,serdes_boot_sym,basic_sym,basic_sym,basic_sym,spi_boot_sym,basic_sym,basic_sym,async_boot_sym,analog_sym,basic_sym,basic_sym,basic_sym,analog_sym,basic_sym,basic_sym,basic_sym,analog_sym,},
};
