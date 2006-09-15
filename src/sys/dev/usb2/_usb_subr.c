/*      $NetBSD: usb_subr.c,v 1.99 2002/07/11 21:14:34 augustss Exp $   */

/* Also already have from NetBSD:
 *      $NetBSD: usb_subr.c,v 1.102 2003/01/01 16:21:50 augustss Exp $
 *      $NetBSD: usb_subr.c,v 1.103 2003/01/10 11:19:13 augustss Exp $
 *      $NetBSD: usb_subr.c,v 1.111 2004/03/15 10:35:04 augustss Exp $
 *      $NetBSD: usb_subr.c,v 1.114 2004/06/23 02:30:52 mycroft Exp $
 *      $NetBSD: usb_subr.c,v 1.115 2004/06/23 05:23:19 mycroft Exp $
 *      $NetBSD: usb_subr.c,v 1.116 2004/06/23 06:27:54 mycroft Exp $
 *      $NetBSD: usb_subr.c,v 1.119 2004/10/23 13:26:33 augustss Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/dev/usb2/usb_subr.c,v 1.86 2006/09/10 15:20:39 trhodes Exp $");

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/queue.h> /* LIST_XXX() */
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kthread.h>
#include <sys/unistd.h>

#include <dev/usb2/usb_port.h>
#include <dev/usb2/usb.h>
#include <dev/usb2/usb_subr.h>
#include <dev/usb2/usb_hid.h>
#include "usbdevs.h"
#include <dev/usb2/usb_quirks.h>

#ifdef USBVERBOSE
/*
 * Descriptions of of known vendors and devices ("products").
 */
struct usb_knowndev {
	u_int16_t		vendor;
	u_int16_t		product;
	int			flags;
	char			*vendorname, *productname;
};
#define	USB_KNOWNDEV_NOPROD	0x01		/* match on vendor only */

#include "usbdevs_data.h"
#endif /* USBVERBOSE */

static void
usbd_trim_spaces(char *p)
{
	char *q, *e;

	if(p == NULL)
		return;
	q = e = p;
	while (*q == ' ')	/* skip leading spaces */
		q++;
	while ((*p = *q++))	/* copy string */
		if (*p++ != ' ') /* remember last non-space */
			e = p;
	*e = 0;			/* kill trailing spaces */
	return;
}

static void
usbd_devinfo_vp(struct usbd_device *udev, char *v, char *p, int usedev)
{
	usb_device_descriptor_t *udd = &udev->ddesc;
	char *vendor, *product;
#ifdef USBVERBOSE
	const struct usb_knowndev *kdp;
#endif

	if(udev == NULL) {
		v[0] = p[0] = '\0';
		return;
	}

	vendor = NULL;
	product = NULL;

	if (usedev)
	{
		(void) usbreq_get_string_any
		  (udev, udd->iManufacturer, v, USB_MAX_STRING_LEN);

		vendor = v;
		usbd_trim_spaces(vendor);

		if(!vendor[0])
		{
			vendor = NULL;
		}

		(void) usbreq_get_string_any
		  (udev, udd->iProduct, p, USB_MAX_STRING_LEN);

		product = p;
		usbd_trim_spaces(product);

		if(!product[0])
		{
			product = NULL;
		}
	}
#ifdef USBVERBOSE
	if ((vendor == NULL) || (product == NULL)) {
		for(kdp = usb_knowndevs;
		    kdp->vendorname != NULL;
		    kdp++) {
			if ((kdp->vendor == UGETW(udd->idVendor)) &&
			   ((kdp->product == UGETW(udd->idProduct)) ||
			    (kdp->flags & USB_KNOWNDEV_NOPROD)))
				break;
		}
		if (kdp->vendorname != NULL) {
			if (vendor == NULL)
			    vendor = kdp->vendorname;
			if (product == NULL)
			    product = ((kdp->flags & USB_KNOWNDEV_NOPROD) == 0) ?
			        kdp->productname : NULL;
		}
	}
#endif
	if ((vendor != NULL) && *vendor)
		strcpy(v, vendor);
	else
		sprintf(v, "vendor 0x%04x", UGETW(udd->idVendor));
	if ((product != NULL) && *product)
		strcpy(p, product);
	else
		sprintf(p, "product 0x%04x", UGETW(udd->idProduct));
	return;
}

static int
usbd_printBCD(char *cp, int bcd)
{
	return (sprintf(cp, "%x.%02x", bcd >> 8, bcd & 0xff));
}

void
usbd_devinfo(struct usbd_device *udev, int showclass, 
	     char *dst_ptr, u_int16_t dst_len)
{
	usb_device_descriptor_t *udd = &udev->ddesc;
	char vendor[USB_MAX_STRING_LEN];
	char product[USB_MAX_STRING_LEN];
	u_int16_t bcdDevice, bcdUSB;

	usbd_devinfo_vp(udev, vendor, product, 1);

	bcdUSB = UGETW(udd->bcdUSB);
	bcdDevice = UGETW(udd->bcdDevice);

	if(showclass)
	{
	    snprintf(dst_ptr, dst_len, "%s %s, class %d/%d, rev %x.%02x/"
		     "%x.%02x, addr %d", vendor, product,
		     udd->bDeviceClass, udd->bDeviceSubClass,
		     (bcdUSB >> 8), bcdUSB & 0xFF,
		     (bcdDevice >> 8), bcdDevice & 0xFF,
		     udev->address);
	}
	else
	{
	    snprintf(dst_ptr, dst_len, "%s %s, rev %x.%02x/"
		     "%x.%02x, addr %d", vendor, product,
		     (bcdUSB >> 8), bcdUSB & 0xFF,
		     (bcdDevice >> 8), bcdDevice & 0xFF,
		     udev->address);
	}
	return;
}

const char * 
usbd_errstr(usbd_status err)
{
	static const char * const
	  MAKE_TABLE(USBD_STATUS,DESC,[]);

	return (err < N_USBD_STATUS) ?
	  USBD_STATUS_DESC[err] : "unknown error!";
}

/* Delay for a certain number of ms */
void
usb_delay_ms(struct usbd_bus *bus, u_int ms)
{
	/* Wait at least two clock ticks so we know the time has passed. */
	if (bus->use_polling || cold)
		DELAY((ms+1) * 1000);
	else
		tsleep(&ms, PRIBIO, "usbdly", (((ms*hz)+999)/1000) + 1);
}

/* Delay given a device handle. */
void
usbd_delay_ms(struct usbd_device *udev, u_int ms)
{
	usb_delay_ms(udev->bus, ms);
}

#define ADD_BYTES(ptr,len) ((void *)(((u_int8_t *)(ptr)) + (len)))

usb_descriptor_t *
usbd_desc_foreach(usb_config_descriptor_t *cd, usb_descriptor_t *desc)
{
	void *end;

	if (cd == NULL) {
	    return NULL;
	}

	end = ADD_BYTES(cd, UGETW(cd->wTotalLength));

	if (desc == NULL) {
	    desc = ADD_BYTES(cd, 0);
	} else {
	    desc = ADD_BYTES(desc, desc->bLength);
	}
	return (((((void *)desc) >= ((void *)cd)) &&
		 (((void *)desc) < end) &&
		 (ADD_BYTES(desc,desc->bLength) >= ((void *)cd)) &&
		 (ADD_BYTES(desc,desc->bLength) <= end) &&
		 (desc->bLength >= sizeof(*desc))) ? desc : NULL);
}

usb_hid_descriptor_t *
usbd_get_hdesc(usb_config_descriptor_t *cd, usb_interface_descriptor_t *id)
{
	usb_descriptor_t *desc = (void *)id;

	if(desc == NULL) {
	    return NULL;
	}

	while ((desc = usbd_desc_foreach(cd, desc)))
	{
		if ((desc->bDescriptorType == UDESC_HID) &&
		    (desc->bLength >= USB_HID_DESCRIPTOR_SIZE(0)))
		{
			return (void *)desc;
		}

		if (desc->bDescriptorType == UDESC_INTERFACE)
		{
			break;
		}
	}
	return NULL;
}

usb_interface_descriptor_t *
usbd_find_idesc(usb_config_descriptor_t *cd, u_int16_t iface_index, 
		u_int16_t alt_index)
{
	usb_descriptor_t *desc = NULL;
	usb_interface_descriptor_t *id;
	u_int16_t curidx = 0xFFFF;
	u_int16_t lastidx = 0xFFFF;
	u_int16_t curaidx = 0;

	while ((desc = usbd_desc_foreach(cd, desc)))
	{
	    if ((desc->bDescriptorType == UDESC_INTERFACE) &&
		(desc->bLength >= sizeof(*id)))
	    {
	        id = (void *)desc;

		if(id->bInterfaceNumber != lastidx)
		{
		    lastidx = id->bInterfaceNumber;
		    curidx++;
		    curaidx = 0;
		}
		else
		{
		    curaidx++;
		}
		if((iface_index == curidx) && (alt_index == curaidx))
		{
		    return (id);
		}
	    }
	}
	return (NULL);
}

usb_endpoint_descriptor_t *
usbd_find_edesc(usb_config_descriptor_t *cd, u_int16_t iface_index, 
		u_int16_t alt_index, u_int16_t endptidx)
{
	usb_descriptor_t *desc = NULL;
	usb_interface_descriptor_t *d;
	u_int16_t curidx = 0;

	d = usbd_find_idesc(cd, iface_index, alt_index);
	if (d == NULL)
	    return NULL;

	if (endptidx >= d->bNumEndpoints) /* quick exit */
	    return NULL;

	desc = ((void *)d);

	while ((desc = usbd_desc_foreach(cd, desc))) {

	    if(desc->bDescriptorType == UDESC_INTERFACE) {
	        break;
	    }

	    if (desc->bDescriptorType == UDESC_ENDPOINT) {

	        if (curidx == endptidx) {
		    return ((desc->bLength >= USB_ENDPOINT_DESCRIPTOR_SIZE) ? 
			    ((void *)d) : NULL);
		}
		curidx++;
	    }
	}
	return (NULL);
}

usb_descriptor_t *
usbd_find_descriptor(usb_config_descriptor_t *cd, int type, int subtype)
{
	usb_descriptor_t *desc = NULL;

	while ((desc = usbd_desc_foreach(cd, desc))) {

		if((desc->bDescriptorType == type) &&
		   ((subtype == USBD_SUBTYPE_ANY) ||
		    (subtype == desc->bDescriptorSubtype)))
		{
			return desc;
		}
	}
	return (NULL);
}

int
usbd_get_no_alts(usb_config_descriptor_t *cd, u_int8_t ifaceno)
{
	usb_descriptor_t *desc = NULL;
	usb_interface_descriptor_t *id;
	int n = 0;

	while ((desc = usbd_desc_foreach(cd, desc))) {

	    if ((desc->bDescriptorType == UDESC_INTERFACE) &&
		(desc->bLength >= sizeof(*id))) {
	        id = (void *)desc;
		if (id->bInterfaceNumber == ifaceno) {
		    n++;
		}
	    }
	}
	return n;
}

static void
usbd_fill_pipe_data(struct usbd_device *udev, u_int8_t iface_index,
		    usb_endpoint_descriptor_t *edesc, struct usbd_pipe *pipe)
{
	bzero(pipe, sizeof(*pipe));
#ifdef USB_COMPAT_OLD
	pipe->udev = udev;
#endif
	pipe->edesc = edesc;
	pipe->iface_index = iface_index;
	LIST_INIT(&pipe->list_head);

	/* first transfer needs to clear stall! */
	pipe->clearstall = 1;

	(udev->bus->methods->pipe_init)(udev,edesc,pipe);
	return;
}

/* NOTE: pipes should not be in use when
 * ``usbd_free_pipe_data()'' is called
 */
static void
usbd_free_pipe_data(struct usbd_device *udev, int iface_index)
{
	struct usbd_pipe *pipe = &udev->pipes[0];
	struct usbd_pipe *pipe_end = &udev->pipes_end[0];

	while(pipe < pipe_end)
	{
		if((iface_index == pipe->iface_index) ||
		   (iface_index == -1))
		{
			/* free pipe */
			pipe->edesc = NULL;
		}
		pipe++;
	}
	return;
}

usbd_status
usbd_fill_iface_data(struct usbd_device *udev, int iface_index, int alt_index)
{
	struct usbd_interface *iface = usbd_get_iface(udev,iface_index);
	struct usbd_pipe *pipe = &udev->pipes[0];
	struct usbd_pipe *pipe_end = &udev->pipes_end[0];
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed = NULL;
	usb_descriptor_t *desc;
	u_int8_t nendpt;

	if(iface == NULL)
	{
		return (USBD_INVAL);
	}

	PRINTFN(4,("iface_index=%d alt_index=%d\n",
		    iface_index, alt_index));

	/* mtx_assert() */

	while(pipe < pipe_end)
	{
		if(pipe->iface_index == iface_index)
		{
			if(pipe->refcount)
			{
				return(USBD_IN_USE);
			}
		}
		pipe++;
	}

	pipe = &udev->pipes[0];

	/* free old pipes if any */
	usbd_free_pipe_data(udev, iface_index);

	id = usbd_find_idesc(udev->cdesc, iface_index, alt_index);
	if(id == NULL)
	{
		return (USBD_INVAL);
	}
#ifdef USB_COMPAT_OLD
	iface->udev = udev;
#endif
	iface->idesc = id;
	iface->alt_index = alt_index;

	USBD_CLR_IFACE_NO_PROBE(udev, iface_index);

	nendpt = id->bNumEndpoints;
	PRINTFN(4,("found idesc nendpt=%d\n", nendpt));

	desc = (void *)id;

	while(nendpt--)
	{
		PRINTFN(10,("endpt=%d\n", nendpt));

		while ((desc = usbd_desc_foreach(udev->cdesc, desc)))
		{
			if ((desc->bDescriptorType == UDESC_ENDPOINT) &&
			    (desc->bLength >= USB_ENDPOINT_DESCRIPTOR_SIZE))
			{
				goto found;
			}
			if (desc->bDescriptorType == UDESC_INTERFACE)
			{
				break;
			}
		}
		goto error;

	found:
		ed = (void *)desc;

		if(udev->speed == USB_SPEED_HIGH)
		{
			u_int16_t mps;
			/* control and bulk endpoints have max packet limits */
			switch (UE_GET_XFERTYPE(ed->bmAttributes)) {
			case UE_CONTROL:
				mps = USB_2_MAX_CTRL_PACKET;
				goto check;
			case UE_BULK:
				mps = USB_2_MAX_BULK_PACKET;
			check:
				if(UGETW(ed->wMaxPacketSize) != mps)
				{
					USETW(ed->wMaxPacketSize, mps);
#ifdef DIAGNOSTIC
					printf("%s: bad wMaxPacketSize, addr=%d!\n",
					       __FUNCTION__, udev->address);
#endif
				}
				break;
			default:
				break;
			}
		}
		if(UGETW(ed->wMaxPacketSize) == 0)
		{
#ifdef USB_DEBUG
			printf("%s: invalid wMaxPacketSize, addr=%d!\n",
			     __FUNCTION__, udev->address);
#endif
		      /* avoid division by zero 
		       * (in EHCI/UHCI/OHCI drivers) 
		       */
		      USETW(ed->wMaxPacketSize, USB_MAX_IPACKET);
		}

		/* find a free pipe */
		while(pipe < pipe_end)
		{
			if(pipe->edesc == NULL)
			{
				/* pipe is free */
				usbd_fill_pipe_data(udev,iface_index,ed,pipe);
				break;
			}
			pipe++;
		}
	}
	return (USBD_NORMAL_COMPLETION);

 error:
	/* passed end, or bad desc */
	printf("%s: bad descriptor(s), addr=%d!\n",
	       __FUNCTION__, udev->address);

	/* free old pipes if any */
	usbd_free_pipe_data(udev, iface_index);
	return (USBD_INVAL);
}

static void
usbd_free_iface_data(struct usbd_device *udev)
{
	struct usbd_interface *iface = &udev->ifaces[0];
	struct usbd_interface *iface_end = &udev->ifaces_end[0];

	/* mtx_assert() */

	/* free all pipes, if any */
	usbd_free_pipe_data(udev, -1);

	/* free all interfaces, if any */
	while(iface < iface_end)
	{
		iface->idesc = NULL;
		iface++;
	}

	if(udev->cdesc != NULL)
	{
		/* free "cdesc" after "ifaces" */
		free(udev->cdesc, M_USB);
	}
	udev->cdesc = NULL;
	udev->config = USB_UNCONFIG_NO;
	return;
}

/* - USB config 0
 *   - USB interfaces
 *     - USB alternative interfaces
 *       - USB pipes
 *
 * - USB config 1
 *   - USB interfaces
 *     - USB alternative interfaces
 *       - USB pipes
 */
usbd_status
usbd_search_and_set_config(struct usbd_device *udev, int no, int msg)
{
	usb_config_descriptor_t cd;
	usbd_status err;
	int index;

	if(no == USB_UNCONFIG_NO)
	{
		return (usbd_set_config_index(udev, USB_UNCONFIG_INDEX, msg));
	}

	PRINTFN(5,("%d\n", no));

	/* figure out what config index to use */
	for(index = 0; 
	    index < udev->ddesc.bNumConfigurations;
	    index++)
	{
		err = usbreq_get_config_desc(udev, index, &cd);
		if(err)
		{
			return (err);
		}
		if(cd.bConfigurationValue == no)
		{
			return (usbd_set_config_index(udev, index, msg));
		}
	}
	return (USBD_INVAL);
}

usbd_status
usbd_set_config_index(struct usbd_device *udev, int index, int msg)
{
	usb_status_t ds;
	usb_hub_descriptor_t hd;
	usb_config_descriptor_t cd, *cdp;
	usbd_status err;
	int nifc, len, selfpowered, power;

	PRINTFN(5,("udev=%p index=%d\n", udev, index));

	if(index == USB_UNCONFIG_INDEX)
	{
		/* leave unallocated when
		 * unconfiguring the device
		 */
		err = usbreq_set_config(udev, USB_UNCONFIG_NO);
		goto error;
	}

	/* get the short descriptor */
	err = usbreq_get_config_desc(udev, index, &cd);
	if(err)
	{
		goto error;
	}

	/* free all configuration data structures */
	usbd_free_iface_data(udev);

	/* get full descriptor */
	len = UGETW(cd.wTotalLength);
	udev->cdesc = malloc(len, M_USB, M_NOWAIT|M_ZERO);
	if(udev->cdesc == NULL)
	{
		return (USBD_NOMEM);
	}

	cdp = udev->cdesc;

	/* get the full descriptor */
	err = usbreq_get_desc(udev, UDESC_CONFIG, index, len, cdp, 3);
	if (err)
		goto error;
	if(cdp->bDescriptorType != UDESC_CONFIG)
	{
		PRINTF(("bad desc %d\n",
			     cdp->bDescriptorType));
		err = USBD_INVAL;
		goto error;
	}
	if(cdp->bNumInterface > (sizeof(udev->ifaces)/sizeof(udev->ifaces[0])))
	{
		PRINTF(("too many interfaces: %d\n", cdp->bNumInterface));
		cdp->bNumInterface = (sizeof(udev->ifaces)/sizeof(udev->ifaces[0]));
	}

	/* Figure out if the device is self or bus powered. */
	selfpowered = 0;
	if (!(udev->quirks->uq_flags & UQ_BUS_POWERED) &&
	    (cdp->bmAttributes & UC_SELF_POWERED)) {
		/* May be self powered. */
		if (cdp->bmAttributes & UC_BUS_POWERED) {
			/* Must ask device. */
			if (udev->quirks->uq_flags & UQ_POWER_CLAIM) {
				/*
				 * Hub claims to be self powered, but isn't.
				 * It seems that the power status can be
				 * determined by the hub characteristics.
				 */
				err = usbreq_get_hub_descriptor(udev, &hd);

				if(!err &&
				   (UGETW(hd.wHubCharacteristics) &
				    UHD_PWR_INDIVIDUAL))
				{
					selfpowered = 1;
				}
				PRINTF(("characteristics=0x%04x, error=%s\n",
					 UGETW(hd.wHubCharacteristics),
					 usbd_errstr(err)));
			} else {
				err = usbreq_get_device_status(udev, &ds);
				if(!err &&
				   (UGETW(ds.wStatus) & UDS_SELF_POWERED))
				{
					selfpowered = 1;
				}
				PRINTF(("status=0x%04x, error=%s\n",
					 UGETW(ds.wStatus), usbd_errstr(err)));
			}
		} else
			selfpowered = 1;
	}
	PRINTF(("udev=%p cdesc=%p (addr %d) cno=%d attr=0x%02x, "
		 "selfpowered=%d, power=%d\n",
		 udev, cdp,
		 cdp->bConfigurationValue, udev->address, cdp->bmAttributes,
		 selfpowered, cdp->bMaxPower * 2));

	/* Check if we have enough power. */
	power = cdp->bMaxPower * 2;
	if(power > udev->powersrc->power)
	{
		PRINTF(("power exceeded %d %d\n", power, udev->powersrc->power));
		/* XXX print nicer message */
		if(msg)
		{
			device_printf(udev->bus->bdev,
				      "device addr %d (config %d) exceeds power "
				      "budget, %d mA > %d mA\n",
				      udev->address,
				      cdp->bConfigurationValue,
				      power, udev->powersrc->power);
		}
		err = USBD_NO_POWER;
		goto error;
	}

	udev->power = power;
	udev->self_powered = selfpowered;
	udev->config = cdp->bConfigurationValue;

	/* Set the actual configuration value. */
	err = usbreq_set_config(udev, cdp->bConfigurationValue);
	if(err)
	{
		goto error;
	}

	/* Allocate and fill interface data. */
	nifc = cdp->bNumInterface;
	while(nifc--)
	{
		err = usbd_fill_iface_data(udev, nifc, 0);
		if (err)
		{
			goto error;
		}
	}

	return (USBD_NORMAL_COMPLETION);

 error:
	PRINTF(("error=%s\n", usbd_errstr(err)));
	usbd_free_iface_data(udev);
	return (err);
}

int
usbd_fill_deviceinfo(struct usbd_device *udev, struct usb_device_info *di,
		     int usedev)
{
	struct usbd_port *p;
	int i, err, s;

	if((udev == NULL) || (di == NULL))
	{
		return (ENXIO);
	}

	bzero(di, sizeof(di[0]));

	di->udi_bus = device_get_unit(udev->bus->bdev);
	di->udi_addr = udev->address;
	di->udi_cookie = udev->cookie;
	usbd_devinfo_vp(udev, di->udi_vendor, di->udi_product, usedev);
	usbd_printBCD(di->udi_release, UGETW(udev->ddesc.bcdDevice));
	di->udi_vendorNo = UGETW(udev->ddesc.idVendor);
	di->udi_productNo = UGETW(udev->ddesc.idProduct);
	di->udi_releaseNo = UGETW(udev->ddesc.bcdDevice);
	di->udi_class = udev->ddesc.bDeviceClass;
	di->udi_subclass = udev->ddesc.bDeviceSubClass;
	di->udi_protocol = udev->ddesc.bDeviceProtocol;
	di->udi_config = udev->config;
	di->udi_power = udev->self_powered ? 0 : udev->power;
	di->udi_speed = udev->speed;

	for(i = 0;
	    (i < (sizeof(udev->subdevs)/sizeof(udev->subdevs[0]))) &&
	      (i < USB_MAX_DEVNAMES);
	    i++)
	{
		if(udev->subdevs[i] &&
		   device_is_attached(udev->subdevs[i]))
		{
			strncpy(di->udi_devnames[i],
				device_get_nameunit(udev->subdevs[i]),
				USB_MAX_DEVNAMELEN);
			di->udi_devnames[i][USB_MAX_DEVNAMELEN-1] = 0;
		}
		/* else { di->udi_devnames[i][0] = 0; } */
	}

	if(udev->hub)
	{
		for(i = 0;
		    (i < (sizeof(di->udi_ports)/sizeof(di->udi_ports[0]))) &&
		      (i < udev->hub->hubdesc.bNbrPorts);
		    i++)
		{
			p = &udev->hub->ports[i];
			if(p->device)
			{
				err = p->device->address;
			}
			else
			{
				s = UGETW(p->status.wPortStatus);
				if (s & UPS_PORT_ENABLED)
				{
					err = USB_PORT_ENABLED;
				}
				else if (s & UPS_SUSPEND)
				{
					err = USB_PORT_SUSPENDED;
				}
				else if (s & UPS_PORT_POWER)
				{
					err = USB_PORT_POWERED;
				}
				else
				{
					err = USB_PORT_DISABLED;
				}
			}
			di->udi_ports[i] = err;
		}
		di->udi_nports = udev->hub->hubdesc.bNbrPorts;
	}
	return 0;
}

/* "usbd_probe_and_attach()" is called 
 * from "usbd_new_device()" and "uhub_explore()"
 */
usbd_status
usbd_probe_and_attach(device_t parent, int port, struct usbd_port *up)
{
	struct usb_attach_arg uaa;
	struct usbd_device *udev = up->device;
	device_t bdev = NULL;
	usbd_status err = 0;
	u_int8_t config;
	u_int8_t i;

	up->last_refcount = usb_driver_added_refcount;

	if(udev == NULL)
	{
		PRINTF(("%s: port %d has no device\n", 
			device_get_nameunit(parent), port));
		return (USBD_INVAL);
	}

	bzero(&uaa, sizeof(uaa));

	/* probe and attach */

	uaa.device = udev;
	uaa.port = port;
	uaa.configno = -1;
	uaa.vendor = UGETW(udev->ddesc.idVendor);
	uaa.product = UGETW(udev->ddesc.idProduct);
	uaa.release = UGETW(udev->ddesc.bcdDevice);

	if((udev->probed == USBD_PROBED_SPECIFIC_AND_FOUND) ||
	   (udev->probed == USBD_PROBED_GENERIC_AND_FOUND))
	{
		/* nothing more to probe */
		goto done;
	}

	bdev = device_add_child(parent, NULL, -1);
	if(!bdev)
	{
		device_printf(udev->bus->bdev,
			      "Device creation failed\n");
		err = USBD_INVAL;
		goto done;
	}

	device_set_ivars(bdev, &uaa);
	device_quiet(bdev);

	if(udev->probed == USBD_PROBED_NOTHING)
	{
		/* first try device specific drivers */
		PRINTF(("trying device specific drivers\n"));

		if(device_probe_and_attach(bdev) == 0)
		{
			device_set_ivars(bdev, NULL); /* no longer accessible */
			udev->subdevs[0] = bdev;
			udev->probed = USBD_PROBED_SPECIFIC_AND_FOUND;
			bdev = 0;
			goto done;
		}

		PRINTF(("no device specific driver found; "
			"looping over %d configurations\n",
			udev->ddesc.bNumConfigurations));
	}

	/* next try interface drivers */

	if((udev->probed == USBD_PROBED_NOTHING) ||
	   (udev->probed == USBD_PROBED_IFACE_AND_FOUND))
	{
	  for(config = 0; config < udev->ddesc.bNumConfigurations; config++)
	  {
		struct usbd_interface *iface;

		/* only set config index the first 
		 * time the devices are probed
		 */
		if(udev->probed == USBD_PROBED_NOTHING)
		{
			err = usbd_set_config_index(udev, config, 1);
			if(err)
			{
			    device_printf(parent,
					  "port %d, set config at addr %d "
					  "failed, error=%s\n",
					  port, udev->address, 
					  usbd_errstr(err));
			    goto done;
			}

			/* ``bNumInterface'' is checked 
			 * by ``usbd_set_config_index()''
			 *
			 * ``USBD_CLR_IFACE_NO_PROBE()'' is run
			 * by ``usbd_fill_iface_data()'', which
			 * is called by ``usbd_set_config_index()''
			 */
		}

		/*
		 * else the configuration is already set
		 */

		uaa.configno = udev->cdesc->bConfigurationValue;
		uaa.ifaces_start = &udev->ifaces[0];
		uaa.ifaces_end = &udev->ifaces[udev->cdesc->bNumInterface];
#ifdef USB_COMPAT_OLD
		uaa.nifaces = udev->cdesc->bNumInterface;

		for(i = 0; i < uaa.nifaces; i++)
		{
			if(USBD_GET_IFACE_NO_PROBE(udev, i))
				uaa.ifaces[i] = NULL;
			else
				uaa.ifaces[i] = &udev->ifaces[i];
		}
#endif
		for(iface = uaa.ifaces_start;
		    iface < uaa.ifaces_end;
		    iface++)
		{
			uaa.iface = iface;
			uaa.iface_index = (i = (iface - &udev->ifaces[0]));

			if(uaa.iface_index >= (sizeof(udev->subdevs)/
					       sizeof(udev->subdevs[0])))
			{
				device_printf(udev->bus->bdev,
					      "Too many subdevices\n");
				break;
			}
#ifdef USB_COMPAT_OLD
			if(uaa.ifaces[i])
#endif
			if((USBD_GET_IFACE_NO_PROBE(udev, i) == 0) &&
			   (udev->subdevs[i] == NULL) &&
			   (device_probe_and_attach(bdev) == 0))
			{
				/* "ivars" are no longer accessible: */
				device_set_ivars(bdev, NULL); 
				udev->subdevs[i] = bdev;
				udev->probed = USBD_PROBED_IFACE_AND_FOUND;
				bdev = 0;

				/* create another child for the next iface [if any] */
				bdev = device_add_child(parent, NULL, -1);
				if(!bdev)
				{
					device_printf(udev->bus->bdev,
						      "Device creation failed\n");

					/* need to update "IFACE_NO_PROBE": */
					break; 
				}
				device_set_ivars(bdev, &uaa);
				device_quiet(bdev);
			}
		}

		if(udev->probed == USBD_PROBED_IFACE_AND_FOUND)
		{
#ifdef USB_COMPAT_OLD
			uaa.nifaces = udev->cdesc->bNumInterface;

			for(i = 0; i < uaa.nifaces; i++)
			{
				/* mark ifaces that should
				 * not be probed
				 */
				if(uaa.ifaces[i] == NULL)
				{
					USBD_SET_IFACE_NO_PROBE(udev, i);
				}
			}
#endif
			break;
		}
	  }
	}

	if(udev->probed == USBD_PROBED_NOTHING)
	{
		/* set config index 0 */

		err = usbd_set_config_index(udev, 0, 1);
		if(err)
		{
		    device_printf(parent,
				  "port %d, set config at addr %d "
				  "failed, error=%s\n",
				  port, udev->address, 
				  usbd_errstr(err));
		    goto done;
		}

		PRINTF(("no interface drivers found\n"));

		/* finally try the generic driver */
		uaa.iface = NULL;
		uaa.iface_index = 0;
		uaa.ifaces_start = NULL;
		uaa.ifaces_end = NULL;
		uaa.usegeneric = 1;
		uaa.configno = -1;

		if(device_probe_and_attach(bdev) == 0)
		{
			device_set_ivars(bdev, NULL); /* no longer accessible */
			udev->subdevs[0] = bdev;
			udev->probed = USBD_PROBED_GENERIC_AND_FOUND;
			bdev = 0;
			goto done;
		}

		/*
		 * Generic attach failed. 
		 * The device is left as it is.
		 * It has no driver, but is fully operational.
		 */
		PRINTF(("generic attach failed\n"));
	}
 done:
	if(bdev)
	{
		/* remove the last created child; it is unused */
		device_delete_child(parent, bdev);
	}
	return err;
}

/*
 * Called when a new device has been put in the powered state,
 * but not yet in the addressed state.
 * Get initial descriptor, set the address, get full descriptor,
 * and attach a driver.
 */
usbd_status
usbd_new_device(device_t parent, struct usbd_bus *bus, int depth,
		int speed, int port, struct usbd_port *up)
{
	struct usbd_device *adev;
	struct usbd_device *udev;
	struct usbd_device *hub;
	usb_port_status_t ps;
	usbd_status err = 0;
	int addr;
	int i;

	PRINTF(("bus=%p port=%d depth=%d speed=%d\n",
		 bus, port, depth, speed));

	/* find unused address */
	addr = USB_MAX_DEVICES;
#if (USB_MAX_DEVICES == 0)
#error "(USB_MAX_DEVICES == 0)"
#endif
	while(addr--)
	{
		if(addr == 0)
		{
			/* address 0 is always unused */
			device_printf(bus->bdev,
				      "No free USB addresses, "
				      "new device ignored.\n");
			return (USBD_NO_ADDR);
		}
		if(bus->devices[addr] == 0)
		{
			break;
		}
	}

	udev = malloc(sizeof(udev[0]), M_USB, M_NOWAIT|M_ZERO);
	if(udev == NULL)
	{
		return (USBD_NOMEM);
	}

	up->device = udev;

	/* set up default endpoint descriptor */
	udev->default_ep_desc.bLength = USB_ENDPOINT_DESCRIPTOR_SIZE;
	udev->default_ep_desc.bDescriptorType = UDESC_ENDPOINT;
	udev->default_ep_desc.bEndpointAddress = USB_CONTROL_ENDPOINT;
	udev->default_ep_desc.bmAttributes = UE_CONTROL;
	USETW(udev->default_ep_desc.wMaxPacketSize, USB_MAX_IPACKET);
	udev->default_ep_desc.bInterval = 0;

	udev->bus = bus;
	udev->quirks = &usbd_no_quirk;
	udev->address = USB_START_ADDR;
	udev->ddesc.bMaxPacketSize = 0;
	udev->depth = depth;
	udev->powersrc = up;
	udev->myhub = up->parent;

	hub = up->parent;

	if(hub)
	{
		if(speed > hub->speed)
		{
#ifdef USB_DEBUG
			printf("%s: maxium speed of attached "
			       "device, %d, is higher than speed "
			       "of parent HUB, %d.\n",
			       __FUNCTION__, speed, hub->speed);
#endif
			/* speed down 
			 * (else there is trouble setting 
			 *  up the right transfer methods)
			 */
			speed = hub->speed;
		}
	}

	adev = udev;
	while(hub && (hub->speed != USB_SPEED_HIGH))
	{
		adev = hub;
		hub = hub->myhub;
	}

	if(hub)
	{
		for(i = 0; i < hub->hub->hubdesc.bNbrPorts; i++)
		{
			if(hub->hub->ports[i].device == adev)
			{
				udev->myhsport = &hub->hub->ports[i];
				break;
			}
		}
	}

	udev->speed = speed;
	udev->langid = USBD_NOLANG;

	/* usb_cookie_no is used by "usb.c" */

	static u_int32_t usb_cookie_no = 0;

	udev->cookie.cookie = ++usb_cookie_no;

	/* init the default pipe */
	usbd_fill_pipe_data(udev, 0,
			    &udev->default_ep_desc,
			    &udev->default_pipe);

	/* Set the address.  Do this early; some devices need that.
	 * Try a few times in case the device is slow (i.e. outside specs.)
	 */
	for (i = 0; i < 15; i++)
	{
		err = usbreq_set_address(udev, addr);
		if(!err)
		{
			break;
		}
		usbd_delay_ms(udev, 200);
		if(((i & 3) == 3) && (up->parent))
		{
			PRINTF(("set address %d "
				 "failed - trying a port reset\n", addr));
			usbreq_reset_port(up->parent, port, &ps);
		}
	}
	if(err)
	{
		PRINTF(("set address %d failed\n", addr));
		err = USBD_SET_ADDR_FAILED;
		goto done;
	}

	/* allow device time to set new address */
	usbd_delay_ms(udev, USB_SET_ADDRESS_SETTLE);
	udev->address = addr;	/* new device address now */
	bus->devices[addr] = udev;

	/* get the first 8 bytes of the device descriptor */
	err = usbreq_get_desc(udev, UDESC_DEVICE, 0, USB_MAX_IPACKET, &udev->ddesc, 0);
	if(err)
	{
		PRINTF(("addr=%d, getting first desc failed\n",
			 udev->address));
		goto done;
	}

	if(speed == USB_SPEED_HIGH)
	{
		/* max packet size must be 64 (sec 5.5.3) */
		if(udev->ddesc.bMaxPacketSize != USB_2_MAX_CTRL_PACKET)
		{
#ifdef DIAGNOSTIC
			printf("%s: addr=%d bad max packet size\n",
			       __FUNCTION__, udev->address);
#endif
			udev->ddesc.bMaxPacketSize = USB_2_MAX_CTRL_PACKET;
		}
	}

	if(udev->ddesc.bMaxPacketSize == 0)
	{
#ifdef USB_DEBUG
		printf("%s: addr=%d invalid bMaxPacketSize!\n",
		       __FUNCTION__, udev->address);
#endif
		/* avoid division by zero */
		udev->ddesc.bMaxPacketSize = USB_MAX_IPACKET;
	}

	PRINTF(("adding unit addr=%d, rev=%02x, class=%d, "
		 "subclass=%d, protocol=%d, maxpacket=%d, len=%d, speed=%d\n",
		 udev->address, UGETW(udev->ddesc.bcdUSB),
		 udev->ddesc.bDeviceClass,
		 udev->ddesc.bDeviceSubClass,
		 udev->ddesc.bDeviceProtocol,
		 udev->ddesc.bMaxPacketSize,
		 udev->ddesc.bLength,
		 udev->speed));

	if(udev->ddesc.bDescriptorType != UDESC_DEVICE)
	{
		/* illegal device descriptor */
		PRINTF(("illegal descriptor %d\n",
			 udev->ddesc.bDescriptorType));
		err = USBD_INVAL;
		goto done;
	}
	if(udev->ddesc.bLength < USB_DEVICE_DESCRIPTOR_SIZE)
	{
		PRINTF(("bad length %d\n",
			 udev->ddesc.bLength));
		err = USBD_INVAL;
		goto done;
	}

	USETW(udev->default_ep_desc.wMaxPacketSize, udev->ddesc.bMaxPacketSize);

	/* get the full device descriptor */
	err = usbreq_get_device_desc(udev, &udev->ddesc);
	if(err)
	{
		PRINTF(("addr=%d, getting full desc failed\n",
			 udev->address));
		goto done;
	}

	/* figure out what's wrong with this device */
	udev->quirks = usbd_find_quirk(&udev->ddesc);

	/* assume 100mA bus powered for now. Changed when configured. */
	udev->power = USB_MIN_POWER;
	udev->self_powered = 0;

	/* buffer serial number */
	(void) usbreq_get_string_any
	  (udev, udev->ddesc.iSerialNumber, 
	   &udev->serial[0], sizeof(udev->serial));

	/* check serial number format */

	for(i = 0;;i++)
	{
		if(udev->serial[i] == '\0') break;
		if(udev->serial[i] == '\"') udev->serial[i] = ' ';
		if(udev->serial[i] == '\n') udev->serial[i] = ' ';
	}

	PRINTF(("new dev (addr %d), udev=%p, parent=%p\n",
		 udev->address, udev, parent));

	err = usbd_probe_and_attach(parent, port, up);

 done:
	if(err)
	{
		/* remove device and sub-devices */
		usbd_free_device(up, 1);
	}
	else
	{
		usbd_add_dev_event(USB_EVENT_DEVICE_ATTACH, udev);
	}
	return(err);
}

/* called when a port has been disconnected
 *
 * The general mechanism for detaching:
 *
 * The drivers should use a static softc or a softc which is not freed
 * immediately, so that calls to routines which are about to access
 * the softc, does not access freed memory.
 *
 * The drivers mutex should also be available for some time after
 * detach.
 *
 * The drivers should have a detach flag which is set when the driver
 * is detached. The detach flag is checked after locking drivers mutex
 * and after waking up from sleep. When the detach flag is set, the
 * driver must unlock drivers mutex and exit.
 */
void
usbd_free_device(struct usbd_port *up, u_int8_t free_subdev)
{
	struct usbd_device *udev = up->device;
	device_t *subdev = &udev->subdevs[0];
	device_t *subdev_end = &udev->subdevs_end[0];
	int error = 0;

	/* mtx_assert() */

	if(udev == NULL)
	{
		/* already freed */
		return;
	}

	PRINTFN(3,("up=%p udev=%p port=%d; "
		    "disconnect subdevs\n",
		    up, udev, up->portno));

	while(subdev < subdev_end)
	{
		if(subdev[0] && free_subdev)
		{
			device_printf(subdev[0], "at %s ", device_get_nameunit
				(device_get_parent(subdev[0])));

			if(up->portno != 0)
			{
				printf("port %d ", up->portno);
			}

			printf("(addr %d) disconnected\n", udev->address);

			/* device_delete_child() will detach all sub-devices ! */
			if(device_delete_child
				(device_get_parent(subdev[0]), subdev[0]))
			{
				/* if detach fails sub-devices will still
				 * be referring to the udev structure
				 * which cannot be freed
				 */
				device_printf(subdev[0], "detach failed "
					"(please ensure that this "
					"device driver supports detach)\n");

				error = ENXIO;
			}
		}

		/* always clear subdev[0], 
		 * because it might be used by
		 * usbd_add_dev_event()
		 */
		subdev[0] = NULL;
		subdev++;
	}

	/* issue detach event and free address */
	if(udev->bus != NULL)
	{
		usbd_add_dev_event(USB_EVENT_DEVICE_DETACH, udev);

		/* NOTE: address 0 is always unused */
		udev->bus->devices[udev->address] = 0;
	}

	usbd_free_iface_data(udev);

	if(error)
	{
		panic("%s: some USB devices would not detach\n",
			__FUNCTION__);
	}

	/* free device */
	free(udev, M_USB);
	up->device = 0;
	return;
}

void
usb_detach_wait(device_t dev)
{
	PRINTF(("waiting for %s\n", device_get_nameunit(dev)));

	/* XXX should use msleep */

	if(tsleep(dev, PZERO, "usbdet", hz * 60))
	{
		device_printf(dev, "didn't detach\n");
	}
	PRINTF(("%s done\n", device_get_nameunit(dev)));
	return;
}

void
usb_detach_wakeup(device_t dev)
{
	PRINTF(("for %s\n", device_get_nameunit(dev)));
	wakeup(dev);
	return;
}

struct usbd_interface *
usbd_get_iface(struct usbd_device *udev, u_int8_t iface_index)
{
	struct usbd_interface *iface = &udev->ifaces[iface_index];

	if((iface < &udev->ifaces[0]) ||
	   (iface >= &udev->ifaces_end[0]) ||
	   (udev->cdesc == NULL) ||
	   (iface_index >= udev->cdesc->bNumInterface))
	{
		return NULL;
	}
	return iface;
}

void
usbd_set_desc(device_t dev, struct usbd_device *udev)
{
	u_int8_t devinfo[256];

	usbd_devinfo(udev, 1, devinfo, sizeof(devinfo));
	device_set_desc_copy(dev, devinfo);
	device_printf(dev, "<%s>\n", devinfo);
	return;
}

/*------------------------------------------------------------------------------*
 *      allocate mbufs to an usbd interface queue
 *
 * returns a pointer that eventually should be passed to "free()"
 *------------------------------------------------------------------------------*/
void *
usbd_alloc_mbufs(struct malloc_type *type, struct usbd_ifqueue *ifq, 
		 u_int32_t block_size, u_int16_t block_number)
{
	struct usbd_mbuf *m_ptr;
	u_int8_t *data_ptr;
	void *free_ptr = NULL;
	u_int32_t alloc_size;

        /* align data */
        block_size += ((-block_size) & (USB_HOST_ALIGN-1));

	if (block_number && block_size) {

	  alloc_size = (block_size + sizeof(struct usbd_mbuf)) * block_number;

	  free_ptr = malloc(alloc_size, type, M_WAITOK|M_ZERO);

	  if (free_ptr == NULL) {
	      goto done;
	  }

	  m_ptr = free_ptr;
	  data_ptr = (void *)(m_ptr + block_number);

	  while(block_number--) {

	      m_ptr->cur_data_ptr =
		m_ptr->min_data_ptr = data_ptr;

	      m_ptr->cur_data_len =
		m_ptr->max_data_len = block_size;

	      USBD_IF_ENQUEUE(ifq, m_ptr);

	      m_ptr++;
	      data_ptr += block_size;
	  }
	}
 done:
	return free_ptr;
}

/*------------------------------------------------------------------------------*
 *  usbd_get_page - lookup DMA-able memory for the given offset
 *------------------------------------------------------------------------------*/
void
usbd_get_page(struct usbd_page_cache *cache, u_int32_t offset, 
	      struct usbd_page_search *res)
{
	struct usbd_page *page;

	offset += cache->page_offset_buf;

	if ((offset < cache->page_offset_cur) ||
	    (cache->page_cur == NULL)) {

	    /* reset the page search */

	    cache->page_cur = cache->page_start;
	    cache->page_offset_cur = 0;
	}

	offset -= cache->page_offset_cur;
	page = cache->page_cur;

	while (1) {

	    if (page >= cache->page_end) {
	        res->length = 0;
		res->buffer = NULL;
		res->physaddr = 0;
		break;
	    }

	    if (offset < page->length) {

	        /* found the offset on a page */

	        res->length = page->length - offset;
		res->buffer = ADD_BYTES(page->buffer, offset);
		res->physaddr = page->physaddr + offset;
		break;
	    }

	    /* get the next page */

	    offset -= page->length;
	    cache->page_offset_cur += page->length;
	    page++;
	    cache->page_cur = page;
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_copy_in - copy directly to DMA-able memory
 *------------------------------------------------------------------------------*/
void
usbd_copy_in(struct usbd_page_cache *cache, u_int32_t offset, 
	     const void *ptr, u_int32_t len)
{
	struct usbd_page_search res;

	while (len) {

	    usbd_get_page(cache, offset, &res);

	    if (res.length == 0) {
	        panic("%s:%d invalid offset!\n",
		      __FUNCTION__, __LINE__);
	    }

	    if (res.length > len) {
	        res.length = len;
	    }

	    bcopy(ptr, res.buffer, res.length);

	    offset += res.length;
	    len -= res.length;
	    ptr = ((const u_int8_t *)ptr) + res.length;
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_m_copy_in - copy a mbuf chain directly into DMA-able memory
 *------------------------------------------------------------------------------*/
struct usbd_m_copy_in_arg {
	struct usbd_page_cache *cache;
	u_int32_t dst_offset;
};

static int32_t
#ifdef __FreeBSD__
usbd_m_copy_in_cb(void *arg, void *src, u_int32_t count)
#else
usbd_m_copy_in_cb(void *arg, caddr_t src, u_int32_t count)
#endif
{
	register struct usbd_m_copy_in_arg *ua = arg;
	usbd_copy_in(ua->cache, ua->dst_offset, src, count);
	ua->dst_offset += count;
	return 0;
}

void
usbd_m_copy_in(struct usbd_page_cache *cache, u_int32_t dst_offset,
	       struct mbuf *m, u_int32_t src_offset, u_int32_t src_len)
{
	struct usbd_m_copy_in_arg arg = { cache, dst_offset };
	register int error;
	error = m_apply(m, src_offset, src_len, &usbd_m_copy_in_cb, &arg);
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_copy_out - copy directly from DMA-able memory
 *------------------------------------------------------------------------------*/
void
usbd_copy_out(struct usbd_page_cache *cache, u_int32_t offset, 
	      void *ptr, u_int32_t len)
{
	struct usbd_page_search res;

	while (len) {

	    usbd_get_page(cache, offset, &res);

	    if (res.length == 0) {
	        panic("%s:%d invalid offset!\n",
		      __FUNCTION__, __LINE__);
	    }

	    if (res.length > len) {
	        res.length = len;
	    }

	    bcopy(res.buffer, ptr, res.length);

	    offset += res.length;
	    len -= res.length;
	    ptr = ADD_BYTES(ptr, res.length);
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_bzero - zero DMA-able memory
 *------------------------------------------------------------------------------*/
void
usbd_bzero(struct usbd_page_cache *cache, u_int32_t offset, u_int32_t len)
{
	struct usbd_page_search res;

	while (len) {

	    usbd_get_page(cache, offset, &res);

	    if (res.length == 0) {
	        panic("%s:%d invalid offset!\n",
		      __FUNCTION__, __LINE__);
	    }

	    if (res.length > len) {
	        res.length = len;
	    }

	    bzero(res.buffer, res.length);

	    offset += res.length;
	    len -= res.length;
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_page_alloc - allocate multiple DMA-able memory pages
 *
 * return values:
 *   1: failure
 *   0: success
 *------------------------------------------------------------------------------*/
u_int8_t
usbd_page_alloc(bus_dma_tag_t tag, struct usbd_page *page, 
		u_int32_t npages)
{
	u_int32_t x;
	void *ptr;

	for (x = 0; x < npages; x++) {

	repeat:

	    ptr = usbd_mem_alloc_sub(tag, page + x, 
				     USB_PAGE_SIZE, USB_PAGE_SIZE);
	    if (ptr == NULL) {

	        while(x--) {
		    usbd_mem_free_sub(page + x);
		}
		return 1; /* failure */
	    } else {

	      if ((page + x)->physaddr == 0) {

		  /* at least the OHCI controller
		   * gives special meaning to 
		   * physaddr == 0, so discard
		   * that page if it gets here:
		   */
		  printf("%s:%d: Discarded memory "
			 "page with physaddr=0!\n",
			 __FUNCTION__, __LINE__);
		  goto repeat;
	      }
	    }
	}
	return 0; /* success */
}

/*------------------------------------------------------------------------------*
 *  usbd_page_free - free multiple DMA-able memory pages
 *------------------------------------------------------------------------------*/
void
usbd_page_free(struct usbd_page *page, u_int32_t npages)
{
	while(npages--) {
	    usbd_mem_free_sub(page + npages);
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_page_get_buf - get virtual buffer
 *------------------------------------------------------------------------------*/
void *
usbd_page_get_buf(struct usbd_page *page, u_int32_t size)
{
	page += (size / USB_PAGE_SIZE);
	size &= (USB_PAGE_SIZE-1);
	return ADD_BYTES(page->buffer,size);
}

/*------------------------------------------------------------------------------*
 *  usbd_page_get_phy - get physical buffer
 *------------------------------------------------------------------------------*/
bus_size_t
usbd_page_get_phy(struct usbd_page *page, u_int32_t size)
{
	page += (size / USB_PAGE_SIZE);
	size &= (USB_PAGE_SIZE-1);
	return (page->physaddr + size);
}

/*------------------------------------------------------------------------------*
 *  usbd_page_set_start
 *------------------------------------------------------------------------------*/
void
usbd_page_set_start(struct usbd_page_cache *pc, struct usbd_page *page_ptr,
		    u_int32_t size)
{
	pc->page_start = page_ptr + (size / USB_PAGE_SIZE);
	pc->page_offset_buf = (size % USB_PAGE_SIZE);
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_page_set_end
 *------------------------------------------------------------------------------*/
void
usbd_page_set_end(struct usbd_page_cache *pc, struct usbd_page *page_ptr,
		  u_int32_t size)
{
	pc->page_end = (page_ptr + 
			((size + USB_PAGE_SIZE -1) / USB_PAGE_SIZE));
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_page_fit_obj - fit object function
 *------------------------------------------------------------------------------*/
u_int32_t
usbd_page_fit_obj(struct usbd_page *page, u_int32_t size, u_int32_t obj_len)
{
	u_int32_t adj;

	if (obj_len > USB_PAGE_SIZE) {
	    panic("%s:%d Too large object, %d bytes, will "
		  "not fit on a USB page, %d bytes!\n", 
		  __FUNCTION__, __LINE__, obj_len, 
		  USB_PAGE_SIZE);
	}

	if (obj_len > (USB_PAGE_SIZE - (size & (USB_PAGE_SIZE-1)))) {

	  /* adjust offset to the beginning 
	   * of the next page:
	   */
	  adj = ((-size) & (USB_PAGE_SIZE-1));

	  if (page) {
	      /* adjust the size of the 
	       * current page, so that the
	       * objects follow back to back!
	       */
	      (page + (size/USB_PAGE_SIZE))->length -= adj;
	  }
	} else {
	  adj = 0;
	}
	return adj;
}

/*------------------------------------------------------------------------------*
 *  usbd_mem_alloc - allocate DMA-able memory
 *------------------------------------------------------------------------------*/
void *
usbd_mem_alloc(bus_dma_tag_t parent, u_int32_t size, 
	       u_int8_t align_power)
{
	bus_dma_tag_t tag;
	struct usbd_page temp;
	u_int32_t alignment = (1 << align_power);
	void *ptr;

	size += (-size) & (USB_HOST_ALIGN-1);
	size += sizeof(temp);

	tag = usbd_dma_tag_alloc(parent, size, alignment);

	if (tag == NULL) {
	    return NULL;
	}

	ptr = usbd_mem_alloc_sub(tag, &temp, size, alignment);

	if (ptr == NULL) {
	    usbd_dma_tag_free(tag);
	    return NULL;
	}

	size -= sizeof(temp);

	bcopy(&temp, ADD_BYTES(ptr,size), sizeof(temp));

	return ptr;
}

/*------------------------------------------------------------------------------*
 *  usbd_mem_vtophys - get the physical address of DMA-able memory
 *------------------------------------------------------------------------------*/
bus_size_t
usbd_mem_vtophys(void *ptr, u_int32_t size)
{
	struct usbd_page *page;

	size += (-size) & (USB_HOST_ALIGN-1);

	page = ADD_BYTES(ptr, size);

	return page->physaddr;
}

/*------------------------------------------------------------------------------*
 *  usbd_mem_free - free DMA-able memory
 *------------------------------------------------------------------------------*/
void
usbd_mem_free(void *ptr, u_int32_t size)
{
	struct usbd_page *page;
	bus_dma_tag_t tag;

	size += (-size) & (USB_HOST_ALIGN-1);

	page = ADD_BYTES(ptr, size);

	tag = page->tag;

	usbd_mem_free_sub(page);

	usbd_dma_tag_free(tag);

	return;
}

#ifdef __FreeBSD__
/*------------------------------------------------------------------------------*
 *  bus_dmamap_load_callback
 *------------------------------------------------------------------------------*/
static void
bus_dmamap_load_callback(void *arg, bus_dma_segment_t *segs, 
			 int nseg, int error)
{
	*((bus_size_t *)arg) = nseg ? segs->ds_addr : 0;

	if(error)
	{
	    printf("%s: %s: error=%d\n",
		   __FILE__, __FUNCTION__, error);
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_dma_tag_alloc - allocate a bus-DMA tag
 *------------------------------------------------------------------------------*/
bus_dma_tag_t 
usbd_dma_tag_alloc(bus_dma_tag_t parent, u_int32_t size, 
		   u_int32_t alignment)
{
	bus_dma_tag_t tag;

	if(bus_dma_tag_create
	   ( /* parent    */parent,
	     /* alignment */alignment,
	     /* boundary  */0,
	     /* lowaddr   */BUS_SPACE_MAXADDR_32BIT,
	     /* highaddr  */BUS_SPACE_MAXADDR,
	     /* filter    */NULL,
	     /* filterarg */NULL,
	     /* maxsize   */size,
	     /* nsegments */1,
	     /* maxsegsz  */size,
	     /* flags     */0,
	     /* lock      */NULL,
	     /*           */NULL,
	     &tag))
	{
	    tag = NULL;
	}
	return tag;
}

/*------------------------------------------------------------------------------*
 *  usbd_dma_tag_free - free a bus-DMA tag
 *------------------------------------------------------------------------------*/
void
usbd_dma_tag_free(bus_dma_tag_t tag)
{
	bus_dma_tag_destroy(tag);
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_mem_alloc_sub - allocate DMA-able memory
 *------------------------------------------------------------------------------*/
void *
usbd_mem_alloc_sub(bus_dma_tag_t tag, struct usbd_page *page,
		   u_int32_t size, u_int32_t alignment)
{
	bus_dmamap_t map;
	bus_size_t physaddr = 0;
	void *ptr;

	if(bus_dmamem_alloc
	   (tag, &ptr, (BUS_DMA_WAITOK|BUS_DMA_COHERENT), &map))
	{
		return NULL;
	}

	if(bus_dmamap_load
	   (tag, map, ptr, size, &bus_dmamap_load_callback, 
	    &physaddr, (BUS_DMA_WAITOK|BUS_DMA_COHERENT)))
	{
		bus_dmamem_free(tag, ptr, map);
		return NULL;
	}

	page->tag = tag;
	page->map = map;
	page->physaddr = physaddr;
	page->buffer = ptr;
	page->length = size;

	bzero(ptr, size);

#ifdef USB_DEBUG
	if(usbdebug > 14)
	{
	    printf("%s: %p, %d bytes, phys=%p\n", 
		   __FUNCTION__, ptr, size, 
		   ((char *)0) + physaddr);
	}
#endif
	return ptr;
}

/*------------------------------------------------------------------------------*
 *  usbd_mem_free_sub - free DMA-able memory
 *------------------------------------------------------------------------------*/
void
usbd_mem_free_sub(struct usbd_page *page)
{
	/* NOTE: make a copy of "tag", "map", 
	 * and "buffer" in case "page" is part 
	 * of the allocated memory:
	 */
	bus_dma_tag_t tag = page->tag;
	bus_dmamap_t map = page->map;
	void *ptr = page->buffer;

	bus_dmamap_unload(tag, map);

	bus_dmamem_free(tag, ptr, map);

#ifdef USB_DEBUG
	if(usbdebug > 14)
	{
	    printf("%s: %p\n", 
		   __FUNCTION__, ptr);
	}
#endif
	return;
}
#endif

#ifdef __NetBSD__

bus_dma_tag_t 
usbd_dma_tag_alloc(bus_dma_tag_t parent, u_int32_t size, 
		   u_int32_t alignment)
{
	/* FreeBSD specific */
	return parent;
}

void
usbd_dma_tag_free(bus_dma_tag_t tag)
{
	return;
}

void *
usbd_mem_alloc_sub(bus_dma_tag_t tag, struct usbd_page *page,
		   u_int32_t size, u_int32_t alignment)
{
	caddr_t ptr = NULL;

	page->tag = tag;
	page->seg_count = 1;

	if(bus_dmamem_alloc(page->tag, size, alignment, 0,
			    &page->seg, 1,
			    &page->seg_count, BUS_DMA_WAITOK))
	{
		goto done_4;
	}

	if(bus_dmamem_map(page->tag, &page->seg, page->seg_count, size,
			  &ptr, BUS_DMA_WAITOK|BUS_DMA_COHERENT))
	{
		goto done_3;
	}

	if(bus_dmamap_create(page->tag, size, 1, size,
			     0, BUS_DMA_WAITOK, &page->map))
	{
		goto done_2;
	}

	if(bus_dmamap_load(page->tag, page->map, ptr, size, NULL, 
			   BUS_DMA_WAITOK))
	{
		goto done_1;
	}

	page->physaddr = page->map->dm_segs[0].ds_addr;
	page->buffer = ptr;
	page->length = size;

	bzero(ptr, size);

#ifdef USB_DEBUG
	if(usbdebug > 14)
	{
	    printf("%s: %p, %d bytes, phys=%p\n", 
		   __FUNCTION__, ptr, size, 
		   ((char *)0) + page->physaddr);
	}
#endif
	return ptr;

 done_1:
	bus_dmamap_destroy(page->tag, page->map);

 done_2:
	bus_dmamem_unmap(page->tag, ptr, size);

 done_3:
	bus_dmamem_free(page->tag, &page->seg, page->seg_count);

 done_4:
	return NULL;
}

void
usbd_mem_free_sub(struct usbd_page *page)
{
	/* NOTE: make a copy of "tag", "map", 
	 * and "buffer" in case "page" is part 
	 * of the allocated memory:
	 */
	struct usbd_page temp = *page;

	bus_dmamap_unload(temp.tag, temp.map);
	bus_dmamap_destroy(temp.tag, temp.map);
	bus_dmamem_unmap(temp.tag, temp.buffer, temp.length);
	bus_dmamem_free(temp.tag, &temp.seg, temp.seg_count);

#ifdef USB_DEBUG
	if(usbdebug > 14)
	{
	    printf("%s: %p\n", 
		   __FUNCTION__, temp.buffer);
	}
#endif
	return;
}
#endif

/*------------------------------------------------------------------------------*
 *  usbd_std_transfer_setup - standard transfer setup
 *------------------------------------------------------------------------------*/
void
usbd_std_transfer_setup(struct usbd_xfer *xfer, 
			const struct usbd_config *setup, 
			u_int16_t max_packet_size, u_int16_t max_frame_size)
{
	__callout_init_mtx(&xfer->timeout_handle, xfer->usb_mtx,
			   CALLOUT_RETURNUNLOCKED);

	xfer->flags = setup->flags;
	xfer->nframes = setup->frames;
	xfer->timeout = setup->timeout;
	xfer->callback = setup->callback;
	xfer->interval = setup->interval;
	xfer->endpoint = xfer->pipe->edesc->bEndpointAddress;
	xfer->max_packet_size = UGETW(xfer->pipe->edesc->wMaxPacketSize);
	xfer->length = setup->bufsize;

	if(xfer->interval == 0)
	{
	    xfer->interval = xfer->pipe->edesc->bInterval;
	}

	if(xfer->interval == 0)
	{
	    /* one is the smallest interval */
	    xfer->interval = 1;
	}

	/* wMaxPacketSize is also checked by "usbd_fill_iface_data()" */

	if (xfer->max_packet_size == 0) {
	    xfer->max_packet_size = 8;
	}

	if (xfer->max_packet_size > max_packet_size) {
	    xfer->max_packet_size = max_packet_size;
	}

	/* frame_size: isochronous frames only */

	xfer->max_frame_size = max_frame_size;

	if (xfer->length == 0) {
	    xfer->length = xfer->max_packet_size;
	    if (xfer->nframes) {
	        xfer->length *= xfer->nframes;
	    }
	    if (setup->sub_frames) {
	        xfer->length *= setup->sub_frames;
	    }
	}
	return;
}

/*------------------------------------------------------------------------------*
 *  usbd_make_str_desc - convert an ASCII string into a UNICODE string
 *------------------------------------------------------------------------------*/
u_int8_t
usbd_make_str_desc(void *ptr, u_int16_t max_len, const char *s)
{
	usb_string_descriptor_t *p = ptr;
	u_int8_t totlen;
	int32_t j;

	if (max_len < 2) {
	    /* invalid length */
	    return 0;
	}

	max_len = ((max_len / 2) - 1);

	j = strlen(s);

	if (j < 0) {
	    j = 0;
	}

	if (j > 126) {
	    j = 126;
	}

	if (max_len > j) {
	    max_len = j;
	}

	totlen = (max_len + 1) * 2;

	p->bLength = totlen;
	p->bDescriptorType = UDESC_STRING;

	while (max_len--) {
	    USETW2(p->bString[max_len], 0, s[max_len]);
	}
	return totlen;
}

/*---------------------------------------------------------------------------*
 * mtx_drop_recurse - drop mutex recurse level
 *---------------------------------------------------------------------------*/
u_int32_t
mtx_drop_recurse(struct mtx *mtx)
{
	u_int32_t recurse_level = mtx->mtx_recurse;
	u_int32_t recurse_curr = recurse_level;

	mtx_assert(mtx, MA_OWNED);

	while(recurse_curr--) {
	    mtx_unlock(mtx);
	}

	return recurse_level;
}

/*---------------------------------------------------------------------------*
 * mtx_pickup_recurse - pickup mutex recurse level
 *---------------------------------------------------------------------------*/
void
mtx_pickup_recurse(struct mtx *mtx, u_int32_t recurse_level)
{
	mtx_assert(mtx, MA_OWNED);

	while(recurse_level--) {
	    mtx_lock(mtx);
	}
	return;
}


/*---------------------------------------------------------------------------*
 * usbd_config_thread
 *---------------------------------------------------------------------------*/
static void
usbd_config_td_thread(void *arg)
{
	struct usbd_config_td *ctd = arg;
	struct usbd_config_td_item *item;
	struct usbd_mbuf *m;
	register int error;

	mtx_lock(ctd->p_mtx);

	while(1) {

	    if (ctd->flag_config_td_gone) {
	        break;
	    }

	    USBD_IF_DEQUEUE(&(ctd->cmd_used), m);

	    if (m) {

	        item = (void *)(m->cur_data_ptr);

		(item->command_func)
		  (ctd->p_softc, (void *)(item+1), item->command_ref);

		USBD_IF_ENQUEUE(&(ctd->cmd_free), m);

		continue;
	    }

	    if (ctd->p_end_of_commands) {
	        (ctd->p_end_of_commands)(ctd->p_softc);
	    }

	    ctd->flag_config_td_sleep = 1;

	    error = msleep(&(ctd->wakeup_config_td), ctd->p_mtx,
			   0, "cfg td sleep", 0);

	    ctd->flag_config_td_sleep = 0;
	}

	ctd->config_thread = NULL;

	wakeup(&(ctd->wakeup_config_td_gone));

	mtx_unlock(ctd->p_mtx);

	kthread_exit(0);

	return;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_setup
 *
 * NOTE: the structure pointed to by "ctd" must be zeroed before calling 
 * this function!
 *
 * Return values:
 *    0: success
 * else: failure
 *---------------------------------------------------------------------------*/
u_int8_t
usbd_config_td_setup(struct usbd_config_td *ctd, void *priv_sc, 
		     struct mtx *priv_mtx, 
		     usbd_config_td_config_copy_t *p_func_cc,
		     usbd_config_td_end_of_commands_t *p_func_eoc,
		     u_int16_t item_size, u_int16_t item_count)
{
	ctd->p_mtx = priv_mtx;
	ctd->p_softc = priv_sc;
	ctd->p_config_copy = p_func_cc;
	ctd->p_end_of_commands = p_func_eoc;

	if (item_count >= 256) {
	    PRINTFN(0,("too many items!\n"));
	    goto error;
	}

	ctd->p_cmd_queue = 
	  usbd_alloc_mbufs(M_DEVBUF, &(ctd->cmd_free), 
			   (sizeof(struct usbd_config_td_item) + item_size), 
			   item_count);

	if (ctd->p_cmd_queue == NULL) {
	    PRINTFN(0,("unable to allocate memory "
		       "for command queue!\n"));
	    goto error;
	}

	if (usb_kthread_create1
	    (&usbd_config_td_thread, ctd, &(ctd->config_thread), 
	     "usbd config thread")) {
	    PRINTFN(0,("unable to create config thread!\n"));
	    ctd->config_thread = NULL;
	    goto error;
	}
	return 0;

 error:
	usbd_config_td_unsetup(ctd);
	return 1;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_dummy_cmd
 *---------------------------------------------------------------------------*/
static void
usbd_config_td_dummy_cmd(struct usbd_config_td_softc *sc, 
			 struct usbd_config_td_cc *cc, 
			 u_int16_t reference)
{
	return;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_stop
 *
 * NOTE: If the structure pointed to by "ctd" is all zero,
 * this function does nothing.
 *---------------------------------------------------------------------------*/
void
usbd_config_td_stop(struct usbd_config_td *ctd)
{
	register int error;

	while (ctd->config_thread) {

	    mtx_assert(ctd->p_mtx, MA_OWNED);

	    ctd->flag_config_td_gone = 1;

	    usbd_config_td_queue_command(ctd, &usbd_config_td_dummy_cmd, 0);

	    if (cold) {
	        panic("%s:%d: cannot stop config thread!\n",
		      __FUNCTION__, __LINE__);
	    }

	    error = msleep(&(ctd->wakeup_config_td_gone), 
			   ctd->p_mtx, 0, "wait config TD", 0);
	}
	return;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_unsetup
 *
 * NOTE: If the structure pointed to by "ctd" is all zero,
 * this function does nothing.
 *---------------------------------------------------------------------------*/
void
usbd_config_td_unsetup(struct usbd_config_td *ctd)
{
	if (ctd->p_mtx) {
	    mtx_lock(ctd->p_mtx);
	    usbd_config_td_stop(ctd);
	    mtx_unlock(ctd->p_mtx);
	}
	if (ctd->p_cmd_queue) {
	    free(ctd->p_cmd_queue, M_DEVBUF);
	    ctd->p_cmd_queue = NULL;
	}
	return;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_queue_command
 *---------------------------------------------------------------------------*/
void
usbd_config_td_queue_command(struct usbd_config_td *ctd,
			     usbd_config_td_command_t *command_func,
			     u_int16_t command_ref)
{
	struct usbd_config_td_item *item;
	struct usbd_mbuf *m;
	int32_t qlen;

	mtx_assert(ctd->p_mtx, MA_OWNED);

	/*
	 * first check if the command was
	 * already queued, and if so, remove
	 * it from the queue:
	 */
	qlen = USBD_IF_QLEN(&(ctd->cmd_used));

	while (qlen--) {

	    USBD_IF_DEQUEUE(&(ctd->cmd_used), m);

	    if (m == NULL) {
	        /* should not happen */
	        break;
	    }

	    item = (void *)(m->cur_data_ptr);

	    if ((item->command_func == command_func) &&
		(item->command_ref == command_ref)) {
	        USBD_IF_ENQUEUE(&(ctd->cmd_free), m);
	    } else {
	        USBD_IF_ENQUEUE(&(ctd->cmd_used), m);
	    }
	}

	/* first call to command function
	 * (do this before calling the 
	 *  config copy function, so that
	 *  the immediate part of the command
	 *  function gets a chance to change 
	 *  the config before it is copied)
	 */
	(command_func)(ctd->p_softc, NULL, command_ref);

	USBD_IF_DEQUEUE(&(ctd->cmd_free), m);

	if (m == NULL) {
	    /* should not happen */
	    panic("%s:%d: out of memory!\n",
		  __FUNCTION__, __LINE__);
	}

	USBD_MBUF_RESET(m);

	item = (void *)(m->cur_data_ptr);

	if (ctd->p_config_copy) {
	    (ctd->p_config_copy)(ctd->p_softc, (void *)(item+1), command_ref);
	}

	item->command_func = command_func;
	item->command_ref = command_ref;

	USBD_IF_ENQUEUE(&(ctd->cmd_used), m);

	if (ctd->flag_config_td_sleep) {
	    ctd->flag_config_td_sleep = 0;
	    wakeup(&(ctd->wakeup_config_td));
	}
	return;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_is_gone
 *
 * Return values:
 *    0: config thread is running
 * else: config thread is gone
 *---------------------------------------------------------------------------*/
u_int8_t
usbd_config_td_is_gone(struct usbd_config_td *ctd)
{
	mtx_assert(ctd->p_mtx, MA_OWNED);

	return ctd->flag_config_td_gone ? 1 : 0;
}

/*---------------------------------------------------------------------------*
 * usbd_config_td_sleep
 *
 * NOTE: this function can only be called from the config thread
 *
 * Return values:
 *    0: normal delay
 * else: config thread is gone
 *---------------------------------------------------------------------------*/
u_int8_t
usbd_config_td_sleep(struct usbd_config_td *ctd, u_int32_t timeout)
{
	register int error;
	u_int8_t is_gone = usbd_config_td_is_gone(ctd);
	u_int32_t level;

	if (is_gone) {
	    goto done;
	}

	if (timeout == 0) {
	    /* zero means no timeout, 
	     * so avoid that by setting
	     * timeout to one:
	     */
	    timeout = 1;
	}

	level = mtx_drop_recurse(ctd->p_mtx);

	error = msleep(ctd, ctd->p_mtx, 0, 
		       "config td sleep", timeout);

	mtx_pickup_recurse(ctd->p_mtx, level);

 done:
	return is_gone;
}

/*---------------------------------------------------------------------------*
 * usbd_ether_get_mbuf - get a new ethernet mbuf
 *---------------------------------------------------------------------------*/
struct mbuf *
usbd_ether_get_mbuf(void)
{
	register struct mbuf *m_new;

	m_new = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m_new) {
	    m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	    m_adj(m_new, ETHER_ALIGN);
	}
	return (m_new);
}
