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
    ap->state = ASYNC_STATE_IDLE;
}

// read_ioreg for node 708 - synchronous bit delivery
// Called when 708 reads from IO register
//
// State machine:
//   IDLE     -> block until first data arrives, then ACTIVE
//   ACTIVE   -> receiving bits, block if no data
//   COMPLETE -> word done, non-blocking idle if no data (stay in @ loop)
//
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

    PRINTF("read_ioreg_708: ioreg=0x%03x iodir=0x%x dirs=0x%x gpio=%d state=%d\n",
	   ioreg, iodir, dirs, is_gpio_read, r708.state);

    // If not a GPIO read, use default handler
    if (!is_gpio_read) {
	PRINTF("read_ioreg_708: not GPIO, fallback\n");
	return f18_read_ioreg(np, ioreg);
    }

    pin17 = byte_queue_curr(&r708.bq);

    if (r708.sample_count <= 0) {  // time for next bit
	switch (r708.state) {
	case ASYNC_STATE_ACTIVE:
	    if (r708.bit_count < BITS_PER_WORD) {
		// Within word - block until data
		pin17 = byte_queue_deq(&r708.bq);  // blocks
		r708.bit_count++;
		// After certain bits, add half-bit delay for center sampling
		switch(r708.bit_count) {
		case 8:  // sample 1.5 bit
		case 12:
		case 22:
		    r708.sample_count = SAMPLES_PER_BIT + (SAMPLES_PER_BIT / 2);
		    break;
		default:
		    r708.sample_count = SAMPLES_PER_BIT;
		}
		PRINTF("708/ ACTIVE: bit=%d, pin=%d, count=%d\n",
		       r708.bit_count, pin17, r708.sample_count);
	    }
	    else {
		// Word complete - transition to COMPLETE
		r708.state = ASYNC_STATE_COMPLETE;
		r708.bit_count = 0;
		PRINTF("708/ ACTIVE->COMPLETE: word done\n");
		// Fall through to COMPLETE handling
	    }
	    if (r708.state != ASYNC_STATE_COMPLETE)
		break;
	    /* FALLTHROUGH */

	case ASYNC_STATE_COMPLETE:
	    // Word done - check for more data without blocking
	    if (byte_queue_available(&r708.bq)) {
		// More data - start next word
		r708.state = ASYNC_STATE_ACTIVE;
		pin17 = byte_queue_deq(&r708.bq);
		r708.bit_count = 1;
		r708.sample_count = SAMPLES_PER_BIT;
		PRINTF("708/ COMPLETE->ACTIVE: next word, bit=%d, pin=%d\n",
		       r708.bit_count, pin17);
		break;
	    }
	    // No data - fall through to IDLE to block for next boot
	    r708.state = ASYNC_STATE_IDLE;
	    PRINTF("708/ COMPLETE->IDLE: no data, will block\n");
	    /* FALLTHROUGH */

	case ASYNC_STATE_IDLE:
	    // First read - block until data arrives
	    PRINTF("708/ IDLE: waiting for data...\n");
	    pin17 = byte_queue_deq(&r708.bq);  // blocks
	    r708.state = ASYNC_STATE_ACTIVE;
	    r708.bit_count = 1;
	    r708.sample_count = SAMPLES_PER_BIT;
	    PRINTF("708/ IDLE->ACTIVE: bit=%d, pin=%d\n", r708.bit_count, pin17);
	    break;
	}
    }

    if (r708.sample_count > 0)
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
		write(ap->fd, &b, 1);
	    }
	    bits = 0;
	    count = 0;
	}
    }
}
