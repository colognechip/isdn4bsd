/*-
 *
 * Copyright (c) 2005 Henk Vergonet. All rights reserved.
 *
 * Copyright (c) 2009 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *---------------------------------------------------------------------------*/

#ifndef _YEALINK_H_
#define	_YEALINK_H_

#define	YEALINK_PKT_LEN			16	/* bytes */
#define	YEALINK_CMD_INIT                0x8e
#define	YEALINK_CMD_KEYPRESS            0x80
#define	YEALINK_CMD_SCANCODE            0x81
#define	YEALINK_CMD_LCD                 0x04
#define	YEALINK_CMD_LED                 0x05
#define	YEALINK_CMD_RING_VOLUME         0x11
#define	YEALINK_CMD_RING_NOTE           0x02
#define	YEALINK_CMD_RINGTONE            0x03
#define	YEALINK_CMD_DIALTONE            0x09

#define	YEALINK_LCD_LINE1_OFFSET        0
#define	YEALINK_LCD_LINE1_SIZE          17
#define	YEALINK_LCD_LINE2_OFFSET        (YEALINK_LCD_LINE1_OFFSET + YEALINK_LCD_LINE1_SIZE)
#define	YEALINK_LCD_LINE2_SIZE          9
#define	YEALINK_LCD_LINE3_OFFSET        (YEALINK_LCD_LINE2_OFFSET + YEALINK_LCD_LINE2_SIZE)
#define	YEALINK_LCD_LINE3_SIZE          12
#define	YEALINK_LCD_LINE4_OFFSET        (YEALINK_LCD_LINE3_OFFSET + YEALINK_LCD_LINE3_SIZE)
#define	YEALINK_LCD_LINE4_SIZE          3
#define	YEALINK_LCD_LINE5_OFFSET        (YEALINK_LCD_LINE4_OFFSET + YEALINK_LCD_LINE4_SIZE)
#define	YEALINK_LCD_LINE5_SIZE		0

struct yealink_ctl_packet {
	uByte	cmd;
	uByte	size;
	uWord	offset;
	uByte	data[11];
	uByte	sum;
} __packed;

struct yealink_status {
	uByte	lcd[24];
	uByte	led;
	uByte	dialtone;
	uByte	ringtone;
	uByte	keynum;
} __packed;

#define	YEALINK_SEG(t, a, am, b, bm, c, cm, d, dm, e, em, f, fm, g, gm)	\
	{ t, { .s = { {a, am}, {b, bm}, {c, cm}, {d, dm},		\
		      {e, em}, {g, gm}, {f, fm} } } }

#define	YEALINK_SYM(t, h, hm, n)		\
	{ t, { .p = { h, hm, n } } }

struct yealink_lcd_map {
	char	type;
	union {
		struct {
			uint8_t	a;	/* address */
			uint8_t	m;	/* mask */
			char	desc[12];
		}	p;
		struct {
			uint8_t	a;	/* address */
			uint8_t	m;	/* mask */
		}	s     [7];
	}	u;
};

struct yealink_softc {
	uint8_t	lcd_map[YEALINK_LCD_LINE5_OFFSET];
	uint8_t	last_key;

};

#endif					/* _YEALINK_H_ */
