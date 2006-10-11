/*	$NetBSD: umodem.c,v 1.45 2002/09/23 05:51:23 simonb Exp $	*/

/*-
 * Copyright (c) 2003, M. Warner Losh <imp@freebsd.org>.
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
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

/*
 * Comm Class spec:  http://www.usb.org/developers/devclass_docs/usbccs10.pdf
 *                   http://www.usb.org/developers/devclass_docs/usbcdc11.pdf
 */

/*
 * TODO:
 * - Add error recovery in various places; the big problem is what
 *   to do in a callback if there is an error.
 * - Implement a Call Device for modems without multiplexed commands.
 *
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/termios.h>
#include <sys/serial.h>
#include <sys/taskqueue.h>

#include <dev/usb2/usb_port.h>
#include <dev/usb2/usb.h>
#include <dev/usb2/usb_subr.h>
#include <dev/usb2/usb_quirks.h>
#include <dev/usb2/usb_cdc.h>

#include <dev/usb/ucomvar.h>

#include "usbdevs.h"

__FBSDID("$FreeBSD: src/sys/dev/usb/umodem.c $");

#ifdef USB_DEBUG
#define DPRINTF(n,fmt,...)						\
  do { if (umodem_debug > (n)) {						\
      printf("%s: " fmt, __FUNCTION__,## __VA_ARGS__); } } while (0)

static int umodem_debug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, umodem, CTLFLAG_RW, 0, "USB umodem");
SYSCTL_INT(_hw_usb_umodem, OID_AUTO, debug, CTLFLAG_RW,
	   &umodem_debug, 0, "umodem debug level");
#else
#define DPRINTF(...)
#endif

static const struct umodem_product {
	u_int16_t	vendor;
	u_int16_t	product;
	u_int8_t	interface;
} umodem_products[] = {
	/* Kyocera AH-K3001V*/
	{ USB_VENDOR_KYOCERA, USB_PRODUCT_KYOCERA_AHK3001V, 0 },
	{ 0, 0, 0 },
};

/*
 * These are the maximum number of bytes transferred per frame.
 * If some really high speed devices should use this driver they
 * may need to be increased, but this is good enough for normal modems.
 */
#define UMODEM_IBUFSIZE 64
#define UMODEM_OBUFSIZE 256
#define UMODEM_N_DATA_TRANSFER 7
#define UMODEM_N_INTR_TRANSFER 2

#define UMODEM_MODVER			1	/* module version */

struct umodem_softc {
	struct ucom_softc	sc_ucom;
	usb_cdc_line_state_t	sc_line_state;	/* current line state */

	struct usbd_xfer *	sc_xfer_data[UMODEM_N_DATA_TRANSFER];
	struct usbd_xfer *	sc_xfer_intr[UMODEM_N_INTR_TRANSFER];

	u_int8_t		sc_lsr;	/* local status register */
	u_int8_t		sc_msr;	/* modem status register */
	u_int8_t		sc_break; /* current BREAK state */
	u_int8_t		sc_dtr;	/* current DTR state */
	u_int8_t		sc_rts;	/* current RTS state */
	u_int8_t		sc_ctrl_iface_no;
	u_int8_t		sc_ctrl_iface_index;
	u_int8_t		sc_data_iface_no;
	u_int8_t		sc_data_iface_index;
	u_int8_t		sc_cm_over_data;
	u_int8_t		sc_cm_cap; /* CM capabilities */
	u_int8_t		sc_acm_cap; /* ACM capabilities */
	u_int8_t		sc_flag;
#define UMODEM_FLAG_READ_STALL  0x01
#define UMODEM_FLAG_WRITE_STALL 0x02
#define UMODEM_FLAG_INTR_STALL  0x04
#define UMODEM_FLAG_SET_LS      0x08
#define UMODEM_FLAG_SEND_BREAK  0x10
#define UMODEM_FLAG_SET_LC      0x20
};

static device_probe_t umodem_probe;
static device_attach_t umodem_attach;
static device_detach_t umodem_detach;

static int
umodem_open(struct ucom_softc *ucom);

static void
umodem_close(struct ucom_softc *ucom);

static void
umodem_start_read(struct ucom_softc *ucom);

static void
umodem_stop_read(struct ucom_softc *ucom);

static void
umodem_start_write(struct ucom_softc *ucom);

static void
umodem_stop_write(struct ucom_softc *ucom);

static void
umodem_get_caps(struct usbd_device *udev, u_int8_t *cm, u_int8_t *acm);

static void
umodem_get_status(struct ucom_softc *ucom, u_int8_t *lsr, u_int8_t *msr);

static int
umodem_param(struct ucom_softc *ucom, struct termios *t);

static int
umodem_ioctl(struct ucom_softc *ucom, u_long cmd, caddr_t data, 
	     int flag, struct thread *td);
static void
umodem_set_dtr(struct ucom_softc *ucom, u_int8_t onoff);

static void
umodem_set_rts(struct ucom_softc *ucom, u_int8_t onoff);

static void
umodem_set_break(struct ucom_softc *ucom, u_int8_t onoff);

static void
umodem_intr_callback(struct usbd_xfer *xfer);

static void
umodem_intr_clear_stall_callback(struct usbd_xfer *xfer);

static void
umodem_set_line_state_callback(struct usbd_xfer *xfer);

static void
umodem_set_break_callback(struct usbd_xfer *xfer);

static void
umodem_write_callback(struct usbd_xfer *xfer);

static void
umodem_read_callback(struct usbd_xfer *xfer);

static void
umodem_write_clear_stall_callback(struct usbd_xfer *xfer);

static void
umodem_read_clear_stall_callback(struct usbd_xfer *xfer);

static void
umodem_set_line_coding_callback(struct usbd_xfer *xfer);

static void *
umodem_get_desc(struct usbd_device *udev, u_int8_t type, u_int8_t subtype);

static usbd_status
umodem_set_comm_feature(struct usbd_device *udev, u_int8_t iface_no, int feature, int state);

static const struct usbd_config umodem_config_data[UMODEM_N_DATA_TRANSFER] = {

    [0] = {
      .type      = UE_BULK,
      .endpoint  = -1, /* any */
      .direction = UE_DIR_OUT,
      .bufsize   = UMODEM_OBUFSIZE,
      .flags     = 0,
      .callback  = &umodem_write_callback,
    },

    [1] = {
      .type      = UE_BULK,
      .endpoint  = -1, /* any */
      .direction = UE_DIR_IN,
      .bufsize   = UMODEM_IBUFSIZE,
      .flags     = USBD_SHORT_XFER_OK,
      .callback  = &umodem_read_callback,
    },

    [2] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .callback  = &umodem_write_clear_stall_callback,
      .timeout   = 1000, /* 1 second */
    },

    [3] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .callback  = &umodem_read_clear_stall_callback,
      .timeout   = 1000, /* 1 second */
    },

    [4] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .callback  = &umodem_set_line_state_callback,
      .timeout   = 1000, /* 1 second */
    },

    [5] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .callback  = &umodem_set_break_callback,
      .timeout   = 1000, /* 1 second */
    },

    [6] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t) + sizeof(usb_cdc_line_state_t),
      .callback  = &umodem_set_line_coding_callback,
      .timeout   = 1000, /* 1 second */
    },
};

static const struct usbd_config umodem_config_intr[UMODEM_N_INTR_TRANSFER] = {
    [0] = {
      .type      = UE_INTERRUPT,
      .endpoint  = -1, /* any */
      .direction = UE_DIR_IN,
      .flags     = USBD_SHORT_XFER_OK,
      .bufsize   = 0, /* use wMaxPacketSize */
      .callback  = &umodem_intr_callback,
    },

    [1] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .callback  = &umodem_intr_clear_stall_callback,
      .timeout   = 1000, /* 1 second */
    },
};

static const struct ucom_callback umodem_callback = {
  .ucom_get_status  = &umodem_get_status,
  .ucom_set_dtr     = &umodem_set_dtr,
  .ucom_set_rts     = &umodem_set_rts,
  .ucom_set_break   = &umodem_set_break,
  .ucom_param       = &umodem_param,
  .ucom_ioctl       = &umodem_ioctl,
  .ucom_open        = &umodem_open,
  .ucom_close       = &umodem_close,
  .ucom_start_read  = &umodem_start_read,
  .ucom_stop_read   = &umodem_stop_read,
  .ucom_start_write = &umodem_start_write,
  .ucom_stop_write  = &umodem_stop_write,
};

static device_method_t umodem_methods[] = {
    DEVMETHOD(device_probe, umodem_probe),
    DEVMETHOD(device_attach, umodem_attach),
    DEVMETHOD(device_detach, umodem_detach),
    { 0, 0 }
};

static driver_t umodem_driver = {
    .name    = "ucom",
    .methods = umodem_methods,
    .size    = sizeof(struct umodem_softc),
};

DRIVER_MODULE(umodem, uhub, umodem_driver, ucom_devclass, usbd_driver_load, 0);
MODULE_DEPEND(umodem, usb, 1, 1, 1);
MODULE_DEPEND(umodem, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
MODULE_VERSION(umodem, UMODEM_MODVER);

static int
umodem_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	usb_interface_descriptor_t *id;
	const struct umodem_product *up = umodem_products;
	u_int8_t cm, acm;
	int error = UMATCH_NONE;

	DPRINTF(10, "\n");

	if (uaa->iface == NULL) {
	    goto done;
	}

	id = usbd_get_interface_descriptor(uaa->iface);

	if (id == NULL) {
	    goto done;
	}

	while (up->vendor) {
	    if ((up->vendor == uaa->vendor) &&
		(up->product == uaa->product) &&
		(up->interface == id->bInterfaceNumber)) {
	        error = UMATCH_VENDOR_PRODUCT;
		break;
	    }
	    up++;
	}

	if ((error == UMATCH_NONE) &&
	    (id->bInterfaceClass == UICLASS_CDC) &&
	    (id->bInterfaceSubClass == UISUBCLASS_ABSTRACT_CONTROL_MODEL) &&
	    (id->bInterfaceProtocol == UIPROTO_CDC_AT)) {
	    error = UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO;
	}

	if (error == UMATCH_NONE) {
	    goto done;
	}

	umodem_get_caps(uaa->device, &cm, &acm);
	if (!(cm & USB_CDC_CM_DOES_CM) ||
	    !(cm & USB_CDC_CM_OVER_DATA) ||
	    !(acm & USB_CDC_ACM_HAS_LINE)) {
	    error = UMATCH_NONE;
	}

 done:
	return error;
}

static int
umodem_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umodem_softc *sc = device_get_softc(dev);
	usb_interface_descriptor_t *id;
	usb_cdc_cm_descriptor_t *cmd;
	struct usbd_interface *iface;
	u_int8_t i;
	int error;

	if (sc == NULL) {
	    return ENOMEM;
	}

	usbd_set_desc(dev, uaa->device);

	id = usbd_get_interface_descriptor(uaa->iface);

	sc->sc_ctrl_iface_no = id->bInterfaceNumber;
	sc->sc_ctrl_iface_index = uaa->iface_index;

	umodem_get_caps(uaa->device, &sc->sc_cm_cap, &sc->sc_acm_cap);

	/* get the data interface number */

	cmd = umodem_get_desc(uaa->device, UDESC_CS_INTERFACE, 
			      UDESCSUB_CDC_CM);

	if ((cmd == NULL) || (cmd->bLength < sizeof(*cmd))) {
	    device_printf(dev, "no CM descriptor!\n");
	    goto detach;
	}

	sc->sc_data_iface_no = cmd->bDataInterface;

	device_printf(dev, "data interface %d, has %sCM over "
		      "data, has %sbreak\n",
		      sc->sc_data_iface_no,
		      sc->sc_cm_cap & USB_CDC_CM_OVER_DATA ? "" : "no ",
		      sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK ? "" : "no ");

	/* get the data interface too */

	for (i = 0; ; i++) {

	    iface = usbd_get_iface(uaa->device, i);

	    if (iface) {

	        id = usbd_get_interface_descriptor(iface);

		if (id && (id->bInterfaceNumber == sc->sc_data_iface_no)) {
		    sc->sc_data_iface_index = i;
		    USBD_SET_IFACE_NO_PROBE(uaa->device, i);
		    break;
		}

	    } else {
	        device_printf(dev, "no data interface!\n");
		goto detach;	      
	    }
	}

	if (usbd_get_quirks(uaa->device)->uq_flags & UQ_ASSUME_CM_OVER_DATA) {
	    DPRINTF(0, "Quirk says to assume CM over data\n");
	    sc->sc_cm_over_data = 1;
	} else {
	    if (sc->sc_cm_cap & USB_CDC_CM_OVER_DATA) {
	        if (sc->sc_acm_cap & USB_CDC_ACM_HAS_FEATURE) {
		    error = umodem_set_comm_feature
		      (uaa->device, sc->sc_ctrl_iface_no, UCDC_ABSTRACT_STATE, UCDC_DATA_MULTIPLEXED);

		    if (error) {
		        device_printf(dev, "could not set data "
				      "multiplex mode\n");
			goto detach;
		    }
		}
		sc->sc_cm_over_data = 1;
	    }
	}

	error = usbd_transfer_setup(uaa->device, sc->sc_data_iface_index,
				    sc->sc_xfer_data, umodem_config_data, 
				    UMODEM_N_DATA_TRANSFER,
				    sc, &Giant);
	if (error) {
	    goto detach;
	}

	error = usbd_transfer_setup(uaa->device, sc->sc_ctrl_iface_index,
				    sc->sc_xfer_intr, umodem_config_intr,
				    UMODEM_N_INTR_TRANSFER,
				    sc, &Giant);
	if (error) {
	    /* ignore */
	    DPRINTF(0, "no interrupt pipe!\n");
	}

	sc->sc_rts = -1;
	sc->sc_dtr = -1;
	sc->sc_break = -1;

	sc->sc_ucom.sc_parent = sc;
	sc->sc_ucom.sc_portno = 0;
	sc->sc_ucom.sc_callback = &umodem_callback;

	error = ucom_attach(&(sc->sc_ucom), dev);

	if (error) {
	    goto detach;
	}

	return 0;

 detach:
	umodem_detach(dev);
	return ENXIO;
}

static int
umodem_open(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "sc=%p\n", sc);

	/* clear stall first */
	sc->sc_flag |= (UMODEM_FLAG_READ_STALL|
			UMODEM_FLAG_WRITE_STALL);

	if (sc->sc_xfer_intr[0]) {
	    usbd_transfer_start(sc->sc_xfer_intr[0]);
	}
	return 0;
}

static void
umodem_close(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "sc=%p\n", sc);

	if (sc->sc_xfer_intr[0]) {
	    usbd_transfer_stop(sc->sc_xfer_intr[0]);
	}
	return;
}

static void
umodem_start_read(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;
	usbd_transfer_start(sc->sc_xfer_data[1]);
	return;
}

static void
umodem_stop_read(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;
	usbd_transfer_stop(sc->sc_xfer_data[3]);
	usbd_transfer_stop(sc->sc_xfer_data[1]);
	return;
}

static void
umodem_start_write(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;
	usbd_transfer_start(sc->sc_xfer_data[0]);
	return;
}

static void
umodem_stop_write(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;
	usbd_transfer_stop(sc->sc_xfer_data[2]);
	usbd_transfer_stop(sc->sc_xfer_data[0]);
	return;
}

static void
umodem_get_caps(struct usbd_device *udev, u_int8_t *cm, u_int8_t *acm)
{
	usb_cdc_cm_descriptor_t *cmd;
	usb_cdc_acm_descriptor_t *cad;

	*cm = *acm = 0;

	cmd = umodem_get_desc(udev, UDESC_CS_INTERFACE, UDESCSUB_CDC_CM);
	if ((cmd == NULL) || (cmd->bLength < sizeof(*cmd))) {
		DPRINTF(0, "no CM desc\n");
		return;
	}
	*cm = cmd->bmCapabilities;

	cad = umodem_get_desc(udev, UDESC_CS_INTERFACE, UDESCSUB_CDC_ACM);
	if ((cad == NULL) || (cad->bLength < sizeof(*cad))) {
		DPRINTF(0, "no ACM desc\n");
		return;
	}
	*acm = cad->bmCapabilities;

	return;
}

static void
umodem_get_status(struct ucom_softc *ucom, u_int8_t *lsr, u_int8_t *msr)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "\n");

	if (lsr) {
	    *lsr = sc->sc_lsr;
	}

	if (msr) {
	    *msr = sc->sc_msr;
	}
	return;
}

static int
umodem_param(struct ucom_softc *ucom, struct termios *t)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "sc=%p\n", sc);

	USETDW(sc->sc_line_state.dwDTERate, t->c_ospeed);

	sc->sc_line_state.bCharFormat = (t->c_cflag & CSTOPB) ? 
	  UCDC_STOP_BIT_2 : UCDC_STOP_BIT_1;

	sc->sc_line_state.bParityType = (t->c_cflag & PARENB) ?
	  ((t->c_cflag & PARODD) ? 
	   UCDC_PARITY_ODD : UCDC_PARITY_EVEN) : UCDC_PARITY_NONE;

	switch (t->c_cflag & CSIZE) {
	case CS5:
		sc->sc_line_state.bDataBits = 5;
		break;
	case CS6:
		sc->sc_line_state.bDataBits = 6;
		break;
	case CS7:
		sc->sc_line_state.bDataBits = 7;
		break;
	case CS8:
		sc->sc_line_state.bDataBits = 8;
		break;
	}

	sc->sc_flag |= UMODEM_FLAG_SET_LC;
	usbd_transfer_start(sc->sc_xfer_data[6]);
	return 0;
}

static int
umodem_ioctl(struct ucom_softc *ucom, u_long cmd, caddr_t data, 
	     int flag, struct thread *td)
{
	struct umodem_softc *sc = ucom->sc_parent;
	int error = 0;

	DPRINTF(0, "cmd=0x%08lx\n", cmd);

	switch (cmd) {
	case USB_GET_CM_OVER_DATA:
		*(int *)data = sc->sc_cm_over_data;
		break;

	case USB_SET_CM_OVER_DATA:
		if (*(int *)data != sc->sc_cm_over_data) {
			/* XXX change it */
		}
		break;

	default:
		DPRINTF(0, "unknown\n");
		error = ENOTTY;
		break;
	}

	return (error);
}

static void
umodem_set_dtr(struct ucom_softc *ucom, u_int8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "onoff=%d\n", onoff);

	if (sc->sc_dtr != onoff) {
	    sc->sc_dtr = onoff;
	    sc->sc_flag |= UMODEM_FLAG_SET_LS;
	    usbd_transfer_start(sc->sc_xfer_data[4]);
	}
	return;
}

static void
umodem_set_rts(struct ucom_softc *ucom, u_int8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "onoff=%d\n", onoff);

	if (sc->sc_rts != onoff) {
	    sc->sc_rts = onoff;
	    sc->sc_flag |= UMODEM_FLAG_SET_LS;
	    usbd_transfer_start(sc->sc_xfer_data[4]);
	}
	return;
}

static void
umodem_set_break(struct ucom_softc *ucom, u_int8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF(0, "onoff=%d\n", onoff);

	if (sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK) {
	    sc->sc_break = onoff;
	    sc->sc_flag |= UMODEM_FLAG_SEND_BREAK;
	    usbd_transfer_start(sc->sc_xfer_data[5]);
	}
	return;
}

static void
umodem_intr_callback(struct usbd_xfer *xfer)
{
	usb_cdc_notification_t *nbuf = xfer->buffer;
	struct umodem_softc *sc = xfer->priv_sc;
	u_int16_t wLength;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	if (xfer->error != USBD_CANCELLED) {
	    sc->sc_flag |= UMODEM_FLAG_INTR_STALL;
	    usbd_transfer_start(sc->sc_xfer_intr[1]);
	}
	return;

 tr_transferred:

	if (xfer->actlen >= 8) {

	    if (nbuf->bmRequestType != UCDC_NOTIFICATION) {
	        DPRINTF(0, "unknown message type, "
			"0x%02x, on notify pipe!\n",
			nbuf->bmRequestType);
	        goto tr_setup;
	    }

	    switch (nbuf->bNotification) {
	    case UCDC_N_SERIAL_STATE:
		/*
		 * Set the serial state in ucom driver based on
		 * the bits from the notify message
		 */
	        wLength = UGETW(nbuf->wLength);

		if ((wLength < 2) ||
		    ((wLength + 8) < xfer->actlen)) {
		    DPRINTF(0, "Invalid notification length, "
			    "%d bytes!\n", wLength);
		    break;
		}
		DPRINTF(0, "notify bytes = %02x%02x\n",
			nbuf->data[0],
			nbuf->data[1]);

		/* Currently, lsr is always zero. */
		sc->sc_lsr = 0;
		sc->sc_msr = 0;

		if (nbuf->data[0] & UCDC_N_SERIAL_RI) {
		    sc->sc_msr |= SER_RI;
		}
		if (nbuf->data[0] & UCDC_N_SERIAL_DSR) {
		    sc->sc_msr |= SER_DSR;
		}
		if (nbuf->data[0] & UCDC_N_SERIAL_DCD) {
		    sc->sc_msr |= SER_DCD;
		}

		ucom_status_change(&(sc->sc_ucom));
		break;

	    default:
	        DPRINTF(0, "unknown notify message: %02x\n",
			nbuf->bNotification);
		break;
	    }

	} else {
	    DPRINTF(0, "received short packet, "
		    "%d bytes\n", xfer->actlen);
	}

 tr_setup:
	if (sc->sc_flag & UMODEM_FLAG_INTR_STALL) {
	    usbd_transfer_start(sc->sc_xfer_intr[1]);
	} else {
	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_intr_clear_stall_callback(struct usbd_xfer *xfer)
{
	struct umodem_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_setup:
	/* start clear stall */
	usbd_clear_stall_tr_setup(xfer, sc->sc_xfer_intr[0]);
	return;

 tr_transferred:
	usbd_clear_stall_tr_transferred(xfer, sc->sc_xfer_intr[0]);
	sc->sc_flag &= ~UMODEM_FLAG_INTR_STALL;
	usbd_transfer_start(sc->sc_xfer_intr[0]);
	return;

 tr_error:
	sc->sc_flag &= ~UMODEM_FLAG_INTR_STALL;
	DPRINTF(0, "clear stall failed, error=%s\n",
		usbd_errstr(xfer->error));
	return;
}

static void
umodem_set_line_state_callback(struct usbd_xfer *xfer)
{
	usb_device_request_t *req = xfer->buffer;
	struct umodem_softc *sc = xfer->priv_sc;
	u_int16_t temp;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	DPRINTF(0, "error=%s\n", usbd_errstr(xfer->error));

 tr_setup:
 tr_transferred:
	if (sc->sc_flag &   UMODEM_FLAG_SET_LS) {
	    sc->sc_flag &= ~UMODEM_FLAG_SET_LS;

	    temp = ((sc->sc_dtr ? UCDC_LINE_DTR : 0) |
		    (sc->sc_rts ? UCDC_LINE_RTS : 0));

	    req->bmRequestType = UT_WRITE_CLASS_INTERFACE;
	    req->bRequest = UCDC_SET_CONTROL_LINE_STATE;
	    USETW(req->wValue, temp);
	    USETW(req->wIndex, sc->sc_ctrl_iface_no);
	    USETW(req->wLength, 0);

	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_set_break_callback(struct usbd_xfer *xfer)
{
	usb_device_request_t *req = xfer->buffer;
	struct umodem_softc *sc = xfer->priv_sc;
	u_int16_t temp;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	DPRINTF(0, "error=%s\n", usbd_errstr(xfer->error));

 tr_setup:
 tr_transferred:

	if (sc->sc_flag &   UMODEM_FLAG_SEND_BREAK) {
	    sc->sc_flag &= ~UMODEM_FLAG_SEND_BREAK;

	    temp = sc->sc_break ? UCDC_BREAK_ON : UCDC_BREAK_OFF;

	    req->bmRequestType = UT_WRITE_CLASS_INTERFACE;
	    req->bRequest = UCDC_SEND_BREAK;
	    USETW(req->wValue, temp);
	    USETW(req->wIndex, sc->sc_ctrl_iface_no);
	    USETW(req->wLength, 0);

	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_set_line_coding_callback(struct usbd_xfer *xfer)
{
	usb_device_request_t *req = xfer->buffer;
	usb_cdc_line_state_t *ls = (void *)(req+1);
	struct umodem_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	DPRINTF(0, "error=%s\n", usbd_errstr(xfer->error));

 tr_setup:
 tr_transferred:

	if (sc->sc_flag &   UMODEM_FLAG_SET_LC) {
	    sc->sc_flag &= ~UMODEM_FLAG_SET_LC;

	    DPRINTF(0, "rate=%d fmt=%d parity=%d bits=%d\n",
		    UGETDW(ls->dwDTERate), ls->bCharFormat,
		    ls->bParityType, ls->bDataBits);

	    req->bmRequestType = UT_WRITE_CLASS_INTERFACE;
	    req->bRequest = UCDC_SET_LINE_CODING;
	    USETW(req->wValue, 0);
	    USETW(req->wIndex, sc->sc_ctrl_iface_no);
	    USETW(req->wLength, sizeof(*ls));

	    *ls = sc->sc_line_state;

	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_write_callback(struct usbd_xfer *xfer)
{
	struct umodem_softc *sc = xfer->priv_sc;
	u_int32_t actlen;

	USBD_CHECK_STATUS(xfer);
tr_error:
	if (xfer->error != USBD_CANCELLED) {
	    sc->sc_flag |= UMODEM_FLAG_WRITE_STALL;
	    usbd_transfer_start(sc->sc_xfer_data[2]);
	}
	return;

tr_setup:
tr_transferred:
	if (sc->sc_flag & UMODEM_FLAG_WRITE_STALL) {
	    usbd_transfer_start(sc->sc_xfer_data[2]);
	    return;
	}

	if(ucom_get_data(&(sc->sc_ucom), xfer->buffer, UMODEM_OBUFSIZE, &actlen)) {

	    xfer->length = actlen;

	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_write_clear_stall_callback(struct usbd_xfer *xfer)
{
	struct umodem_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_setup:
	/* start clear stall */
	usbd_clear_stall_tr_setup(xfer, sc->sc_xfer_data[0]);
	return;

 tr_transferred:
	usbd_clear_stall_tr_transferred(xfer, sc->sc_xfer_data[0]);
	sc->sc_flag &= ~UMODEM_FLAG_WRITE_STALL;
	usbd_transfer_start(sc->sc_xfer_data[0]);
	return;

 tr_error:
	sc->sc_flag &= ~UMODEM_FLAG_WRITE_STALL;
	DPRINTF(0, "clear stall failed, error=%s\n",
		usbd_errstr(xfer->error));
	return;
}

static void
umodem_read_callback(struct usbd_xfer *xfer)
{
	struct umodem_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	if (xfer->error != USBD_CANCELLED) {
	    sc->sc_flag |= UMODEM_FLAG_READ_STALL;
	    usbd_transfer_start(sc->sc_xfer_data[3]);
	}
	return;

 tr_transferred:
	ucom_put_data(&(sc->sc_ucom), xfer->buffer, xfer->actlen);

 tr_setup:
	if (sc->sc_flag & UMODEM_FLAG_READ_STALL) {
	    usbd_transfer_start(sc->sc_xfer_data[3]);
	} else {
	    usbd_start_hardware(xfer);
	}
	return;
}

static void
umodem_read_clear_stall_callback(struct usbd_xfer *xfer)
{
	struct umodem_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_setup:
	/* start clear stall */
	usbd_clear_stall_tr_setup(xfer, sc->sc_xfer_data[1]);
	return;

 tr_transferred:
	usbd_clear_stall_tr_transferred(xfer, sc->sc_xfer_data[1]);
	sc->sc_flag &= ~UMODEM_FLAG_READ_STALL;
	usbd_transfer_start(sc->sc_xfer_data[1]);
	return;

 tr_error:
	sc->sc_flag &= ~UMODEM_FLAG_READ_STALL;
	DPRINTF(0, "clear stall failed, error=%s\n",
		usbd_errstr(xfer->error));
	return;
}


static void *
umodem_get_desc(struct usbd_device *udev, u_int8_t type, u_int8_t subtype)
{
	usb_config_descriptor_t *cd = usbd_get_config_descriptor(udev);
	usb_descriptor_t *desc = usbd_find_descriptor(cd, type, subtype);
	return desc;
}

static usbd_status
umodem_set_comm_feature(struct usbd_device *udev, u_int8_t iface_no, int feature, int state)
{
	usb_device_request_t req;
	usb_cdc_abstract_state_t ast;

	DPRINTF(0, "feature=%d state=%d\n", 
		feature, state);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_COMM_FEATURE;
	USETW(req.wValue, feature);
	USETW(req.wIndex, iface_no);
	USETW(req.wLength, UCDC_ABSTRACT_STATE_LENGTH);
	USETW(ast.wState, state);

	return usbd_do_request(udev, &req, &ast);
}

static int
umodem_detach(device_t dev)
{
	struct umodem_softc *sc = device_get_softc(dev);

	DPRINTF(0, "sc=%p\n", sc);

	ucom_detach(&(sc->sc_ucom));

	usbd_transfer_unsetup(sc->sc_xfer_intr, UMODEM_N_INTR_TRANSFER);

	usbd_transfer_unsetup(sc->sc_xfer_data, UMODEM_N_DATA_TRANSFER);

	return 0;
}