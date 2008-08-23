/*-
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
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

#ifndef _USB2_IOCTL_H_
#define	_USB2_IOCTL_H_

#include <sys/ioccom.h>

#define	USB_DEVICE_NAME "usb"

struct usb2_read_dir {
	void   *urd_data;
	uint32_t urd_startentry;
	uint32_t urd_maxlen;
};

struct usb2_ctl_request {
	void   *ucr_data;
	uint16_t ucr_flags;
#define	USB_USE_POLLING         0x0001	/* internal flag */
#define	USB_SHORT_XFER_OK       0x0004	/* allow short reads */
#define	USB_DELAY_STATUS_STAGE  0x0010	/* insert delay before STATUS stage */
	uint16_t ucr_actlen;		/* actual length transferred */
	uint8_t	ucr_addr;		/* zero - currently not used */
	struct usb2_device_request ucr_request;
};

struct usb2_alt_interface {
	uint8_t	uai_interface_index;
	uint8_t	uai_alt_index;
};

struct usb2_gen_descriptor {
	void   *ugd_data;
	uint16_t ugd_lang_id;
	uint16_t ugd_maxlen;
	uint16_t ugd_actlen;
	uint16_t ugd_offset;
	uint8_t	ugd_config_index;
	uint8_t	ugd_string_index;
	uint8_t	ugd_iface_index;
	uint8_t	ugd_altif_index;
	uint8_t	ugd_endpt_index;
	uint8_t	ugd_report_type;
	uint8_t	reserved[8];
};

struct usb2_device_names {
	char   *udn_devnames_ptr;	/* userland pointer to comma separated
					 * list of device names */
	uint16_t udn_devnames_len;	/* maximum string length including
					 * terminating zero */
};

struct usb2_device_info {
	uint16_t udi_productNo;
	uint16_t udi_vendorNo;
	uint16_t udi_releaseNo;
	uint16_t udi_power;		/* power consumption in mA, 0 if
					 * selfpowered */
	uint8_t	udi_bus;
	uint8_t	udi_addr;		/* device address */
	uint8_t	udi_index;		/* device index */
	uint8_t	udi_class;
	uint8_t	udi_subclass;
	uint8_t	udi_protocol;
	uint8_t	udi_config_no;		/* current config number */
	uint8_t	udi_config_index;	/* current config index */
	uint8_t	udi_speed;		/* see "USB_SPEED_XXX" */
	uint8_t	udi_mode;		/* see "USB_MODE_XXX" */
	uint8_t	udi_nports;
	uint8_t	udi_hubaddr;		/* parent HUB address */
	uint8_t	udi_hubindex;		/* parent HUB device index */
	uint8_t	udi_hubport;		/* parent HUB port */
	uint8_t	udi_devstate;
#define	USB_DEVSTATE_ENABLED 0x0
#define	USB_DEVSTATE_SUSPENDED 0x1
#define	USB_DEVSTATE_POWERED 0x2
#define	USB_DEVSTATE_DISABLED 0x3
	char	udi_product[128];
	char	udi_vendor[128];
	char	udi_serial[64];
	char	udi_release[8];
};

struct usb2_device_stats {
	uint32_t uds_requests_ok[4];	/* Indexed by transfer type UE_XXX */
	uint32_t uds_requests_fail[4];	/* Indexed by transfer type UE_XXX */
};

struct usb2_fs_start {
	uint8_t	ep_index;
};

struct usb2_fs_stop {
	uint8_t	ep_index;
};

struct usb2_fs_complete {
	uint8_t	ep_index;
};

/* This structure is used for all endpoint types */
struct usb2_fs_endpoint {
	/*
	 * NOTE: isochronous USB transfer only use one buffer, but can have
	 * multiple frame lengths !
	 */
	void  **ppBuffer;		/* pointer to userland buffers */
	uint32_t *pLength;		/* pointer to frame lengths, updated
					 * to actual length */
	uint32_t nFrames;		/* number of frames */
	uint32_t aFrames;		/* actual number of frames */
	uint16_t flags;
	/* a single short frame will terminate */
#define	USB_FS_FLAG_SINGLE_SHORT_OK 0x0001
	/* multiple short frames are allowed */
#define	USB_FS_FLAG_MULTI_SHORT_OK 0x0002
	/* all frame(s) transmitted are short terminated */
#define	USB_FS_FLAG_FORCE_SHORT 0x0004
	/* will do a clear-stall before xfer */
#define	USB_FS_FLAG_CLEAR_STALL 0x0008
	uint16_t timeout;		/* in milliseconds */
	/* timeout value for no timeout */
#define	USB_FS_TIMEOUT_NONE 0
	uint8_t	status;			/* see USB_ERR_XXX */
};

struct usb2_fs_init {
	/* userland pointer to endpoints structure */
	struct usb2_fs_endpoint *pEndpoints;
	/* maximum number of endpoints */
	uint8_t	ep_index_max;
};

struct usb2_fs_uninit {
	uint8_t	dummy;			/* zero */
};

struct usb2_fs_open {
#define	USB_FS_MAX_BUFSIZE (1 << 18)
	uint32_t max_bufsize;
#define	USB_FS_MAX_FRAMES (1 << 12)
	uint32_t max_frames;
	uint16_t max_packet_length;	/* read only */
	uint8_t	dev_index;		/* currently unused */
	uint8_t	ep_index;
	uint8_t	ep_no;			/* bEndpointNumber */
};

struct usb2_fs_close {
	uint8_t	ep_index;
};

struct usb2_fs_clear_stall_sync {
	uint8_t	ep_index;
};

struct usb2_dev_perm {
	/* Permissions */
	uint16_t user_id;
	uint16_t group_id;
	uint16_t mode;

	/* Device location */
	uint16_t bus_index;
	uint16_t dev_index;
	uint16_t iface_index;
};

/* USB controller */
#define	USB_REQUEST		_IOWR('U', 1, struct usb2_ctl_request)
#define	USB_SETDEBUG		_IOW ('U', 2, int)
#define	USB_DISCOVER		_IO  ('U', 3)
#define	USB_DEVICEINFO		_IOWR('U', 4, struct usb2_device_info)
#define	USB_DEVICESTATS		_IOR ('U', 5, struct usb2_device_stats)
#define	USB_DEVICEENUMERATE	_IOW ('U', 6, int)

/* Generic HID device */
#define	USB_GET_REPORT_DESC	_IOR ('U', 21, struct usb2_gen_descriptor)
#define	USB_SET_IMMED		_IOW ('U', 22, int)
#define	USB_GET_REPORT		_IOWR('U', 23, struct usb2_gen_descriptor)
#define	USB_SET_REPORT		_IOW ('U', 24, struct usb2_gen_descriptor)
#define	USB_GET_REPORT_ID	_IOR ('U', 25, int)

/* Generic USB device */
#define	USB_GET_CONFIG		_IOR ('U', 100, int)
#define	USB_SET_CONFIG		_IOW ('U', 101, int)
#define	USB_GET_ALTINTERFACE	_IOWR('U', 102, struct usb2_alt_interface)
#define	USB_SET_ALTINTERFACE	_IOWR('U', 103, struct usb2_alt_interface)
#define	USB_GET_DEVICE_DESC	_IOR ('U', 105, struct usb2_device_descriptor)
#define	USB_GET_CONFIG_DESC	_IOR ('U', 106, struct usb2_config_descriptor)
#define	USB_GET_INTERFACE_DESC	_IOR ('U', 107, struct usb2_interface_descriptor)
#define	USB_GET_ENDPOINT_DESC	_IOR ('U', 108, struct usb2_endpoint_descriptor)
#define	USB_GET_FULL_DESC	_IOWR('U', 109, struct usb2_gen_descriptor)
#define	USB_GET_STRING_DESC	_IOWR('U', 110, struct usb2_gen_descriptor)
#define	USB_DO_REQUEST		_IOWR('U', 111, struct usb2_ctl_request)
#define	USB_GET_DEVICEINFO	_IOR ('U', 112, struct usb2_device_info)
#define	USB_SET_SHORT_XFER	_IOW ('U', 113, int)
#define	USB_SET_TIMEOUT		_IOW ('U', 114, uint32_t)
#define	USB_GET_FRAME_SIZE	_IOR ('U', 115, uint32_t)
#define	USB_GET_BUFFER_SIZE	_IOR ('U', 117, uint32_t)
#define	USB_SET_BUFFER_SIZE	_IOW ('U', 118, uint32_t)
#define	USB_SET_RX_STALL_FLAG	_IOW ('U', 119, int)
#define	USB_SET_TX_STALL_FLAG	_IOW ('U', 120, int)
#define	USB_GET_DEVICENAMES	_IOW ('U', 121, struct usb2_device_names)
#define	USB_CLAIM_INTERFACE	_IOW ('U', 122, int)
#define	USB_RELEASE_INTERFACE	_IOW ('U', 123, int)
#define	USB_IFACE_DRIVER_ACTIVE	_IOW ('U', 124, int)
#define	USB_IFACE_DRIVER_DETACH	_IOW ('U', 125, int)
#define	USB_GET_PLUGTIME	_IOR ('U', 126, uint32_t)
#define	USB_READ_DIR		_IOW ('U', 127, struct usb2_read_dir)
#define	USB_SET_ROOT_PERM	_IOW ('U', 128, struct usb2_dev_perm)
#define	USB_SET_BUS_PERM	_IOW ('U', 129, struct usb2_dev_perm)
#define	USB_SET_DEVICE_PERM	_IOW ('U', 130, struct usb2_dev_perm)
#define	USB_SET_IFACE_PERM	_IOW ('U', 131, struct usb2_dev_perm)
#define	USB_GET_ROOT_PERM	_IOW ('U', 132, struct usb2_dev_perm)
#define	USB_GET_BUS_PERM	_IOW ('U', 133, struct usb2_dev_perm)
#define	USB_GET_DEVICE_PERM	_IOW ('U', 134, struct usb2_dev_perm)
#define	USB_GET_IFACE_PERM	_IOW ('U', 135, struct usb2_dev_perm)

/* Modem device */
#define	USB_GET_CM_OVER_DATA	_IOR ('U', 160, int)
#define	USB_SET_CM_OVER_DATA	_IOW ('U', 161, int)

/* USB file system interface */
#define	USB_FS_START		_IOW ('U', 192, struct usb2_fs_start)
#define	USB_FS_STOP		_IOW ('U', 193, struct usb2_fs_stop)
#define	USB_FS_COMPLETE		_IOR ('U', 194, struct usb2_fs_complete)
#define	USB_FS_INIT		_IOW ('U', 195, struct usb2_fs_init)
#define	USB_FS_UNINIT		_IOW ('U', 196, struct usb2_fs_uninit)
#define	USB_FS_OPEN		_IOWR('U', 197, struct usb2_fs_open)
#define	USB_FS_CLOSE		_IOW ('U', 198, struct usb2_fs_close)
#define	USB_FS_CLEAR_STALL_SYNC _IOW ('U', 199, struct usb2_fs_clear_stall_sync)

#endif					/* _USB2_IOCTL_H_ */