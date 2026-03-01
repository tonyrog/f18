// F18 Config map

#include "f18.h"

#define COLD 0x0aa  // cold start address


#define UNDEF { .rom=basic, .comm=0,           .io_addr=0, .io_type=none, .reset=0L }
#define RD    { .rom=basic, .comm=IOREG_RD__,  .io_addr=0, .io_type=none, .reset=IOREG_RD__ }
#define RDL   { .rom=basic, .comm=IOREG_RDL_,  .io_addr=0, .io_type=none, .reset=IOREG_RDL_ }
#define RDU   { .rom=basic, .comm=IOREG_RD_U,  .io_addr=0, .io_type=none, .reset=IOREG_RD_U }
#define RDLU  { .rom=basic, .comm=IOREG_RDLU,  .io_addr=0, .io_type=none, .reset=IOREG_RDLU }

#define N000 RD
#define N001 { .rom = serdes_boot, .comm = IOREG_RDL_, .io_addr=IOREG____U, .io_type=serdes,.reset=COLD }
#define N002 RDL
#define N003 RDL
#define N004 RDL
#define N005 RDL
#define N006 RDL
#define N007 { .rom=sdram_data,    .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=parallel_bus, .reset=IOREG_RDL_ }
#define N008 { .rom=sdram_control, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=gpio_x4,      .reset=IOREG_RDL_ }
#define N009 { .rom=sdram_addr,    .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=parallel_bus, .reset=IOREG_RDL_ }
#define N010 RDL
#define N011 RDL
#define N012 RDL
#define N013 RDL
#define N014 RDL
#define N015 RDL
#define N016 RDL
#define N017 RD
#define N100 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N101 RDLU
#define N102 RDLU
#define N103 RDLU
#define N104 RDLU
#define N105 { .rom=eForth_bitsy, .comm=IOREG_RDLU,  .io_addr=0, .reset=IOREG_RDLU }
#define N106 { .rom=eForth_stack, .comm=IOREG_RDLU,  .io_addr=0, .reset=IOREG_RDLU }
#define N107 { .rom=sdram_mux,    .comm=IOREG_RDLU,  .io_addr=0, .reset=IOREG_RDLU }
#define N108 { .rom=sdram_idle,   .comm=IOREG_RDLU,  .io_addr=0, .reset=IOREG_RDLU }
#define N109 RDLU
#define N110 RDLU
#define N111 RDLU
#define N112 RDLU
#define N113 RDLU
#define N114 RDLU
#define N115 RDLU
#define N116 RDLU
#define N117 { .rom=analog, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=analog_pin, .reset=IOREG_RD_U }
#define N200 { .rom=one_wire, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N201 RDLU
#define N202 RDLU
#define N203 RDLU
#define N204 RDLU
#define N205 RDLU
#define N206 RDLU
#define N207 RDLU
#define N208 RDLU
#define N209 RDLU
#define N210 RDLU
#define N211 RDLU
#define N212 RDLU
#define N213 RDLU
#define N214 RDLU
#define N215 RDLU
#define N216 RDLU
#define N217 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N300 { .rom=sync_boot, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x2, .reset=COLD }
#define N301 RDLU
#define N302 RDLU
#define N303 RDLU
#define N304 RDLU
#define N305 RDLU
#define N306 RDLU
#define N307 RDLU
#define N308 RDLU
#define N309 RDLU
#define N310 RDLU
#define N311 RDLU
#define N312 RDLU
#define N313 RDLU
#define N314 RDLU
#define N315 RDLU
#define N316 RDLU
#define N317 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N400 RDU
#define N401 RDLU
#define N402 RDLU
#define N403 RDLU
#define N404 RDLU
#define N405 RDLU
#define N406 RDLU
#define N407 RDLU
#define N408 RDLU
#define N409 RDLU
#define N410 RDLU
#define N411 RDLU
#define N412 RDLU
#define N413 RDLU
#define N414 RDLU
#define N415 RDLU
#define N416 RDLU
#define N417 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N500 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N501 RDLU
#define N502 RDLU
#define N503 RDLU
#define N504 RDLU
#define N505 RDLU
#define N506 RDLU
#define N507 RDLU
#define N508 RDLU
#define N509 RDLU
#define N510 RDLU
#define N511 RDLU
#define N512 RDLU
#define N513 RDLU
#define N514 RDLU
#define N515 RDLU
#define N516 RDLU
#define N517 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N600 { .rom=basic, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=gpio_x1, .reset=IOREG_RD_U }
#define N601 RDLU
#define N602 RDLU
#define N603 RDLU
#define N604 RDLU
#define N605 RDLU
#define N606 RDLU
#define N607 RDLU
#define N608 RDLU
#define N609 RDLU
#define N610 RDLU
#define N611 RDLU
#define N612 RDLU
#define N613 RDLU
#define N614 RDLU
#define N615 RDLU
#define N616 RDLU
#define N617 { .rom=analog, .comm=IOREG_RD_U,  .io_addr=IOREG___L_, .io_type=analog_pin, .reset=IOREG_RD_U }
#define N700 RD
#define N701 { .rom=serdes_boot, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=serdes, .reset=COLD }
#define N702 RDL
#define N703 RDL
#define N704 RDL
#define N705 { .rom=spi_boot, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=gpio_x4, .reset=COLD }
#define N706 RDL
#define N707 RDL
#define N708 { .rom=async_boot, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=gpio_x2, .reset=COLD }
#define N709 { .rom=analog, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=analog_pin, .reset=IOREG_RDL_ }
#define N710 RDL
#define N711 RDL
#define N712 RDL
#define N713 { .rom=analog, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=analog_pin, .reset=IOREG_RDL_ }
#define N714 RDL
#define N715 { .rom=basic, .comm=IOREG_RDL_,  .io_addr=IOREG____U, .io_type=gpio_x1, .reset=IOREG_RDL_ }
#define N716 RDL
#define N717 { .rom=analog, .comm=IOREG_RD__,  .io_addr=IOREG____U, .io_type=analog_pin, .reset=IOREG_RD__ }

const f18_config_t ConfigMap[8][18] =
{
    
    /* 000 */ { N000, N001, N002, N003, N004, N005, N006, N006, N008, N009, N010, N011, N012, N013, N014, N015, N016, N017 },
    /* 100 */ { N100, N101, N102, N103, N104, N105, N106, N106, N108, N109, N110, N111, N112, N113, N114, N115, N116, N117 },    
    /* 200 */ { N200, N201, N202, N203, N204, N205, N206, N206, N208, N209, N210, N211, N212, N213, N214, N215, N216, N217 },
    /* 300 */ { N300, N301, N302, N303, N304, N305, N306, N306, N308, N309, N310, N311, N312, N313, N314, N315, N316, N317 },
    /* 400 */ { N400, N401, N402, N403, N404, N405, N406, N406, N408, N409, N410, N411, N412, N413, N414, N415, N416, N417 },
    /* 500 */ { N500, N501, N502, N503, N504, N505, N506, N506, N508, N509, N510, N511, N512, N513, N514, N515, N516, N517 },
    /* 600 */ { N600, N601, N602, N603, N604, N605, N606, N606, N608, N609, N610, N611, N612, N613, N614, N615, N616, N617 },
    /* 700 */ { N700, N701, N702, N703, N704, N705, N706, N706, N708, N709, N710, N711, N712, N713, N714, N715, N716, N717 },
};

