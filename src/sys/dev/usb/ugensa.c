/*	$NetBSD: ugensa.c,v 1.9.2.1 2007/03/24 14:55:50 yamt Exp $	*/

/*
 * Copyright (c) 2004, 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell <elric@netbsd.org>.
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
 * NOTE: all function names beginning like "ugensa_cfg_" can only
 * be called from within the config thread function !
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/termios.h>
#include <sys/serial.h>
#include <sys/malloc.h>

#include <dev/usb/usb_port.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_subr.h>

#include <dev/usb/ucomvar.h>

#include "usbdevs.h"

#define DPRINTF(...) { }

#define UGENSA_BUF_SIZE		2048 /* bytes */
#define UGENSA_N_TRANSFER	4 /* units */
#define UGENSA_CONFIG_INDEX	0
#define UGENSA_IFACE_INDEX	0

struct ugensa_softc {
	struct ucom_super_softc	sc_super_ucom;
	struct ucom_softc	sc_ucom;

	struct usbd_xfer 	*sc_xfer[UGENSA_N_TRANSFER];

	uint8_t			sc_flags;
#define UGENSA_FLAG_BULK_READ_STALL	0x01
#define UGENSA_FLAG_BULK_WRITE_STALL	0x02
};

/* prototypes */

static device_probe_t ugensa_probe;
static device_attach_t ugensa_attach;
static device_detach_t ugensa_detach;

static usbd_callback_t ugensa_bulk_write_callback;
static usbd_callback_t ugensa_bulk_write_clear_stall_callback;
static usbd_callback_t ugensa_bulk_read_callback;
static usbd_callback_t ugensa_bulk_read_clear_stall_callback;

static void ugensa_start_read(struct ucom_softc *ucom);
static void ugensa_stop_read(struct ucom_softc *ucom);
static void ugensa_start_write(struct ucom_softc *ucom);
static void ugensa_stop_write(struct ucom_softc *ucom);

static const struct usbd_config 
ugensa_xfer_config[UGENSA_N_TRANSFER] = {

    [0] = {
      .type      = UE_BULK,
      .endpoint  = -1, /* any */
      .direction = UE_DIR_OUT,
      .bufsize   = UGENSA_BUF_SIZE,
      .flags     = 0,
      .callback  = &ugensa_bulk_write_callback,
    },

    [1] = {
      .type      = UE_BULK,
      .endpoint  = -1, /* any */
      .direction = UE_DIR_IN,
      .bufsize   = UGENSA_BUF_SIZE,
      .flags     = USBD_SHORT_XFER_OK,
      .callback  = &ugensa_bulk_read_callback,
    },

    [2] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .flags     = (USBD_USE_DMA),
      .callback  = &ugensa_bulk_write_clear_stall_callback,
      .timeout   = 1000, /* 1 second */
    },

    [3] = {
      .type      = UE_CONTROL,
      .endpoint  = 0x00, /* Control pipe */
      .direction = -1,
      .bufsize   = sizeof(usb_device_request_t),
      .flags     = (USBD_USE_DMA),
      .callback  = &ugensa_bulk_read_clear_stall_callback,
      .timeout   = 1000, /* 1 second */
    },
};

static const struct ucom_callback ugensa_callback = {
    .ucom_start_read      = &ugensa_start_read,
    .ucom_stop_read       = &ugensa_stop_read,
    .ucom_start_write     = &ugensa_start_write,
    .ucom_stop_write      = &ugensa_stop_write,
};

static device_method_t ugensa_methods[] = { 
    /* Device methods */
    DEVMETHOD(device_probe, ugensa_probe),
    DEVMETHOD(device_attach, ugensa_attach),
    DEVMETHOD(device_detach, ugensa_detach),
    { 0, 0 }
};

static devclass_t ugensa_devclass;

static driver_t ugensa_driver = {
    .name    = "ugensa",
    .methods = ugensa_methods,
    .size    = sizeof(struct ugensa_softc),
};

DRIVER_MODULE(ugensa, uhub, ugensa_driver, ugensa_devclass, usbd_driver_load, 0);
MODULE_DEPEND(ugensa, usb, 1, 1, 1);
MODULE_DEPEND(ugensa, ucom, UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);

static const struct usb_devno ugensa_devs[] = {
	{ USB_VENDOR_AIRPRIME, USB_PRODUCT_AIRPRIME_PC5220 },
	{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_FLEXPACKGPS },
	{ USB_VENDOR_QUALCOMM_K, USB_PRODUCT_QUALCOMM_K_CDMA_MSM_K },
	{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD580 },
	{ USB_VENDOR_NOVATEL2, USB_PRODUCT_NOVATEL2_CDMA_MODEM },
	{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CDMA_MODEM1 },
};

#define ugensa_lookup(v, p) usb_lookup(ugensa_devs, v, p)

static int
ugensa_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->iface) {
	    return UMATCH_NONE;
	}

	return (ugensa_lookup(uaa->vendor, uaa->product) ? UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

static int
ugensa_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ugensa_softc *sc = device_get_softc(dev);
	int32_t error;

	if (sc == NULL) {
	    return ENOMEM;
	}

	usbd_set_desc(dev, uaa->device);

	/* Move the device into the configured state */
	error = usbd_set_config_index(uaa->device, UGENSA_CONFIG_INDEX, 1);
	if (error) {
	    device_printf(dev, "failed to set configuration, err=%s\n",
			  usbd_errstr(error));
	    goto detach;
	}

	error = usbd_transfer_setup
	  (uaa->device, UGENSA_IFACE_INDEX,
	   sc->sc_xfer, ugensa_xfer_config, UGENSA_N_TRANSFER,
	   sc, &Giant);

	if (error) {
	    device_printf(dev, "allocating control USB "
			  "transfers failed!\n");
	    goto detach;
	}

	/* clear stall at first run */
	sc->sc_flags |= (UGENSA_FLAG_BULK_WRITE_STALL|
			 UGENSA_FLAG_BULK_READ_STALL);

        error = ucom_attach(&(sc->sc_super_ucom), &(sc->sc_ucom), 1, sc,
			    &ugensa_callback, &Giant);
        if (error) {
            DPRINTF(0, "ucom_attach failed\n");
            goto detach;
        }

	return 0; /* success */

 detach:
	ugensa_detach(dev);
	return ENXIO; /* failure */
}

static int
ugensa_detach(device_t dev)
{
	struct ugensa_softc *sc = device_get_softc(dev);

        ucom_detach(&(sc->sc_super_ucom), &(sc->sc_ucom), 1);

        usbd_transfer_unsetup(sc->sc_xfer, UGENSA_N_TRANSFER);

	return 0;
}

static void
ugensa_bulk_write_callback(struct usbd_xfer *xfer)
{
	struct ugensa_softc *sc = xfer->priv_sc;
	u_int32_t actlen;

	USBD_CHECK_STATUS(xfer);

tr_error:
	if (xfer->error != USBD_CANCELLED) {
	    sc->sc_flags |= UGENSA_FLAG_BULK_WRITE_STALL;
	    usbd_transfer_start(sc->sc_xfer[2]);
	}
	return;

tr_setup:
tr_transferred:
	if (sc->sc_flags & UGENSA_FLAG_BULK_WRITE_STALL) {
	    usbd_transfer_start(sc->sc_xfer[2]);
	    return;
	}

	if (ucom_get_data(&(sc->sc_ucom), xfer->buffer, 
			  UGENSA_BUF_SIZE, &actlen)) {
	    xfer->length = actlen;
	    usbd_start_hardware(xfer);
	}
	return;
}

static void
ugensa_bulk_write_clear_stall_callback(struct usbd_xfer *xfer)
{
	struct ugensa_softc *sc = xfer->priv_sc;
	struct usbd_xfer *xfer_other = sc->sc_xfer[0];

	USBD_CHECK_STATUS(xfer);

 tr_setup:
	/* start clear stall */
	usbd_clear_stall_tr_setup(xfer, xfer_other);
	return;

 tr_transferred:
	usbd_clear_stall_tr_transferred(xfer, xfer_other);
	sc->sc_flags &= ~UGENSA_FLAG_BULK_WRITE_STALL;
	usbd_transfer_start(xfer_other);
	return;

 tr_error:
	sc->sc_flags &= ~UGENSA_FLAG_BULK_WRITE_STALL;
	DPRINTF(sc, 0, "clear stall failed, error=%s\n",
		usbd_errstr(xfer->error));
	return;
}

static void
ugensa_bulk_read_callback(struct usbd_xfer *xfer)
{
	struct ugensa_softc *sc = xfer->priv_sc;

	USBD_CHECK_STATUS(xfer);

 tr_error:
	if (xfer->error != USBD_CANCELLED) {
	    sc->sc_flags |= UGENSA_FLAG_BULK_READ_STALL;
	    usbd_transfer_start(sc->sc_xfer[3]);
	}
	return;

 tr_transferred:
	ucom_put_data(&(sc->sc_ucom), xfer->buffer, 
		      xfer->actlen);

 tr_setup:
	if (sc->sc_flags & UGENSA_FLAG_BULK_READ_STALL) {
	    usbd_transfer_start(sc->sc_xfer[3]);
	} else {
	    usbd_start_hardware(xfer);
	}
	return;
}

static void
ugensa_bulk_read_clear_stall_callback(struct usbd_xfer *xfer)
{
	struct ugensa_softc *sc = xfer->priv_sc;
	struct usbd_xfer *xfer_other = sc->sc_xfer[1];

	USBD_CHECK_STATUS(xfer);

 tr_setup:
	/* start clear stall */
	usbd_clear_stall_tr_setup(xfer, xfer_other);
	return;

 tr_transferred:
	usbd_clear_stall_tr_transferred(xfer, xfer_other);
	sc->sc_flags &= ~UGENSA_FLAG_BULK_READ_STALL;
	usbd_transfer_start(xfer_other);
	return;

 tr_error:
	sc->sc_flags &= ~UGENSA_FLAG_BULK_READ_STALL;
	DPRINTF(sc, 0, "clear stall failed, error=%s\n",
		usbd_errstr(xfer->error));
	return;
}

static void
ugensa_start_read(struct ucom_softc *ucom)
{
        struct ugensa_softc *sc = ucom->sc_parent;
	usbd_transfer_start(sc->sc_xfer[1]);
        return;
}

static void
ugensa_stop_read(struct ucom_softc *ucom)
{
        struct ugensa_softc *sc = ucom->sc_parent;
	usbd_transfer_stop(sc->sc_xfer[3]);
	usbd_transfer_stop(sc->sc_xfer[1]);
        return;
}

static void
ugensa_start_write(struct ucom_softc *ucom)
{
        struct ugensa_softc *sc = ucom->sc_parent;
	usbd_transfer_start(sc->sc_xfer[0]);
        return;
}

static void
ugensa_stop_write(struct ucom_softc *ucom)
{
        struct ugensa_softc *sc = ucom->sc_parent;
	usbd_transfer_stop(sc->sc_xfer[2]);
	usbd_transfer_stop(sc->sc_xfer[0]);
        return;
}