/*	$NetBSD: ucomvar.h,v 1.9 2001/01/23 21:56:17 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/ucomvar.h,v 1.8 2006/09/07 00:06:41 imp Exp $	*/

/*-
 * Copyright (c) 2001-2002, Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
 * All rights reserved.
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
 */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Module interface related macros */
#define UCOM_MODVER	1

#define UCOM_MINVER	1
#define UCOM_PREFVER	UCOM_MODVER
#define UCOM_MAXVER	1

struct ucom_softc;
struct thread;

struct ucom_callback {
    void (*ucom_get_status)(struct ucom_softc *, u_int8_t *, u_int8_t *);
    void (*ucom_set_dtr)(struct ucom_softc *, u_int8_t);
    void (*ucom_set_rts)(struct ucom_softc *, u_int8_t);
    void (*ucom_set_break)(struct ucom_softc *, u_int8_t);
    int  (*ucom_param)(struct ucom_softc *, struct termios *);
    int  (*ucom_ioctl)(struct ucom_softc *, u_long, caddr_t, int, struct thread *);
    int  (*ucom_open)(struct ucom_softc *);
    void (*ucom_close)(struct ucom_softc *);
    void (*ucom_start_read)(struct ucom_softc *);
    void (*ucom_stop_read)(struct ucom_softc *);
    void (*ucom_start_write)(struct ucom_softc *);
    void (*ucom_stop_write)(struct ucom_softc *);
};

/* line status register */
#define ULSR_RCV_FIFO	0x80
#define ULSR_TSRE	0x40	/* Transmitter empty: byte sent */
#define ULSR_TXRDY	0x20	/* Transmitter buffer empty */
#define ULSR_BI		0x10	/* Break detected */
#define ULSR_FE		0x08	/* Framing error: bad stop bit */
#define ULSR_PE		0x04	/* Parity error */
#define ULSR_OE		0x02	/* Overrun, lost incoming byte */
#define ULSR_RXRDY	0x01	/* Byte ready in Receive Buffer */
#define ULSR_RCV_MASK	0x1f	/* Mask for incoming data or error */

struct ucom_softc {
	struct task            sc_task;

  const struct ucom_callback   *sc_callback;
	struct tty             *sc_tty;
	void                   *sc_parent;

	u_int16_t	       sc_portno;

	u_int8_t               sc_flag;
#define UCOM_FLAG_RTS_IFLOW    0x01 /* use RTS input flow control */
#define UCOM_FLAG_GONE         0x02 /* the device is gone */

	u_int8_t               sc_lsr;
	u_int8_t               sc_msr;
	u_int8_t               sc_mcr;
	u_int8_t               sc_poll;
        u_int8_t               sc_last_status;
};

extern devclass_t ucom_devclass;

extern int
ucom_attach(struct ucom_softc *sc, device_t dev);

extern void
ucom_detach(struct ucom_softc *);

extern void
ucom_status_change(struct ucom_softc *);

extern u_int8_t
ucom_get_data(struct ucom_softc *sc, u_int8_t *buf, u_int32_t len,
	      u_int32_t *actlen);

extern void
ucom_put_data(struct ucom_softc *sc, u_int8_t *ptr, u_int16_t len);
