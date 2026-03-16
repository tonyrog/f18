//
// F18 Async Boot Node (708) Implementation
//
// Node 708 has special async serial boot capability.
// This module handles the async I/O for that node.
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>
#include <termios.h>

#include "f18.h"
#include "f18_node.h"
#include "f18_async.h"

// Global async nodes
async_reader_t r708;
async_writer_t w708;

#define BITS_PER_WORD     30  // 3 bytes * 10 bits (start + 8 data + stop)
#define SAMPLES_PER_BIT   10

// Initialize async_reader sync buffer
void async_reader_init(async_reader_t* ap)
{
    byte_queue_init(&ap->bq);
    ap->sample_count = 1;
    ap->bit_count = 0;
    ap->first_bit_received = 0;
}

// read_ioreg for node 708 - synchronous bit delivery
// Called when 708 reads from IO register
uint18_t read_ioreg_708(node_t* np, uint18_t ioreg)
{
    uint18_t ior_val;
    int pin17;

    // Check if this ioreg read includes GPIO direction
    // For 708, io_addr is set (0x91), so we check if ioreg matches
    uint18_t iodir = np->io_addr ?
	dirbits(ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), np->io_addr) : 0;
    uint18_t dirs = ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) ?
	dirbits(ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg) : 0;

    // IOREG_IO (0x15D) is "---D" = GPIO only, handle specially
    int is_gpio_read = (ioreg == IOREG_IO) || (iodir && (dirs & iodir));

    PRINTF("read_ioreg_708: ioreg=0x%03x iodir=0x%x dirs=0x%x gpio=%d\n",
	   ioreg, iodir, dirs, is_gpio_read);

    // If not a GPIO read, use default handler
    if (!is_gpio_read) {
	PRINTF("read_ioreg_708: not GPIO, fallback\n");
	return f18_read_ioreg(np, ioreg);
    }

    pin17 = byte_queue_curr(&r708.bq);
    if (r708.sample_count <= 0) {  // next bit
	if (r708.bit_count < BITS_PER_WORD) {
	    // Within word or starting new word
	    pin17 = byte_queue_deq(&r708.bq);  // May block if empty
	    r708.bit_count++;
	    // After bit 5 (sync pattern), add half-bit delay for center sampling
	    switch(r708.bit_count) {
	    case 8:  // sample 1.5 bit (stretch B0 instead of B0,START
	    case 12:
	    case 22:
		r708.sample_count = SAMPLES_PER_BIT + (SAMPLES_PER_BIT / 2);
		PRINTF("708/ bit=%d, pin=%d count=%d\n",
		       r708.bit_count, pin17, r708.sample_count);
		break;
	    default:  // sample 1 bit
		r708.sample_count = SAMPLES_PER_BIT;
		PRINTF("708/ bit=%d,pin=%d,count=%d\n",
		       r708.bit_count, pin17, r708.sample_count);
	    }
	}
	else {
	    // After 30 bits - get next word (block if needed)
	    r708.bit_count = 0;  // Reset for next word
	    PRINTF("708/ word done, waiting for next\n");
	    pin17 = byte_queue_deq(&r708.bq);  // Block until next frame
	    r708.bit_count++;
	    r708.sample_count = SAMPLES_PER_BIT;
	}
    }
    r708.sample_count--;

    // Build IOR value with current PIN17 state
    ior_val = __atomic_load_n(&np->ior, __ATOMIC_SEQ_CST);
    if (pin17)
	ior_val |= F18_IO_PIN17;
    else
	ior_val &= ~F18_IO_PIN17;
    return ior_val;
}

// READ from GPIO - fills bit buffer for 708 to consume
void async_reader(async_reader_t* ap)
{
    printf("async_reader: started baud=%d (sync buffer mode)\n", ap->baud);

    tcflush(ap->fd, TCIFLUSH);
    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	uint8_t w18[3];
	int n;

	if ((n = read(ap->fd, w18, 3)) == 3) {
	    uint8_t bits[30];  // 3 bytes * 10 bits each
	    int bi = 0;
	    int i, j;

	    // Build all 30 bits first
	    for (i = 0; i < 3; i++) {
		uint8_t b0 = ~w18[i];  // invert all bits

		// Start bit (HIGH after inversion)
		bits[bi++] = 1;

		// 8 data bits (LSB first)
		for (j = 0; j < 8; j++) {
		    bits[bi++] = b0 & 1;
		    b0 >>= 1;
		}
		// Stop bit (LOW after inversion)
		bits[bi++] = 0;
	    }

	    // Push all 30 bits atomically
	    byte_queue_enq_batch(&ap->bq, bits, 30);

	    PRINTF("async_reader: delivered bytes 0x%02x%02x%02x\n",
		   w18[2], w18[1], w18[0]);
	}
	else if (n == 0) {
	    // No data, retry
	    continue;
	}
	else if (n < 0) {
	    if (errno == EINTR)
		continue;
	    ERRORF("async_reader: read error %d (%s)\n",
		   errno, strerror(errno));
	    return;
	}
    }
}

// WRITE to GPIO (emulated serial port/socket whatever)
// when 708 write value to ioreg 'io' then it ends up here
//
void async_writer(async_writer_t* ap)
{
    int count = 0;
    uint18_t bits = 0;

    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	uint8_t b;
	int i;
	chan_t* rp = ap->in;  // like 708 gpio output

	while((count < 10) && !ap->chan.terminate) {
	    uint18_t value;

	    if (f18_chan_read(rp, GPIO, &value))
		;
	    else
	    {
		f18_init_transfer(&ap->chan, READ, DIR_BIT(GPIO), 0, 0);
		if (f18_chan_read(rp, GPIO, &value)) {
		    f18_complete_transfer(&ap->chan, READ);
		}
		else {
		    value = f18_wait_transfer(&ap->chan, READ);
		    if (ap->chan.terminate)
			break;
		}
	    }
	    // FIXME: may implement multiple pins (configure in async_writer)
	    // emulate bit sending 11 = 0, 10 => 1
	    PRINTF("async_writer: got value=%d, count=%d\n", value, count);
	    if (value == 3) {
		bits = (bits << 1) | 0;
		count++;
	    }
	    else if (value == 2) {
		bits = (bits << 1) | 1;
		count++;
	    }
	    else {
		PRINTF("async_writer: unexpected value %d\n", value);
	    }
	}
	if (!ap->chan.terminate) {
	    // reverse bits
	    b = 0;
	    bits >>= 1; // skip "stop" bit
	    for (i = 0; i < 8; i++) {
		b = (b << 1) | (bits & 1);
		bits >>= 1;
	    }
	    if (ap->fd >= 0) {
		PRINTF("async_writer: output byte %02x '%c'\n",
		       b, (b >= 32 && b < 127) ? b : '?');
		// fix sending
		// write(ap->fd, &b, 1);
	    }
	    bits = 0;
	    count = 0;
	}
    }
}
