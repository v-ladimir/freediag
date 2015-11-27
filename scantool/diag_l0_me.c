/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * Diag, Layer 0, Interface for Multiplex Engineering interface
 *		Supports #T16 interface only. Other interfaces need special
 *		code to read multi-frame messages with > 3 frames (and don't
 *		support all interface types)
 *
 *   http://www.multiplex-engineering.com
 *
 */


#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_iso14230.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"


#define INTERFACE_ADDRESS 0x38


extern const struct diag_l0 diag_l0_me;

/*
 * Baud rate table for converting single byte value from interface to
 * baud rate. Note the single byte value is count in 2.5microseconds for
 * receiving a bit of the 0x55
 */
static const unsigned int me_baud_table[] = { 0, 400000, 200000, 133333, 100000, 80000,
			66666, 57142, 50000, 44444,
	/* 10 */ 40000, 36363, 33333, 30769, 28571, 26666,
			25000, 23529, 22222, 21052,
	/* 20 */ 19200, 19200, 18181, 17391, 16666, 16000,
			15384, 14814, 14285, 13793,
	/* 30 */ 13333, 12903, 12500, 12121, 11764, 11428,
			11111, 10400, 10400, 10400,
	/* 40 */ 10400, 9600, 9600, 9600, 9600, 8888, 8695, 8510, 8333, 8163,
	/* 50 */ 8000, 7843, 7692, 7547, 7407, 7272, 7142, 7017, 6896, 6779,
	/* 60 */ 6666, 6557, 6451, 6349, 0, 6153, 6060, 5970, 5882, 5797,
	/* 70 */ 5714, 5633, 5555, 5479, 5405, 5333, 5263, 5194, 5128, 5063,
	/* 80 */ 5000, 4800, 4800, 4800, 4800, 4800, 4800, 4597, 4545, 4494,
	/* 90 */ 4444, 4395, 4347, 4301, 4255, 4210, 4166, 4123, 4081, 4040,
	/* 100 */ 4000, 3960, 3921, 3883, 3846, 3809, 3600, 3600, 3600, 3600,
	/* 110 */ 3600, 3600, 3600, 3600, 3600, 3478, 3448, 3418, 3389, 3361,
	/* 120 */ 3333, 3305, 3278, 3252, 3225, 3200, 3174, 3149, 3125, 3100,
	/* 130 */ 3076, 3053, 3030, 3007, 2985, 2962, 2941, 2919, 2898, 2877,
	/* 140 */ 2857, 2836, 2816, 2797, 2777, 2758, 2739, 2721, 2702, 2684,
	/* 150 */ 2666, 2649, 2631, 2614, 2597, 2580, 2564, 2547, 2531, 2515,
	/* 160 */ 2500, 2400, 2400, 2400, 2400, 2400, 2400, 2400, 2400, 2400,
	/* 170 */ 2400, 2400, 2400, 2400, 2298, 2285, 2272, 2259, 2247, 2234,
	/* 180 */ 2222, 2209, 2197, 2185, 2173, 2162, 2150, 2139, 2127, 2116,
	/* 190 */ 2105, 2094, 2083, 2072, 2061, 2051, 2040, 2030, 2020, 2010,
	/* 200 */ 2000, 1990, 1980, 1970, 1960, 1951, 1941, 1932, 1923, 1913,
	/* 210 */ 1904, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 220 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 230 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 240 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 250 */ 1600, 1593, 1587, 1581, 1574, 1568, } ;

/* interface error codes */
static const struct {
	int code;
	const char *str;
} me_errs[] = {
	{0x00, "Request Message command not implemented in this interface."},
	{0x01, "Request Message command is not used."},
	{0x02, "Request Message sum check error."},
	{0x03, "ISO or KWP - No sync or incorrect sync."},
	{0x04, "Incorrect ISO inverted address received."},
	{0x05, "No ISO response to the request message."},
	{0x06, "J1850 message received was not a diagnostic message."},
	{0x07, "No J1850 response to the request message."},
	{0x08, "ISO checksum error detected in response message."},
	{0x09, "J1850 CRC error detected in response message."},
	{0x0A, "Unused"},
	{0x0B, "KWP baud rate too slow"},
	{0x0C, "No KWP response to the request message."},
	{0x0D, "KWP incorrect inverted address returned by car"},
	{0x0E, "PWM pulse width is too long"},
	{0x0F, "Incorrect variable response counter"},
	{0x10, "ISO not enabled"},
	{0x11, "J1850 VPW not enabled"},
	{0x12, "J1850 PWM not enabled"},
	{0x13, "KWP not enabled"},
	{0x14, "VW Pass-through mode not enabled"},
	{0x15, "Repeated arbitration errors"},
	{0x16, "CAN did not respond to request"},
	{0x17, "CAN core command had a bad command count"},
	{0x20, "CAN destination address byte error"},
	{0x21, "CAN command byte error"},
	{0x22, "CAN byte count byte error"},
	{0x23, "CAN sum check byte error"},
	{0x24, "CAN RS232 receive message - 100ms timeout error"},
	{0x25, "CAN Configuration command address error"},
	{0x26, "CAN stop bit error"},
	{0x27, "CAN transmit message - 100ms timeout error"},
	{0x28, "CAN transmit error"},
	{0x29, "CAN transmit lost arbitration"},
	{0x2A, "CAN receive message - 100ms timeout error"},
	{0x2B, "CAN Mode Request - 100ms timeout error"},
	{0x2C, "CAN invalid CAN byte count error"},
	{0xF1, "Unimplemented USB command attempted"},
	{0xF2, "Legacy (Non CAN) bus hardware did not respond to USB request"},
	{0xF3, "CAN hardware did not respond to USB request"}
};

static const char *me_geterr(const int err)
{
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(me_errs); i++) {
		if (me_errs[i].code == err) {
			return me_errs[i].str;
		}
	}

	return "[undefined]";
}

struct diag_l0_muleng_device
{
	uint8_t dev_addr;	/* ME device address; default is 0x38. TODO : configurable */
	int protocol;
	int dev_wakeup;		/* Contains wakeup type for next packet */
	int dev_state;		/* State for 5 baud startup stuff */
	uint8_t dev_kb1;	/* KB1/KB2 for 5 baud startup stuff */
	uint8_t dev_kb2;

	uint8_t	dev_rxbuf[14];	/* Receive buffer */
	int		dev_rxlen;	/* Length of data in buffer */
	int		dev_rdoffset;	/* Offset to read from to */
};

#define MULENG_STATE_CLOSED		0x00

/* 5 baud init was successful, need to report keybytes on first recv() */
#define MULENG_STATE_KWP_SENDKB1		0x01
#define MULENG_STATE_KWP_SENDKB2		0x02

#define MULENG_STATE_RAW		0x10	/* Open and working in Passthru mode */

#define MULENG_STATE_FASTSTART		0x18	/* 1st recv() after fast init */
#define MULENG_STATE_OPEN		0x20	/* Open and working */


static void diag_l0_muleng_close(struct diag_l0_device *dl0d);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_muleng_init(void)
{
/* Global init flag */
	static int diag_l0_muleng_initdone=0;

	if (diag_l0_muleng_initdone)
		return 0;

	/* Do required scheduling tweaks */
	diag_os_sched();
	diag_l0_muleng_initdone = 1;

	return 0;
}

/* Put in the ME checksum at the correct place */
static int
diag_l0_muleng_txcksum(uint8_t *data)
{
	uint8_t cksum;

	cksum = diag_cks1(data, 14);
	data[14] = cksum;
	return cksum;
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_muleng_open(const char *subinterface, int iProtocol)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_muleng_device *dev;
	struct diag_serial_settings set;

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_muleng_init();

	if ((rv=diag_calloc(&dev, 1)))
		return diag_pseterr(rv);

	dev->protocol = iProtocol;

	dl0d = diag_l0_new(&diag_l0_me, (void *)dev);
	if (!dl0d) {
		free(dev);
		return diag_pseterr(rv);
	}
	/* try to open TTY */
	if ((rv=diag_tty_open(dl0d, subinterface))) {
		diag_l0_del(dl0d);
		free(dev);
		return diag_pseterr(rv);
	}

	/* And set to 19200 baud , 8N1 */

	set.speed = 19200;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if ((rv=diag_tty_setup(dl0d, &set))) {
		diag_l0_muleng_close(dl0d);
		return diag_pseterr(rv);
	}

	/* And set DTR high and RTS low to power the device */
	if ((rv=diag_tty_control(dl0d, 1, 0))) {
		diag_l0_muleng_close(dl0d);
		return diag_pseterr(rv);
	}
	dev->dev_addr = INTERFACE_ADDRESS;

	diag_tty_iflush(dl0d);	/* Flush unread input */

	return dl0d ;
}

static void
diag_l0_muleng_close(struct diag_l0_device *dl0d)
{
	if (!dl0d) return;
	struct diag_l0_muleng_device *dev =
		(struct diag_l0_muleng_device *)dl0d->l0_int;

	if (diag_l0_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, FLFMT "link %p closing\n",
			FL, (void *)dl0d);

	if (dev)
		free(dev);

	diag_tty_close(dl0d);
	diag_l0_del(dl0d);

	return;
}

/*
 * Safe write routine; return 0 on success
 */
static int
diag_l0_muleng_write(struct diag_l0_device *dl0d, const void *dp, size_t txlen)
{
	if (txlen <=0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) )
	{
		fprintf(stderr, FLFMT "device link %p sending to ME device: ",
			FL, (void *)dl0d);
		diag_data_dump(stderr, dp, txlen);
		fprintf(stderr, "\n");
	}

	/*
	 * And send it to the interface
	 */
	if (diag_tty_write(dl0d, dp, txlen) != (int) txlen) {
		fprintf(stderr, FLFMT "muleng_write error!!\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
}


/*
 * Do 5 Baud initialisation
 *
 * In the case of ISO9141 we operate in the interface's "raw" mode
 * (VAG compatibility mode), in 14230 we do a slow init and send
 * a tester present message
 */
static int
diag_l0_muleng_slowinit( struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in,
		struct diag_l0_muleng_device *dev)
{
	/*
	 * Slow init
	 * Build message into send buffer, and calculate checksum
	 */
	uint8_t txbuf[15];
	uint8_t rxbuf[15];
	int rv;
	unsigned int baud;

	memset(txbuf, 0, sizeof(txbuf));
	txbuf[0] = dev->dev_addr;

	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		txbuf[1] = 0x20;	/* Raw mode 5 baud init */
		txbuf[2] = in->addr;
		break;
	case DIAG_L1_ISO14230:
		txbuf[1] = 0x85;
		txbuf[2] = 0x01;		/* One byte message */
		txbuf[3] = DIAG_KW2K_SI_TP;	/* tester present */
		break;
	}

	/*
	 * Calculate the checksum, and send the request
	 */
	(void)diag_l0_muleng_txcksum(txbuf);
	if ((rv = diag_l0_muleng_write(dl0d, txbuf, 15)))
		return diag_iseterr(rv);

	/*
	 * Get answer
	 */
	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		/*
		 * This is raw mode, we should get a single byte back
		 * with the timing interval, then we need to change speed
		 * to match that speed. Remember it takes 2 seconds to send
		 * the 10 bit (1+8+1) address at 5 baud
		 */
		rv = diag_tty_read(dl0d, rxbuf, 1, 2350);
		if (rv != 1)
			return diag_iseterr(DIAG_ERR_GENERAL);

		if (rxbuf[0] == 0x40) {
			/* Problem ..., got an error message */

			diag_tty_iflush(dl0d); /* Empty the receive buffer */

			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		baud = me_baud_table[rxbuf[0]];

		if (diag_l0_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "device link %p setting baud to %u\n",
				FL, (void *)dl0d, baud);

		if (baud) {
			struct diag_serial_settings set;
			set.speed = baud;
			set.databits = diag_databits_8;
			set.stopbits = diag_stopbits_1;
			set.parflag = diag_par_n;

			/* And set the baud rate */
			diag_tty_setup(dl0d, &set);
		}

		dev->dev_state = MULENG_STATE_RAW;

		break;
	case DIAG_L1_ISO14230:
		/* XXX
		 * Should get an ack back, rather than an error response
		 */
		if ((rv = diag_tty_read(dl0d, rxbuf, 14, 200)) < 0)
			return diag_iseterr(rv);

		if (rxbuf[1] == 0x80)
			return diag_iseterr(DIAG_ERR_GENERAL);

		/*
 		 * Now send the "get keybyte" request, and wait for
		 * response
		 */
		memset(txbuf, 0, sizeof(txbuf));
		txbuf[0] = dev->dev_addr;
		txbuf[1] = 0x86;
		(void)diag_l0_muleng_txcksum(txbuf);
		rv = diag_l0_muleng_write(dl0d, txbuf, 15);
		if (rv < 0)
			return diag_iseterr(rv);

		if ((rv = diag_tty_read(dl0d, rxbuf, 14, 200)) < 0)
			return diag_iseterr(rv);

		if (rxbuf[1] == 0x80)	/* Error */
			return diag_iseterr(rv);
		/*
		 * Store the keybytes
		 */
		dev->dev_kb1 = rxbuf[2];
		dev->dev_kb2 = rxbuf[3];
		/*
		 * And tell read code to report the keybytes on first read
		 */
		dev->dev_state = MULENG_STATE_KWP_SENDKB1;
		break;
	}


	return rv;
}

/*
 * Do wakeup on the bus
 *
 * We do this by noting a wakeup needs to be done for the next packet for
 * fastinit, and doing slowinit now
 */
static int
diag_l0_muleng_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = 0;
	struct diag_l0_muleng_device *dev;

	dev = (struct diag_l0_muleng_device *)dl0d->l0_int;

	if (!dev)
		return diag_iseterr(DIAG_ERR_GENERAL);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d proto %d\n",
			FL, (void *)dl0d, (void *)dev, in->type, dev->protocol);

	diag_tty_iflush(dl0d); /* Empty the receive buffer, wait for idle bus */

	if (in->type == DIAG_L1_INITBUS_5BAUD)
		rv = diag_l0_muleng_slowinit(dl0d, in, dev);
	else
	{
		/* Do wakeup on first TX */
		dev->dev_wakeup = in->type;
		dev->dev_state = MULENG_STATE_FASTSTART;
	}

	return rv;
}

/*
 * Set speed/parity etc : ignored.
 */

static int
diag_l0_muleng_setspeed(UNUSED(struct diag_l0_device *dl0d),
		UNUSED(const struct diag_serial_settings *pset))
{
	fprintf(stderr, FLFMT "Warning: attempted to override com speed (%d)! Report this !\n", FL,pset->speed);
	return 0;
}

/*
 * Send a load of data
 *
 * Returns 0 on success, -1 on failure
 *
 * This routine will do a fastinit if needed, but all 5 baud inits
 * will have been done by the slowinit() code
 */
static int
diag_l0_muleng_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len)
{
	int rv;
	uint8_t cmd;

	uint8_t txbuf[MAXRBUF];
	struct diag_l0_muleng_device *dev;

	dev = (struct diag_l0_muleng_device *)dl0d->l0_int;

	if (len <= 0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (len > 255) {
		fprintf(stderr, FLFMT "_send : requesting too many bytes !\n", FL);
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, FLFMT "device link %p send %ld bytes protocol %d ",
			FL, (void *)dl0d, (long)len, dev->protocol);
		if (diag_l0_debug & DIAG_DEBUG_DATA)
		{
			diag_data_dump(stderr, data, len);
			fprintf(stderr, "\n");
		}
	}

	if (dev->dev_state == MULENG_STATE_RAW)
	{
		/* Raw mode, no pretty processing */
		rv = diag_l0_muleng_write(dl0d, data, len);
		return rv;
	}

	/*
	 * Figure out cmd to send depending on the hardware we have been
	 * told to use and whether we need to do fastinit or not
	 */
	switch (dev->protocol)
	{
	case DIAG_L1_ISO9141:
		cmd = 0x10;
		break;

	case DIAG_L1_ISO14230:
		if (dev->dev_wakeup == DIAG_L1_INITBUS_FAST)
			cmd = 0x87;
		else
			cmd = 0x88;
		dev->dev_wakeup = 0;	/* We've done the wakeup now */
		break;

	case DIAG_L1_J1850_VPW:
		cmd = 0x02;
		break;

	case DIAG_L1_J1850_PWM:
		cmd = 0x04;
		break;

	case DIAG_L1_CAN:
		cmd = 0x08;
		break;
	default:
		fprintf(stderr, FLFMT "Command never initialised.\n", FL);
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

	/*
	 * Build message into send buffer, and calculate checksum and
	 * send it
	 */
	memset(txbuf, 0, sizeof(txbuf));

	txbuf[0] = dev->dev_addr;
	txbuf[1] = cmd;
	txbuf[2] = (uint8_t) len;
	memcpy(&txbuf[3], data, len);

	(void)diag_l0_muleng_txcksum(txbuf);
	rv = diag_l0_muleng_write(dl0d, txbuf, 15);

	return rv;
}

/*
 * Get data (blocking), returns number of bytes read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 *
 * This attempts to read whole message, so if we receive any data, timeout
 * is restarted
 *
 * Messages received from the ME device are 14 bytes long, this will
 * always be called with enough "len" to receive the max 11 byte message
 * (there are 2 header and 1 checksum byte)
 */

static int
diag_l0_muleng_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
void *data, size_t len, unsigned int timeout)
{
	ssize_t xferd;
	int rv;
	uint8_t *pdata = (uint8_t *)data;

	struct diag_l0_muleng_device *dev;
	dev = (struct diag_l0_muleng_device *)dl0d->l0_int;

	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %ld bytes timeout %u, rxlen %d offset %d\n",
			FL, (void *)dl0d, (long)len, timeout, dev->dev_rxlen, dev->dev_rdoffset);

	/*
	 * Deal with 5 Baud init states where first two bytes read by
	 * user are the keybytes received from the interface, and where
	 * we are using the interface in pass thru mode on ISO09141 protocols
	 */
	switch (dev->dev_state)
	{
	case MULENG_STATE_KWP_SENDKB1:
		if (len >= 2)
		{
			pdata[0] = dev->dev_kb1;
			pdata[1] = dev->dev_kb2;
			dev->dev_state = MULENG_STATE_OPEN;
			return 2;
		}
		else if (len == 1)
		{
			*pdata = dev->dev_kb1;
			dev->dev_state = MULENG_STATE_KWP_SENDKB2;
			return 1;
		}
		return 0;	/* Strange, user asked for 0 bytes */


	case MULENG_STATE_KWP_SENDKB2:
		if (len >= 1)
		{
			*pdata = dev->dev_kb2;
			dev->dev_state = MULENG_STATE_OPEN;
			return 1;
		}
		return 0;	/* Strange, user asked for 0 bytes */


	case MULENG_STATE_RAW:
		xferd = diag_tty_read(dl0d, data, len, timeout);
		if (diag_l0_debug & DIAG_DEBUG_READ)
			fprintf(stderr, FLFMT "link %p read %ld bytes\n", FL,
				(void *)dl0d, (long)xferd);
		return xferd;

	case MULENG_STATE_FASTSTART:
		/* Extend timeout for 1st recv */
		timeout = 200;
		dev->dev_state = MULENG_STATE_OPEN;
		/* Drop thru */
	default:		/* Some other mode */
		break;
	}

	if (dev->dev_rxlen >= 14)
	{
		/*
		 * There's a full packet been received, but the user
		 * has only asked for a few bytes from it previously
		 * Of the packet, bytes x[2]->x[11] are the network data
		 * others are header from the ME device
		 *
		 * The amount of data remaining to be sent to user is
		 * as below, -1 because the checksum is at the end
		 */
		size_t bufbytes = dev->dev_rxlen - dev->dev_rdoffset - 1;

		if (bufbytes <= len)
		{
			memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], bufbytes);
			dev->dev_rxlen = dev->dev_rdoffset = 0;
			return (int) bufbytes;
		}
		else
		{
			memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], len);
			dev->dev_rdoffset += len;
			return (int) len;
		}
	}

	/*
	 * There's either no data waiting, or only a partial read in the
	 * buffer, read some more
	 */

	if (dev->dev_rxlen < 14) {
		rv = diag_tty_read(dl0d, &dev->dev_rxbuf[dev->dev_rxlen],
			(size_t)(14 - dev->dev_rxlen), timeout);
		if (rv == DIAG_ERR_TIMEOUT) {
			return DIAG_ERR_TIMEOUT;
		}

		if (rv <= 0) {
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

		dev->dev_rxlen += rv;
	}

	/* OK, got whole message */
	if (diag_l0_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr,
			FLFMT "link %p received from ME: ", FL, (void *)dl0d);
		diag_data_dump(stderr, dev->dev_rxbuf, dev->dev_rxlen);
		fprintf(stderr, "\n");
	}
	/*
	 * Check the checksum, 2nd byte onward
	 */
	xferd = diag_cks1(&dev->dev_rxbuf[1], 13);
	if ((xferd & 0xff) != dev->dev_rxbuf[13])
	{
/* XXX, we should deal with this properly rather than just printing a message */
		fprintf(stderr,"Got bad checksum from ME device 0x%X != 0x%X\n",
			(int) xferd & 0xff, dev->dev_rxbuf[13]);
		fprintf(stderr,"PC Serial port probably out of spec.\nRX Data: ");
		diag_data_dump(stderr, dev->dev_rxbuf, dev->dev_rxlen);
		fprintf(stderr, "\n");
	}


	/*
	 * Check the type
	 */
	if (dev->dev_rxbuf[1] == 0x80)
	{
		/* It's an error message not a data frame */
		dev->dev_rxlen = 0;

		if (diag_l0_debug & DIAG_DEBUG_READ)
			fprintf(stderr,
				FLFMT "link %p ME returns err 0x%X : %s; s/w v 0x%X i/f cap. 0x%X\n",
				FL, (void *)dl0d, dev->dev_rxbuf[3], me_geterr(dev->dev_rxbuf[3]),
				dev->dev_rxbuf[2], dev->dev_rxbuf[4]);

		switch (dev->dev_rxbuf[3])
		{
		case 0x05:	/* No ISO response to request */
		case 0x07:	/* No J1850 response to request */
		case 0x0c:	/* No KWP response to request */
			return DIAG_ERR_TIMEOUT;
			break;

		default:
			if ( !(diag_l0_debug & DIAG_DEBUG_READ)) {
				//don't print the error twice
				fprintf(stderr, FLFMT "ME : error 0x%0X, %s\n.", FL,
					dev->dev_rxbuf[3], me_geterr(dev->dev_rxbuf[3]) );
			}
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		/* NOTREACHED */
	}

	dev->dev_rdoffset = 2;		/* Skip the ME header */

	/* Copy data to user */
	xferd = (len>11) ? 11 : xferd;
	xferd = (xferd>(13-dev->dev_rdoffset)) ? 13-dev->dev_rdoffset : xferd;

	memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], (size_t)xferd);
	dev->dev_rdoffset += xferd;
	if (dev->dev_rdoffset == 13)
	{
		/* End of message, reset pointers */
		dev->dev_rxlen = 0;
		dev->dev_rdoffset = 0;
	}
	return xferd;
}

static uint32_t
diag_l0_muleng_getflags(struct diag_l0_device *dl0d)
{
	/*
	 * ISO14230/J1850 protocol does L2 framing, ISO9141 doesn't
	 */
	struct diag_l0_muleng_device *dev;
	int flags;

	dev = (struct diag_l0_muleng_device *)dl0d->l0_int;

	flags = DIAG_L1_AUTOSPEED;
	switch (dev->protocol)
	{
	case DIAG_L1_J1850_VPW:
	case DIAG_L1_J1850_PWM:
			flags = DIAG_L1_DOESL2CKSUM;
			flags |= DIAG_L1_DOESL2FRAME;
			break;
	case DIAG_L1_ISO9141:
			flags = DIAG_L1_SLOW ;
/* XX does it ?		flags |= DIAG_L1_DOESL2CKSUM; */
			break;

 	case DIAG_L1_ISO14230:
			flags = DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
			flags |= DIAG_L1_DOESL2FRAME;
			flags |= DIAG_L1_DOESSLOWINIT;
			flags |= DIAG_L1_DOESL2CKSUM;
			break;

	}

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr,
			FLFMT "getflags link %p proto %d flags 0x%X\n",
			FL, (void *)dl0d, dev->protocol, flags);

	return flags ;
}

const struct diag_l0 diag_l0_me = {
	"Multiplex Engineering T16 interface",
	"MET16",
	DIAG_L1_J1850_VPW | DIAG_L1_J1850_PWM |
		DIAG_L1_ISO9141 | DIAG_L1_ISO14230,
	NULL,
	NULL,
	NULL,
	diag_l0_muleng_init,
	diag_l0_muleng_open,
	diag_l0_muleng_close,
	diag_l0_muleng_initbus,
	diag_l0_muleng_send,
	diag_l0_muleng_recv,
	diag_l0_muleng_setspeed,
	diag_l0_muleng_getflags
};
