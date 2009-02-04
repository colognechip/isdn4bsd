/*-
 * Copyright (c) 2001-2003, Shunsuke Akiyama <akiyama@FreeBSD.org>.
 * Copyright (c) 1997, 1998, 1999, 2000 Bill Paul <wpaul@ee.columbia.edu>.
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
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/dev/usb2/ethernet/if_rue2.c,v 1.7 2009/02/01 00:51:25 thompsa Exp $");

/*
 * RealTek RTL8150 USB to fast ethernet controller driver.
 * Datasheet is available from
 * ftp://ftp.realtek.com.tw/lancard/data_sheet/8150/.
 */

#include <dev/usb2/include/usb2_devid.h>
#include <dev/usb2/include/usb2_standard.h>
#include <dev/usb2/include/usb2_mfunc.h>
#include <dev/usb2/include/usb2_error.h>

#define	USB_DEBUG_VAR rue_debug

#include <dev/usb2/core/usb2_core.h>
#include <dev/usb2/core/usb2_lookup.h>
#include <dev/usb2/core/usb2_process.h>
#include <dev/usb2/core/usb2_debug.h>
#include <dev/usb2/core/usb2_request.h>
#include <dev/usb2/core/usb2_busdma.h>
#include <dev/usb2/core/usb2_util.h>

#include <dev/usb2/ethernet/usb2_ethernet.h>
#include <dev/usb2/ethernet/if_ruereg.h>

#if USB_DEBUG
static int rue_debug = 0;

SYSCTL_NODE(_hw_usb2, OID_AUTO, rue, CTLFLAG_RW, 0, "USB rue");
SYSCTL_INT(_hw_usb2_rue, OID_AUTO, debug, CTLFLAG_RW,
    &rue_debug, 0, "Debug level");
#endif

/*
 * Various supported device vendors/products.
 */

static const struct usb2_device_id rue_devs[] = {
	{USB_VPI(USB_VENDOR_MELCO, USB_PRODUCT_MELCO_LUAKTX, 0)},
	{USB_VPI(USB_VENDOR_REALTEK, USB_PRODUCT_REALTEK_USBKR100, 0)},
};

/* prototypes */

static device_probe_t rue_probe;
static device_attach_t rue_attach;
static device_detach_t rue_detach;
static device_shutdown_t rue_shutdown;

static miibus_readreg_t rue_miibus_readreg;
static miibus_writereg_t rue_miibus_writereg;
static miibus_statchg_t rue_miibus_statchg;

static usb2_callback_t rue_intr_callback;
static usb2_callback_t rue_bulk_read_callback;
static usb2_callback_t rue_bulk_write_callback;

static usb2_ether_fn_t rue_attach_post;
static usb2_ether_fn_t rue_init;
static usb2_ether_fn_t rue_stop;
static usb2_ether_fn_t rue_start;
static usb2_ether_fn_t rue_tick;
static usb2_ether_fn_t rue_setmulti;
static usb2_ether_fn_t rue_setpromisc;

static int	rue_read_mem(struct rue_softc *, uint16_t, void *, int);
static int	rue_write_mem(struct rue_softc *, uint16_t, void *, int);
static uint8_t	rue_csr_read_1(struct rue_softc *, uint16_t);
static uint16_t	rue_csr_read_2(struct rue_softc *, uint16_t);
static int	rue_csr_write_1(struct rue_softc *, uint16_t, uint8_t);
static int	rue_csr_write_2(struct rue_softc *, uint16_t, uint16_t);
static int	rue_csr_write_4(struct rue_softc *, int, uint32_t);

static void	rue_reset(struct rue_softc *);
static int	rue_ifmedia_upd(struct ifnet *);
static void	rue_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static const struct usb2_config rue_config[RUE_N_TRANSFER] = {

	[RUE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.mh.bufsize = MCLBYTES,
		.mh.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.mh.callback = rue_bulk_write_callback,
		.mh.timeout = 10000,	/* 10 seconds */
	},

	[RUE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.mh.bufsize = (MCLBYTES + 4),
		.mh.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.mh.callback = rue_bulk_read_callback,
		.mh.timeout = 0,	/* no timeout */
	},

	[RUE_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.mh.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.mh.bufsize = 0,	/* use wMaxPacketSize */
		.mh.callback = rue_intr_callback,
	},
};

static device_method_t rue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, rue_probe),
	DEVMETHOD(device_attach, rue_attach),
	DEVMETHOD(device_detach, rue_detach),
	DEVMETHOD(device_shutdown, rue_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg, rue_miibus_readreg),
	DEVMETHOD(miibus_writereg, rue_miibus_writereg),
	DEVMETHOD(miibus_statchg, rue_miibus_statchg),

	{0, 0}
};

static driver_t rue_driver = {
	.name = "rue",
	.methods = rue_methods,
	.size = sizeof(struct rue_softc),
};

static devclass_t rue_devclass;

DRIVER_MODULE(rue, ushub, rue_driver, rue_devclass, NULL, 0);
DRIVER_MODULE(miibus, rue, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(rue, usb2_ethernet, 1, 1, 1);
MODULE_DEPEND(rue, usb2_core, 1, 1, 1);
MODULE_DEPEND(rue, ether, 1, 1, 1);
MODULE_DEPEND(rue, miibus, 1, 1, 1);

static const struct usb2_ether_methods rue_ue_methods = {
	.ue_attach_post = rue_attach_post,
	.ue_start = rue_start,
	.ue_init = rue_init,
	.ue_stop = rue_stop,
	.ue_tick = rue_tick,
	.ue_setmulti = rue_setmulti,
	.ue_setpromisc = rue_setpromisc,
	.ue_mii_upd = rue_ifmedia_upd,
	.ue_mii_sts = rue_ifmedia_sts,
};

#define	RUE_SETBIT(sc, reg, x) \
	rue_csr_write_1(sc, reg, rue_csr_read_1(sc, reg) | (x))

#define	RUE_CLRBIT(sc, reg, x) \
	rue_csr_write_1(sc, reg, rue_csr_read_1(sc, reg) & ~(x))

static int
rue_read_mem(struct rue_softc *sc, uint16_t addr, void *buf, int len)
{
	struct usb2_device_request req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (usb2_ether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
rue_write_mem(struct rue_softc *sc, uint16_t addr, void *buf, int len)
{
	struct usb2_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (usb2_ether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static uint8_t
rue_csr_read_1(struct rue_softc *sc, uint16_t reg)
{
	uint8_t val;

	rue_read_mem(sc, reg, &val, 1);
	return (val);
}

static uint16_t
rue_csr_read_2(struct rue_softc *sc, uint16_t reg)
{
	uint8_t val[2];

	rue_read_mem(sc, reg, &val, 2);
	return (UGETW(val));
}

static int
rue_csr_write_1(struct rue_softc *sc, uint16_t reg, uint8_t val)
{
	return (rue_write_mem(sc, reg, &val, 1));
}

static int
rue_csr_write_2(struct rue_softc *sc, uint16_t reg, uint16_t val)
{
	uint8_t temp[2];

	USETW(temp, val);
	return (rue_write_mem(sc, reg, &temp, 2));
}

static int
rue_csr_write_4(struct rue_softc *sc, int reg, uint32_t val)
{
	uint8_t temp[4];

	USETDW(temp, val);
	return (rue_write_mem(sc, reg, &temp, 4));
}

static int
rue_miibus_readreg(device_t dev, int phy, int reg)
{
	struct rue_softc *sc = device_get_softc(dev);
	uint16_t rval;
	uint16_t ruereg;
	int locked;

	if (phy != 0)		/* RTL8150 supports PHY == 0, only */
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		RUE_LOCK(sc);

	switch (reg) {
	case MII_BMCR:
		ruereg = RUE_BMCR;
		break;
	case MII_BMSR:
		ruereg = RUE_BMSR;
		break;
	case MII_ANAR:
		ruereg = RUE_ANAR;
		break;
	case MII_ANER:
		ruereg = RUE_AER;
		break;
	case MII_ANLPAR:
		ruereg = RUE_ANLP;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		rval = 0;
		goto done;
	default:
		if (RUE_REG_MIN <= reg && reg <= RUE_REG_MAX) {
			rval = rue_csr_read_1(sc, reg);
			goto done;
		}
		device_printf(sc->sc_ue.ue_dev, "bad phy register\n");
		rval = 0;
		goto done;
	}

	rval = rue_csr_read_2(sc, ruereg);
done:
	if (!locked)
		RUE_UNLOCK(sc);
	return (rval);
}

static int
rue_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct rue_softc *sc = device_get_softc(dev);
	uint16_t ruereg;
	int locked;

	if (phy != 0)		/* RTL8150 supports PHY == 0, only */
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		RUE_LOCK(sc);

	switch (reg) {
	case MII_BMCR:
		ruereg = RUE_BMCR;
		break;
	case MII_BMSR:
		ruereg = RUE_BMSR;
		break;
	case MII_ANAR:
		ruereg = RUE_ANAR;
		break;
	case MII_ANER:
		ruereg = RUE_AER;
		break;
	case MII_ANLPAR:
		ruereg = RUE_ANLP;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		goto done;
	default:
		if (RUE_REG_MIN <= reg && reg <= RUE_REG_MAX) {
			rue_csr_write_1(sc, reg, data);
			goto done;
		}
		device_printf(sc->sc_ue.ue_dev, " bad phy register\n");
		goto done;
	}
	rue_csr_write_2(sc, ruereg, data);
done:
	if (!locked)
		RUE_UNLOCK(sc);
	return (0);
}

static void
rue_miibus_statchg(device_t dev)
{
	/*
	 * When the code below is enabled the card starts doing weird
	 * things after link going from UP to DOWN and back UP.
	 *
	 * Looks like some of register writes below messes up PHY
	 * interface.
	 *
	 * No visible regressions were found after commenting this code
	 * out, so that disable it for good.
	 */
#if 0
	struct rue_softc *sc = device_get_softc(dev);
	struct mii_data *mii = GET_MII(sc);
	uint16_t bmcr;
	int locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		RUE_LOCK(sc);

	RUE_CLRBIT(sc, RUE_CR, (RUE_CR_RE | RUE_CR_TE));

	bmcr = rue_csr_read_2(sc, RUE_BMCR);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		bmcr |= RUE_BMCR_SPD_SET;
	else
		bmcr &= ~RUE_BMCR_SPD_SET;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		bmcr |= RUE_BMCR_DUPLEX;
	else
		bmcr &= ~RUE_BMCR_DUPLEX;

	rue_csr_write_2(sc, RUE_BMCR, bmcr);

	RUE_SETBIT(sc, RUE_CR, (RUE_CR_RE | RUE_CR_TE));

	if (!locked)
		RUE_UNLOCK(sc);
#endif
}

static void
rue_setpromisc(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);
	struct ifnet *ifp = usb2_ether_getifp(ue);

	RUE_LOCK_ASSERT(sc, MA_OWNED);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		RUE_SETBIT(sc, RUE_RCR, RUE_RCR_AAP);
	else
		RUE_CLRBIT(sc, RUE_RCR, RUE_RCR_AAP);
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
rue_setmulti(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);
	struct ifnet *ifp = usb2_ether_getifp(ue);
	uint16_t rxcfg;
	int h = 0;
	uint32_t hashes[2] = { 0, 0 };
	struct ifmultiaddr *ifma;
	int mcnt = 0;

	RUE_LOCK_ASSERT(sc, MA_OWNED);

	rxcfg = rue_csr_read_2(sc, RUE_RCR);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxcfg |= (RUE_RCR_AAM | RUE_RCR_AAP);
		rxcfg &= ~RUE_RCR_AM;
		rue_csr_write_2(sc, RUE_RCR, rxcfg);
		rue_csr_write_4(sc, RUE_MAR0, 0xFFFFFFFF);
		rue_csr_write_4(sc, RUE_MAR4, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	rue_csr_write_4(sc, RUE_MAR0, 0);
	rue_csr_write_4(sc, RUE_MAR4, 0);

	/* now program new ones */
	IF_ADDR_LOCK(ifp);
	TAILQ_FOREACH (ifma, &ifp->if_multiaddrs, ifma_link)
	{
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}
	IF_ADDR_UNLOCK(ifp);

	if (mcnt)
		rxcfg |= RUE_RCR_AM;
	else
		rxcfg &= ~RUE_RCR_AM;

	rxcfg &= ~(RUE_RCR_AAM | RUE_RCR_AAP);

	rue_csr_write_2(sc, RUE_RCR, rxcfg);
	rue_csr_write_4(sc, RUE_MAR0, hashes[0]);
	rue_csr_write_4(sc, RUE_MAR4, hashes[1]);
}

static void
rue_reset(struct rue_softc *sc)
{
	int i;

	rue_csr_write_1(sc, RUE_CR, RUE_CR_SOFT_RST);

	for (i = 0; i != RUE_TIMEOUT; i++) {
		if (usb2_ether_pause(&sc->sc_ue, hz / 1000))
			break;
		if (!(rue_csr_read_1(sc, RUE_CR) & RUE_CR_SOFT_RST))
			break;
	}
	if (i == RUE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "reset never completed!\n");

	usb2_ether_pause(&sc->sc_ue, hz / 100);
}

static void
rue_attach_post(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);

	/* reset the adapter */
	rue_reset(sc);

	/* get station address from the EEPROM */
	rue_read_mem(sc, RUE_EEPROM_IDR0, ue->ue_eaddr, ETHER_ADDR_LEN);
}

/*
 * Probe for a RTL8150 chip.
 */
static int
rue_probe(device_t dev)
{
	struct usb2_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb2_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != RUE_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != RUE_IFACE_IDX)
		return (ENXIO);

	return (usb2_lookup_id_by_uaa(rue_devs, sizeof(rue_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
rue_attach(device_t dev)
{
	struct usb2_attach_arg *uaa = device_get_ivars(dev);
	struct rue_softc *sc = device_get_softc(dev);
	struct usb2_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	device_set_usb2_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = RUE_IFACE_IDX;
	error = usb2_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, rue_config, RUE_N_TRANSFER,
	    sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed!\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &rue_ue_methods;

	error = usb2_ether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	rue_detach(dev);
	return (ENXIO);			/* failure */
}

static int
rue_detach(device_t dev)
{
	struct rue_softc *sc = device_get_softc(dev);
	struct usb2_ether *ue = &sc->sc_ue;

	usb2_transfer_unsetup(sc->sc_xfer, RUE_N_TRANSFER);
	usb2_ether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
rue_intr_callback(struct usb2_xfer *xfer)
{
	struct rue_softc *sc = xfer->priv_sc;
	struct ifnet *ifp = usb2_ether_getifp(&sc->sc_ue);
	struct rue_intrpkt pkt;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (ifp && (ifp->if_drv_flags & IFF_DRV_RUNNING) &&
		    (xfer->actlen >= sizeof(pkt))) {

			usb2_copy_out(xfer->frbuffers, 0, &pkt, sizeof(pkt));

			ifp->if_ierrors += pkt.rue_rxlost_cnt;
			ifp->if_ierrors += pkt.rue_crcerr_cnt;
			ifp->if_collisions += pkt.rue_col_cnt;
		}
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		xfer->frlengths[0] = xfer->max_data_length;
		usb2_start_hardware(xfer);
		return;

	default:			/* Error */
		if (xfer->error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			xfer->flags.stall_pipe = 1;
			goto tr_setup;
		}
		return;
	}
}

static void
rue_bulk_read_callback(struct usb2_xfer *xfer)
{
	struct rue_softc *sc = xfer->priv_sc;
	struct usb2_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = usb2_ether_getifp(ue);
	uint16_t status;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (xfer->actlen < 4) {
			ifp->if_ierrors++;
			goto tr_setup;
		}
		usb2_copy_out(xfer->frbuffers, xfer->actlen - 4,
		    &status, sizeof(status));
		xfer->actlen -= 4;

		/* check recieve packet was valid or not */
		status = le16toh(status);
		if ((status & RUE_RXSTAT_VALID) == 0) {
			ifp->if_ierrors++;
			goto tr_setup;
		}
		usb2_ether_rxbuf(ue, xfer->frbuffers, 0, xfer->actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		xfer->frlengths[0] = xfer->max_data_length;
		usb2_start_hardware(xfer);
		usb2_ether_rxflush(ue);
		return;

	default:			/* Error */
		DPRINTF("bulk read error, %s\n",
		    usb2_errstr(xfer->error));

		if (xfer->error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			xfer->flags.stall_pipe = 1;
			goto tr_setup;
		}
		return;
	}
}

static void
rue_bulk_write_callback(struct usb2_xfer *xfer)
{
	struct rue_softc *sc = xfer->priv_sc;
	struct ifnet *ifp = usb2_ether_getifp(&sc->sc_ue);
	struct mbuf *m;
	int temp_len;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		ifp->if_opackets++;

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & RUE_FLAG_LINK) == 0) {
			/*
			 * don't send anything if there is no link !
			 */
			return;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL)
			return;
		if (m->m_pkthdr.len > MCLBYTES)
			m->m_pkthdr.len = MCLBYTES;
		temp_len = m->m_pkthdr.len;

		usb2_m_copy_in(xfer->frbuffers, 0,
		    m, 0, m->m_pkthdr.len);

		/*
		 * This is an undocumented behavior.
		 * RTL8150 chip doesn't send frame length smaller than
		 * RUE_MIN_FRAMELEN (60) byte packet.
		 */
		if (temp_len < RUE_MIN_FRAMELEN) {
			usb2_bzero(xfer->frbuffers, temp_len,
			    RUE_MIN_FRAMELEN - temp_len);
			temp_len = RUE_MIN_FRAMELEN;
		}
		xfer->frlengths[0] = temp_len;

		/*
		 * if there's a BPF listener, bounce a copy
		 * of this frame to him:
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

		usb2_start_hardware(xfer);

		return;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usb2_errstr(xfer->error));

		ifp->if_oerrors++;

		if (xfer->error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			xfer->flags.stall_pipe = 1;
			goto tr_setup;
		}
		return;
	}
}

static void
rue_tick(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);
	struct mii_data *mii = GET_MII(sc);

	RUE_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & RUE_FLAG_LINK) == 0
	    && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->sc_flags |= RUE_FLAG_LINK;
		rue_start(ue);
	}
}

static void
rue_start(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usb2_transfer_start(sc->sc_xfer[RUE_INTR_DT_RD]);
	usb2_transfer_start(sc->sc_xfer[RUE_BULK_DT_RD]);
	usb2_transfer_start(sc->sc_xfer[RUE_BULK_DT_WR]);
}

static void
rue_init(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);
	struct ifnet *ifp = usb2_ether_getifp(ue);

	RUE_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Cancel pending I/O
	 */
	rue_reset(sc);

	/* Set MAC address */
	rue_write_mem(sc, RUE_IDR0, IF_LLADDR(ifp), ETHER_ADDR_LEN);

	rue_stop(ue);

	/*
	 * Set the initial TX and RX configuration.
	 */
	rue_csr_write_1(sc, RUE_TCR, RUE_TCR_CONFIG);
	rue_csr_write_2(sc, RUE_RCR, RUE_RCR_CONFIG|RUE_RCR_AB);

	/* Load the multicast filter */
	rue_setpromisc(ue);
	/* Load the multicast filter. */
	rue_setmulti(ue);

	/* Enable RX and TX */
	rue_csr_write_1(sc, RUE_CR, (RUE_CR_TE | RUE_CR_RE | RUE_CR_EP3CLREN));

	usb2_transfer_set_stall(sc->sc_xfer[RUE_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	rue_start(ue);
}

/*
 * Set media options.
 */
static int
rue_ifmedia_upd(struct ifnet *ifp)
{
	struct rue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	RUE_LOCK_ASSERT(sc, MA_OWNED);

        sc->sc_flags &= ~RUE_FLAG_LINK;
	if (mii->mii_instance) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);
	return (0);
}

/*
 * Report current media status.
 */
static void
rue_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	RUE_LOCK(sc);
	mii_pollstat(mii);
	RUE_UNLOCK(sc);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static void
rue_stop(struct usb2_ether *ue)
{
	struct rue_softc *sc = usb2_ether_getsc(ue);
	struct ifnet *ifp = usb2_ether_getifp(ue);

	RUE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sc->sc_flags &= ~RUE_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usb2_transfer_stop(sc->sc_xfer[RUE_BULK_DT_WR]);
	usb2_transfer_stop(sc->sc_xfer[RUE_BULK_DT_RD]);
	usb2_transfer_stop(sc->sc_xfer[RUE_INTR_DT_RD]);

	rue_csr_write_1(sc, RUE_CR, 0x00);

	rue_reset(sc);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
rue_shutdown(device_t dev)
{
	struct rue_softc *sc = device_get_softc(dev);

	usb2_ether_ifshutdown(&sc->sc_ue);

	return (0);
}
