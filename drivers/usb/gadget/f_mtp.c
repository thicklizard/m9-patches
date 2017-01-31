/*
 * Gadget Function Driver for MTP
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/ch9.h>
#include <linux/usb/f_mtp.h>

#define MTP_BULK_BUFFER_SIZE       16384
#define INTR_BUFFER_SIZE           28
/*++ 2014/11/20, USB Team, PCN00042 ++*/
#define MTP_THREAD_UNSUPPORT	0
#define MTP_THREAD_SUPPORT	1
/*-- 2014/11/20, USB Team, PCN00042 --*/

/* String IDs */
#define INTERFACE_STRING_INDEX	0

/* values for mtp_dev.state */
#define STATE_OFFLINE               0   /* initial state, disconnected */
#define STATE_READY                 1   /* ready for userspace calls */
#define STATE_BUSY                  2   /* processing userspace calls */
#define STATE_CANCELED              3   /* transaction canceled by host */
#define STATE_ERROR                 4   /* error from completion routine */

/* number of tx and rx requests to allocate */
#define MTP_TX_REQ_MAX 8
/*++ 2014/11/20, USB Team, PCN00042 ++*/
#define MTP_RX_REQ_MAX 8
/*-- 2014/11/20, USB Team, PCN00042 --*/
#define INTR_REQ_MAX 5

/* ID for Microsoft MTP OS String */
#define MTP_OS_STRING_ID   0xEE

/* MTP class reqeusts */
#define MTP_REQ_CANCEL              0x64
#define MTP_REQ_GET_EXT_EVENT_DATA  0x65
#define MTP_REQ_RESET               0x66
#define MTP_REQ_GET_DEVICE_STATUS   0x67

/* constants for device status */
#define MTP_RESPONSE_OK             0x2001
#define MTP_RESPONSE_DEVICE_BUSY    0x2019
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
#include <mach/perflock.h>
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/

unsigned int mtp_rx_req_len = MTP_BULK_BUFFER_SIZE;
module_param(mtp_rx_req_len, uint, S_IRUGO | S_IWUSR);

unsigned int mtp_tx_req_len = MTP_BULK_BUFFER_SIZE;
module_param(mtp_tx_req_len, uint, S_IRUGO | S_IWUSR);

unsigned int mtp_tx_reqs = MTP_TX_REQ_MAX;
module_param(mtp_tx_reqs, uint, S_IRUGO | S_IWUSR);
/*++ 2014/10/21, USB Team, PCN00019 ++*/
static int htc_mtp_open_state;
/*-- 2014/10/21, USB Team, PCN00019 --*/

static const char mtp_shortname[] = "mtp_usb";

struct mtp_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;
	struct usb_ep *ep_intr;

	int state;

/*++ 2014/11/20, USB Team, PCN00042 ++*/
	atomic_t read_excl;
	atomic_t write_excl;

	/* the request we're currently reading from */
	struct usb_request *rx_req;
	unsigned char *read_buf;
	uint64_t read_count;

	struct list_head rx_idle;
	struct list_head rx_done;

	struct timeval st0;
	struct timeval st1;
/*-- 2014/11/20, USB Team, PCN00042 --*/

	/* synchronize access to our device file */
	atomic_t open_excl;
	/* to enforce only one ioctl at a time */
	atomic_t ioctl_excl;

	struct list_head tx_idle;
	struct list_head intr_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	wait_queue_head_t intr_wq;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	/*struct usb_request *rx_req[RX_REQ_MAX];*/
	/*int rx_done;*/
/*-- 2014/11/20, USB Team, PCN00042 --*/

	/* for processing MTP_SEND_FILE, MTP_RECEIVE_FILE and
	 * MTP_SEND_FILE_WITH_HEADER ioctls on a work queue
	 */
	struct workqueue_struct *wq;
	struct work_struct send_file_work;
	struct work_struct receive_file_work;
	struct file *xfer_file;
	loff_t xfer_file_offset;
	int64_t xfer_file_length;
	unsigned xfer_send_header;
	uint16_t xfer_command;
	uint32_t xfer_transaction_id;
	int xfer_result;

/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	struct work_struct release_perflock_work;
	bool mtp_perf_lock_on;
	struct perf_lock perf_lock;
	struct timer_list perf_timer;
	unsigned long timer_expired;
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
};

static struct usb_interface_descriptor mtp_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 3,
	.bInterfaceClass        = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass     = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol     = 0,
};

static struct usb_interface_descriptor ptp_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 3,
	.bInterfaceClass        = USB_CLASS_STILL_IMAGE,
	.bInterfaceSubClass     = 1,
	.bInterfaceProtocol     = 1,
};

static struct usb_endpoint_descriptor mtp_superspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor mtp_superspeed_in_comp_desc = {
	.bLength =		sizeof mtp_superspeed_in_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	.bMaxBurst =		2,
	/* .bmAttributes =	0, */
};

static struct usb_endpoint_descriptor mtp_superspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor mtp_superspeed_out_comp_desc = {
	.bLength =		sizeof mtp_superspeed_out_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	 .bMaxBurst =		2,
	/* .bmAttributes =	0, */
};

static struct usb_endpoint_descriptor mtp_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor mtp_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor mtp_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor mtp_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor mtp_intr_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize         = __constant_cpu_to_le16(INTR_BUFFER_SIZE),
	.bInterval              = 6,
};

static struct usb_ss_ep_comp_descriptor mtp_superspeed_intr_comp_desc = {
	.bLength =		sizeof mtp_superspeed_intr_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 3 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
	.wBytesPerInterval =	cpu_to_le16(INTR_BUFFER_SIZE),
};

static struct usb_descriptor_header *fs_mtp_descs[] = {
	(struct usb_descriptor_header *) &mtp_interface_desc,
	(struct usb_descriptor_header *) &mtp_fullspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_fullspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	NULL,
};

static struct usb_descriptor_header *hs_mtp_descs[] = {
	(struct usb_descriptor_header *) &mtp_interface_desc,
	(struct usb_descriptor_header *) &mtp_highspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_highspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	NULL,
};

static struct usb_descriptor_header *ss_mtp_descs[] = {
	(struct usb_descriptor_header *) &mtp_interface_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_in_comp_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_out_comp_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_intr_comp_desc,
	NULL,
};

static struct usb_descriptor_header *fs_ptp_descs[] = {
	(struct usb_descriptor_header *) &ptp_interface_desc,
	(struct usb_descriptor_header *) &mtp_fullspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_fullspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	NULL,
};

static struct usb_descriptor_header *hs_ptp_descs[] = {
	(struct usb_descriptor_header *) &ptp_interface_desc,
	(struct usb_descriptor_header *) &mtp_highspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_highspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	NULL,
};

static struct usb_descriptor_header *ss_ptp_descs[] = {
	(struct usb_descriptor_header *) &ptp_interface_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_in_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_in_comp_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_out_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_out_comp_desc,
	(struct usb_descriptor_header *) &mtp_intr_desc,
	(struct usb_descriptor_header *) &mtp_superspeed_intr_comp_desc,
	NULL,
};

static struct usb_string mtp_string_defs[] = {
	/* Naming interface "MTP" so libmtp will recognize us */
	[INTERFACE_STRING_INDEX].s	= "MTP",
	{  },	/* end of list */
};

static struct usb_gadget_strings mtp_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= mtp_string_defs,
};

static struct usb_gadget_strings *mtp_strings[] = {
	&mtp_string_table,
	NULL,
};

/* Microsoft MTP OS String */
static u8 mtp_os_string[] = {
	18, /* sizeof(mtp_os_string) */
	USB_DT_STRING,
	/* Signature field: "MSFT100" */
	'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
	/* vendor code */
	1,
	/* padding */
	0
};

/* Microsoft Extended Configuration Descriptor Header Section */
struct mtp_ext_config_desc_header {
	__le32	dwLength;
	__u16	bcdVersion;
	__le16	wIndex;
	__u8	bCount;
	__u8	reserved[7];
};

/* Microsoft Extended Configuration Descriptor Function Section */
struct mtp_ext_config_desc_function {
	__u8	bFirstInterfaceNumber;
	__u8	bInterfaceCount;
	__u8	compatibleID[8];
	__u8	subCompatibleID[8];
	__u8	reserved[6];
};

/* MTP Extended Configuration Descriptor */
struct {
	struct mtp_ext_config_desc_header	header;
	struct mtp_ext_config_desc_function    function;
} mtp_ext_config_desc = {
	.header = {
		.dwLength = __constant_cpu_to_le32(sizeof(mtp_ext_config_desc)),
		.bcdVersion = __constant_cpu_to_le16(0x0100),
		.wIndex = __constant_cpu_to_le16(4),
		.bCount = __constant_cpu_to_le16(1),
	},
	.function = {
		.bFirstInterfaceNumber = 0,
		.bInterfaceCount = 1,
		.compatibleID = { 'M', 'T', 'P' },
	},
};

struct mtp_device_status {
	__le16	wLength;
	__le16	wCode;
};

struct mtp_data_header {
	/* length of packet, including this header */
	__le32	length;
	/* container type (2 for data packet) */
	__le16	type;
	/* MTP command code */
	__le16	command;
	/* MTP transaction ID */
	__le32	transaction_id;
};

/* temporary variable used between mtp_open() and mtp_gadget_bind() */
static struct mtp_dev *_mtp_dev;

/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK

static void mtp_setup_perflock(bool mtp_perf_lock_on)
{
	struct mtp_dev *dev = _mtp_dev;
	dev->mtp_perf_lock_on = mtp_perf_lock_on;

	/* reset the timer */
	del_timer(&dev->perf_timer);
	if (mtp_perf_lock_on) {
		if (!is_perf_lock_active(&dev->perf_lock)) {
			printk(KERN_INFO "[USB][MTP] %s, perf on\n", __func__);
			perf_lock(&dev->perf_lock);
		}
	} else {
		if (is_perf_lock_active(&dev->perf_lock)) {
			printk(KERN_INFO "[USB][MTP] %s, perf off\n", __func__);
			perf_unlock(&dev->perf_lock);
		}
	}
}

static __maybe_unused void release_perflock_work_func(struct work_struct *data)
{
	mtp_setup_perflock(false);
}

/* 15ms per file */
#define MTP_QOS_N_RATIO     15
#define MTP_TRANSFER_EXPIRED    (jiffies + msecs_to_jiffies(5000))
static void mtp_qos_enable(int qos_n)
{
	struct mtp_dev *dev = _mtp_dev;

	if (qos_n) {
		mtp_setup_perflock(true);
		dev->timer_expired = qos_n * MTP_QOS_N_RATIO;
		if (dev->timer_expired < 5000)
			dev->timer_expired = 5000;
		mod_timer(&dev->perf_timer,
				jiffies + msecs_to_jiffies(dev->timer_expired));
	} else
		mtp_setup_perflock(false);
}

static __maybe_unused void mtp_perf_lock_disable(unsigned long data)
{
	struct mtp_dev *dev = _mtp_dev;
	schedule_work(&dev->release_perflock_work);
}
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/

static inline struct mtp_dev *func_to_mtp(struct usb_function *f)
{
	return container_of(f, struct mtp_dev, function);
}

static struct usb_request *mtp_request_new(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void mtp_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static inline int mtp_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void mtp_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

/* add a request to the tail of a list */
static void mtp_req_put(struct mtp_dev *dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
static struct usb_request
*mtp_req_get(struct mtp_dev *dev, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&dev->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return req;
}

static void mtp_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct mtp_dev *dev = _mtp_dev;

	if (req->status != 0)
		dev->state = STATE_ERROR;

	mtp_req_put(dev, &dev->tx_idle, req);

	wake_up(&dev->write_wq);
}

static void mtp_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct mtp_dev *dev = _mtp_dev;
/*-- 2014/11/20, USB Team, PCN00042 --*/
	struct usb_composite_dev *cdev = dev->cdev;

	if (req->status != 0) {
		dev->state = STATE_ERROR;
		DBG(cdev, "%s: %p rx_idle status: %d\n", __func__, req, req->status);
		mtp_req_put(dev, &dev->rx_idle, req);
	} else if (dev->state == STATE_OFFLINE) {
		DBG(cdev, "%s: %p rx_idle offlin: %d\n", __func__, req, req->status);
		mtp_req_put(dev, &dev->rx_idle, req);
	} else {
		DBG(cdev, "%s: %p rx_done\n", __func__, req);
		mtp_req_put(dev, &dev->rx_done, req);
	}
/*-- 2014/11/20, USB Team, PCN00042 --*/
	wake_up(&dev->read_wq);
}

static void mtp_complete_intr(struct usb_ep *ep, struct usb_request *req)
{
	struct mtp_dev *dev = _mtp_dev;

	if (req->status != 0)
		dev->state = STATE_ERROR;

	mtp_req_put(dev, &dev->intr_idle, req);

	wake_up(&dev->intr_wq);
}

static int mtp_create_bulk_endpoints(struct mtp_dev *dev,
				struct usb_endpoint_descriptor *in_desc,
				struct usb_endpoint_descriptor *out_desc,
				struct usb_endpoint_descriptor *intr_desc)
{
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;

	DBG(cdev, "create_bulk_endpoints dev: %p\n", dev);

	ep = usb_ep_autoconfig(cdev->gadget, in_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_in failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_in got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for mtp ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, intr_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_intr failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for mtp ep_intr got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_intr = ep;

retry_tx_alloc:
	if (mtp_tx_req_len > MTP_BULK_BUFFER_SIZE)
		mtp_tx_reqs = 4;

	/* now allocate requests for our endpoints */
	for (i = 0; i < mtp_tx_reqs; i++) {
		req = mtp_request_new(dev->ep_in, mtp_tx_req_len);
		if (!req) {
			if (mtp_tx_req_len <= MTP_BULK_BUFFER_SIZE)
				goto fail;
			while ((req = mtp_req_get(dev, &dev->tx_idle)))
				mtp_request_free(req, dev->ep_in);
			mtp_tx_req_len = MTP_BULK_BUFFER_SIZE;
			mtp_tx_reqs = MTP_TX_REQ_MAX;
			goto retry_tx_alloc;
		}
		req->complete = mtp_complete_in;
		mtp_req_put(dev, &dev->tx_idle, req);
	}

	/*
	 * The RX buffer should be aligned to EP max packet for
	 * some controllers.  At bind time, we don't know the
	 * operational speed.  Hence assuming super speed max
	 * packet size.
	 */
	if (mtp_rx_req_len % 1024)
		mtp_rx_req_len = MTP_BULK_BUFFER_SIZE;

/*-- 2014/11/20, USB Team, PCN00042 --*/
retry_rx_alloc:
	for (i = 0; i < MTP_RX_REQ_MAX; i++) {
		req = mtp_request_new(dev->ep_out, mtp_rx_req_len);
		if (!req) {
			if (mtp_rx_req_len <= MTP_BULK_BUFFER_SIZE)
				goto fail;
			while ((req = mtp_req_get(dev, &dev->rx_idle)))
				mtp_request_free(req, dev->ep_out);
			mtp_rx_req_len = MTP_BULK_BUFFER_SIZE;
			goto retry_rx_alloc;
		}
		req->complete = mtp_complete_out;
		mtp_req_put(dev, &dev->rx_idle, req);
/*-- 2014/11/20, USB Team, PCN00042 --*/
	}
	for (i = 0; i < INTR_REQ_MAX; i++) {
		req = mtp_request_new(dev->ep_intr, INTR_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = mtp_complete_intr;
		mtp_req_put(dev, &dev->intr_idle, req);
	}

	return 0;

fail:
	printk(KERN_ERR "mtp_bind() could not allocate requests\n");
	return -1;
}

static ssize_t mtp_read(struct file *fp, char __user *buf,
	size_t count, loff_t *pos)
{
	struct mtp_dev *dev = fp->private_data;
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	int r = 0, xfer;
	int ret = 0, file_xfer_zlp_flag = 0;

	DBG(cdev, "mtp_read(%zu)\n", count);

	if (mtp_lock(&dev->read_excl))
		return -EBUSY;
/*-- 2014/11/20, USB Team, PCN00042 --*/

	/* we will block until we're online */
	DBG(cdev, "mtp_read: waiting for online state\n");
	ret = wait_event_interruptible(dev->read_wq,
		dev->state != STATE_OFFLINE);
	if (ret < 0) {
		r = ret;
		goto done;
	}
	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED) {
		/* report cancelation to userspace */
		dev->state = STATE_READY;
		spin_unlock_irq(&dev->lock);
/*++ 2014/11/20, USB Team, PCN00042 ++*/
		r = -ECANCELED;
		goto done;
	}
	if (dev->state == STATE_OFFLINE) {
		spin_unlock_irq(&dev->lock);
		r = -ENODEV;
		goto done;
	}
	dev->state = STATE_BUSY;
	spin_unlock_irq(&dev->lock);

	/* We should have best peformance while transferring huge packet */
	if (count > MTP_BULK_BUFFER_SIZE) {
		file_xfer_zlp_flag = 1;
	/*Enable MTP QoS*/
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
		mtp_qos_enable(1);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
	}
/*-- 2014/11/20, USB Team, PCN00042 --*/

/*++ 2014/11/20, USB Team, PCN00042 ++*/
	while (count > 0) {
		if (dev->state == STATE_CANCELED) {
/*++ 2014/12/29, USB Team, PCN00062 ++*/
			usb_ep_nuke(dev->ep_out);
/*-- 2014/12/29, USB Team, PCN00062 --*/
			while ((req = mtp_req_get(dev, &dev->rx_done)))
				mtp_req_put(dev, &dev->rx_idle, req);
			dev->read_count = 0;
			r = -ECANCELED;
			break;
		} else if (unlikely(dev->state == STATE_OFFLINE)) {
			dev->read_count = 0;
			r = -EIO;
			goto done;
		}

		/* if we have idle read requests, get them queued */
		while ((req = mtp_req_get(dev, &dev->rx_idle))) {
requeue_req:
			#if 0
			req->length = dev->maxsize?dev->maxsize:512;
			#endif
			req->length = MTP_BULK_BUFFER_SIZE;
			DBG(cdev, "%s: queue request(%p) on %s\n", __func__, req, dev->ep_out->name);
			ret = usb_ep_queue(dev->ep_out, req, GFP_ATOMIC);
			if (ret < 0) {
				INFO(cdev, "%s: failed to queue req %p (%d)\n", __func__, req, ret);
				r = -EIO;
				mtp_req_put(dev, &dev->rx_idle, req);
				dev->read_count = 0;
				goto done;
			}
			if (dev->state == STATE_OFFLINE || dev->state == STATE_ERROR) {
				printk(KERN_INFO "[USB][MTP] hit potential WDT issue (%d)\n", dev->state);
				dev->read_count = 0;
				r = -EIO;
				goto done;
			}
		}

		/* if we have data pending, give it to userspace */
		if (dev->read_count > 0) {
			DBG(cdev, "%s: read %llu bytes @ %p\n", __func__,
				dev->read_count, dev->rx_req);

			xfer = (dev->read_count < count) ? dev->read_count : count;

			if (copy_to_user(buf, dev->read_buf, xfer)) {
				r = -EFAULT;
				break;
			}
			dev->read_buf += xfer;
			dev->read_count -= xfer;
			buf += xfer;
			count -= xfer;
			r += xfer;

			/* if we've emptied the buffer, release the request */
			if (dev->read_count == 0 && dev->rx_req) {
				mtp_req_put(dev, &dev->rx_idle, dev->rx_req);
				dev->rx_req = 0;
			}

			/* short packet found */
			if (xfer < MTP_BULK_BUFFER_SIZE) {
				dev->read_count = 0;
				break;
			}
			continue;
		}

		/* wait for a request to complete */
		req = 0;
		ret = wait_event_interruptible(dev->read_wq,
				((req = mtp_req_get(dev, &dev->rx_done))
				 || dev->state != STATE_BUSY));

		if (req != 0) {
			/* if we got a 0-len one we need to put it back into
			 ** service.  if we made it the current read req we'd
			 ** be stuck forever
			 */
			if (req->actual == 0) {
				if (file_xfer_zlp_flag == 0)
					goto requeue_req;
					dev->rx_req = req;
					mtp_req_put(dev, &dev->rx_idle, dev->rx_req);
					INFO(cdev, "%s: got ZLP while file xfer.\n", __func__);
					break;
				}
				dev->rx_req = req;
				dev->read_count = req->actual;
				dev->read_buf = req->buf;
		}

		if (ret < 0) {
			r = ret;
			break;
		}
	}
/*-- 2014/11/20, USB Team, PCN00042 --*/

done:
	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED)
		r = -ECANCELED;
	else if (dev->state != STATE_OFFLINE)
		dev->state = STATE_READY;
	spin_unlock_irq(&dev->lock);
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	mtp_unlock(&dev->read_excl);
/*-- 2014/11/20, USB Team, PCN00042 --*/

	DBG(cdev, "mtp_read returning %d\n", r);
	return r;
}

static ssize_t mtp_write(struct file *fp, const char __user *buf,
	size_t count, loff_t *pos)
{
	struct mtp_dev *dev = fp->private_data;
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req = 0;
	ssize_t r = count;
	unsigned xfer;
	int sendZLP = 0;
	int ret;

	DBG(cdev, "mtp_write(%zu)\n", count);

	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED) {
		/* report cancelation to userspace */
		dev->state = STATE_READY;
		spin_unlock_irq(&dev->lock);
		return -ECANCELED;
	}
	if (dev->state == STATE_OFFLINE) {
		spin_unlock_irq(&dev->lock);
		return -ENODEV;
	}
	dev->state = STATE_BUSY;
	spin_unlock_irq(&dev->lock);

	/* we need to send a zero length packet to signal the end of transfer
	 * if the transfer size is aligned to a packet boundary.
	 */
	if ((count & (dev->ep_in->maxpacket - 1)) == 0)
		sendZLP = 1;

	while (count > 0 || sendZLP) {
		/* so we exit after sending ZLP */
		if (count == 0)
			sendZLP = 0;

		if (dev->state != STATE_BUSY) {
/*++ 2014/11/20, USB Team, PCN00042 ++*/
			INFO(cdev, "mtp_write dev->error, state=%d\n", dev->state);
/*-- 2014/11/20, USB Team, PCN00042 --*/
			r = -EIO;
			break;
		}

		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(dev->write_wq,
			((req = mtp_req_get(dev, &dev->tx_idle))
				|| dev->state != STATE_BUSY));
		if (!req) {
			r = ret;
			break;
		}

		if (count > mtp_tx_req_len)
			xfer = mtp_tx_req_len;
		else
			xfer = count;
		if (xfer && copy_from_user(req->buf, buf, xfer)) {
			r = -EFAULT;
			break;
		}

		req->length = xfer;
		ret = usb_ep_queue(dev->ep_in, req, GFP_KERNEL);
		if (ret < 0) {
/*++ 2014/11/20, USB Team, PCN00042 ++*/
			INFO(cdev, "mtp_write: xfer error %d\n", ret);
/*-- 2014/11/20, USB Team, PCN00042 --*/
			r = -EIO;
			break;
		}

		buf += xfer;
		count -= xfer;

		/* zero this so we don't try to free it on error exit */
		req = 0;
	}

	if (req)
		mtp_req_put(dev, &dev->tx_idle, req);

	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED)
		r = -ECANCELED;
	else if (dev->state != STATE_OFFLINE)
		dev->state = STATE_READY;
	spin_unlock_irq(&dev->lock);

	DBG(cdev, "mtp_write returning %zd\n", r);
	return r;
}

/* read from a local file and write to USB */
static void send_file_work(struct work_struct *data)
{
	struct mtp_dev *dev = container_of(data, struct mtp_dev,
						send_file_work);
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req = 0;
	struct mtp_data_header *header;
	struct file *filp;
	loff_t offset;
	int64_t count;
	int xfer, ret, hdr_size;
	int r = 0;
	int sendZLP = 0;

	/* read our parameters */
	smp_rmb();
	filp = dev->xfer_file;
	offset = dev->xfer_file_offset;
	count = dev->xfer_file_length;

	DBG(cdev, "send_file_work(%lld %lld)\n", offset, count);

	if (dev->xfer_send_header) {
		hdr_size = sizeof(struct mtp_data_header);
		count += hdr_size;
	} else {
		hdr_size = 0;
	}

	/* we need to send a zero length packet to signal the end of transfer
	 * if the transfer size is aligned to a packet boundary.
	 */
	if ((count & (dev->ep_in->maxpacket - 1)) == 0)
		sendZLP = 1;

	while (count > 0 || sendZLP) {
		/* so we exit after sending ZLP */
		if (count == 0)
			sendZLP = 0;

		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(dev->write_wq,
			(req = mtp_req_get(dev, &dev->tx_idle))
			|| dev->state != STATE_BUSY);
		if (dev->state == STATE_CANCELED) {
			r = -ECANCELED;
			break;
		}
		if (!req) {
			r = ret;
			break;
		}

		if (count > mtp_tx_req_len)
			xfer = mtp_tx_req_len;
		else
			xfer = count;

		if (hdr_size) {
			/* prepend MTP data header */
			header = (struct mtp_data_header *)req->buf;
			header->length = __cpu_to_le32(count);
			header->type = __cpu_to_le16(2); /* data packet */
			header->command = __cpu_to_le16(dev->xfer_command);
			header->transaction_id =
					__cpu_to_le32(dev->xfer_transaction_id);
		}

		ret = vfs_read(filp, req->buf + hdr_size, xfer - hdr_size,
								&offset);
		if (ret < 0) {
			r = ret;
			break;
		}
		xfer = ret + hdr_size;
		hdr_size = 0;

		req->length = xfer;
		ret = usb_ep_queue(dev->ep_in, req, GFP_KERNEL);
		if (ret < 0) {
			DBG(cdev, "send_file_work: xfer error %d\n", ret);
			if (dev->state != STATE_OFFLINE)
				dev->state = STATE_ERROR;
			r = -EIO;
			break;
		}

		count -= xfer;

		/* zero this so we don't try to free it on error exit */
		req = 0;
	}

	if (req)
		mtp_req_put(dev, &dev->tx_idle, req);

/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	mod_timer(&dev->perf_timer, MTP_TRANSFER_EXPIRED);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/

	DBG(cdev, "send_file_work returning %d\n", r);
	/* write the result */
	dev->xfer_result = r;
	smp_wmb();
}

/* read from USB and write to a local file */
static void receive_file_work(struct work_struct *data)
{
	struct mtp_dev *dev = container_of(data, struct mtp_dev,
						receive_file_work);
	struct usb_composite_dev *cdev = dev->cdev;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	struct usb_request *req = NULL;
/*-- 2014/11/20, USB Team, PCN00042 --*/
	struct file *filp;
	loff_t offset;
	int64_t count;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	int ret;
	int r = 0, xfer, times = 0, file_xfer_zlp_flag = 0;
/*-- 2014/11/20, USB Team, PCN00042 --*/

	/* read our parameters */
	smp_rmb();
	filp = dev->xfer_file;
	offset = dev->xfer_file_offset;
	count = dev->xfer_file_length;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	dev->read_count = 0;

	DBG(cdev, "receive_file_work(%lld)\n", count);

	if (count == 0xFFFFFFFF)
		file_xfer_zlp_flag = 1;

	while (count > 0) {
		if (dev->state == STATE_CANCELED ) {
/*++ 2014/12/29, USB Team, PCN00062 ++*/
			usb_ep_nuke(dev->ep_out);
/*-- 2014/12/29, USB Team, PCN00062 --*/
			while ((req = mtp_req_get(dev, &dev->rx_done)))
				mtp_req_put(dev, &dev->rx_idle, req);
			r = -ECANCELED;
			times = 0;
			break;
		} else if (dev->state == STATE_OFFLINE) {
			r = -EIO;
			goto done;
		}

		/* if we have idle read requests, get them queued */
		while ((req = mtp_req_get(dev, &dev->rx_idle))) {
requeue_req:
			#if 0
			req->length = dev->maxsize?dev->maxsize:512;
			#endif
			req->length = MTP_BULK_BUFFER_SIZE;
			DBG(cdev, "%s: queue request(%p) on %s\n", __func__, req, dev->ep_out->name);
			ret = usb_ep_queue(dev->ep_out, req, GFP_ATOMIC);
			if (ret < 0) {
				INFO(cdev, "%s: failed to queue req %p (%d)\n", __func__, req, ret);
				r = -EIO;
				mtp_req_put(dev, &dev->rx_idle, req);
				goto done;
			}
		}

		DBG(cdev, "%s: read %llu bytes\n", __func__, dev->read_count);
		/* if we have data pending, give it to userspace */
		if (dev->read_count > 0) {
			xfer = (dev->read_count < count) ? dev->read_count : count;

			ret = vfs_write(filp, dev->read_buf, xfer,
				&offset);
			DBG(cdev, "vfs_write %d\n", ret);
			if (ret != xfer) {
				r = -EIO;
				INFO(cdev, "%s(%d) vfs_write error, ret:%d\n",__func__, __LINE__, ret);
				if (dev->state != STATE_OFFLINE)
					dev->state = STATE_ERROR;
				break;
			}
			dev->read_buf += xfer;
			dev->read_count -= xfer;

			if(unlikely(dev->state == STATE_OFFLINE)) {
				r = -EIO;
				goto done;
			}
			/* if xfer_file_length is 0xFFFFFFFF, then we read until
			 * we get a zero length packet
			 */
			if (file_xfer_zlp_flag == 0)
				count -= xfer;

			/* if we've emptied the buffer, release the request */
			if (dev->read_count == 0 && dev->rx_req) {
				mtp_req_put(dev, &dev->rx_idle, dev->rx_req);
				dev->rx_req = 0;
			}

			/* short packet found */
			if (xfer < MTP_BULK_BUFFER_SIZE) {
				break;
			}
			continue;
		}

		/* wait for a request to complete */
		req = 0;
		ret = wait_event_interruptible(dev->read_wq,
				((req = mtp_req_get(dev, &dev->rx_done))
				 || dev->state != STATE_BUSY));

		if (req != 0) {
			/* if we got a 0-len one we need to put it back into
			 ** service.  if we made it the current read req we'd
			 ** be stuck forever
				 */
			if (req->actual == 0) {
				if (file_xfer_zlp_flag == 0)
					goto requeue_req;

				dev->rx_req = req;
				mtp_req_put(dev, &dev->rx_idle, dev->rx_req);
				INFO(cdev, "%s: got ZLP while file xfer.\n", __func__);
				break;
			}

			dev->rx_req = req;
			dev->read_count = req->actual;
			dev->read_buf = req->buf;
		}

		if (ret < 0) {
			INFO(cdev, "%s: wait_event_interruptible ret = %d\n", __func__, ret);
			r = ret;
			break;
		}
	}

done:
	DBG(cdev, "receive_file_work returning %d\n", r);

/*-- 2014/11/20, USB Team, PCN00042 --*/
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	mod_timer(&dev->perf_timer, MTP_TRANSFER_EXPIRED);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/

	DBG(cdev, "receive_file_work returning %d\n", r);
	/* write the result */
	dev->xfer_result = r;
	smp_wmb();
}

static int mtp_send_event(struct mtp_dev *dev, struct mtp_event *event)
{
	struct usb_request *req = NULL;
	int ret;
	int length = event->length;

	DBG(dev->cdev, "mtp_send_event(%zu)\n", event->length);

	if (length < 0 || length > INTR_BUFFER_SIZE)
		return -EINVAL;
	if (dev->state == STATE_OFFLINE)
		return -ENODEV;

	ret = wait_event_interruptible_timeout(dev->intr_wq,
			(req = mtp_req_get(dev, &dev->intr_idle)),
			msecs_to_jiffies(1000));
	if (!req)
		return -ETIME;

	if (copy_from_user(req->buf, (void __user *)event->data, length)) {
		mtp_req_put(dev, &dev->intr_idle, req);
		return -EFAULT;
	}
	req->length = length;
	ret = usb_ep_queue(dev->ep_intr, req, GFP_KERNEL);
	if (ret)
		mtp_req_put(dev, &dev->intr_idle, req);

	return ret;
}

static long mtp_send_receive_ioctl(struct file *fp, unsigned code,
	struct mtp_file_range *mfr)
{
	struct mtp_dev *dev = fp->private_data;
	struct file *filp = NULL;
	struct work_struct *work;
	int ret = -EINVAL;

	if (mtp_lock(&dev->ioctl_excl))
		return -EBUSY;

	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED) {
		/* report cancelation to userspace */
		dev->state = STATE_READY;
		spin_unlock_irq(&dev->lock);
		ret = -ECANCELED;
		goto out;
	}
	if (dev->state == STATE_OFFLINE) {
		spin_unlock_irq(&dev->lock);
		ret = -ENODEV;
		goto out;
	}
	dev->state = STATE_BUSY;
	spin_unlock_irq(&dev->lock);

	/* hold a reference to the file while we are working with it */
	filp = fget(mfr->fd);
	if (!filp) {
		ret = -EBADF;
		goto fail;
	}

	/* write the parameters */
	dev->xfer_file = filp;
	dev->xfer_file_offset = mfr->offset;
	dev->xfer_file_length = mfr->length;
	smp_wmb();

	if (code == MTP_SEND_FILE_WITH_HEADER) {
		work = &dev->send_file_work;
		dev->xfer_send_header = 1;
		dev->xfer_command = mfr->command;
		dev->xfer_transaction_id = mfr->transaction_id;
	} else if (code == MTP_SEND_FILE) {
		work = &dev->send_file_work;
		dev->xfer_send_header = 0;
	} else {
		work = &dev->receive_file_work;
	}

	/* We do the file transfer on a work queue so it will run
	 * in kernel context, which is necessary for vfs_read and
	 * vfs_write to use our buffers in the kernel address space.
	 */
	queue_work(dev->wq, work);
	/* wait for operation to complete */
	flush_workqueue(dev->wq);
	fput(filp);

	/* read the result */
	smp_rmb();
	ret = dev->xfer_result;

fail:
	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_CANCELED)
		ret = -ECANCELED;
	else if (dev->state != STATE_OFFLINE)
		dev->state = STATE_READY;
	spin_unlock_irq(&dev->lock);
out:
	mtp_unlock(&dev->ioctl_excl);
	DBG(dev->cdev, "ioctl returning %d\n", ret);
	return ret;
}

static long mtp_ioctl(struct file *fp, unsigned code, unsigned long value)
{
	struct mtp_dev *dev = fp->private_data;
	struct mtp_file_range	mfr;
	struct mtp_event	event;
	int ret = -EINVAL;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	int thread_supported = MTP_THREAD_SUPPORT;
/*-- 2014/11/20, USB Team, PCN00042 --*/

	switch (code) {
	case MTP_SEND_FILE:
	case MTP_RECEIVE_FILE:
	case MTP_SEND_FILE_WITH_HEADER:

		if (copy_from_user(&mfr, (void __user *)value, sizeof(mfr))) {
			ret = -EFAULT;
			goto fail;
		}
		ret = mtp_send_receive_ioctl(fp, code, &mfr);
	break;
	case MTP_SEND_EVENT:
		if (mtp_lock(&dev->ioctl_excl))
			return -EBUSY;
		/* return here so we don't change dev->state below,
		 * which would interfere with bulk transfer state.
		 */
		if (copy_from_user(&event, (void __user *)value, sizeof(event)))
			ret = -EFAULT;
		else
			ret = mtp_send_event(dev, &event);
		mtp_unlock(&dev->ioctl_excl);
	break;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	case MTP_THREAD_SUPPORTED:
	{
		printk("[USB][MTP] %s: ioctl MTP_THREAD_SUPPORTED, val %d\n", __func__, (int)thread_supported);
		if (copy_to_user((void __user *)value, &thread_supported, sizeof(int))) {
			ret = -EFAULT;
			goto fail;
		} else
			ret = 0;
		break;
	}
/*-- 2014/11/20, USB Team, PCN00042 --*/
	default:
		DBG(dev->cdev, "unknown ioctl code: %d\n", code);
	}
fail:
	return ret;
}

/*
 * 32 bit userspace calling into 64 bit kernl. handle ioctl code
 * and userspace pointer
*/
#ifdef CONFIG_COMPAT
static long compat_mtp_ioctl(struct file *fp, unsigned code,
	unsigned long value)
{
	struct mtp_dev *dev = fp->private_data;
	struct mtp_file_range	mfr;
	struct __compat_mtp_file_range	cmfr;
	struct mtp_event	event;
	struct __compat_mtp_event cevent;
	unsigned cmd;
	bool send_file = false;
	int ret = -EINVAL;
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	int thread_supported = MTP_THREAD_SUPPORT;
/*-- 2014/11/20, USB Team, PCN00042 --*/

	switch (code) {
	case COMPAT_MTP_SEND_FILE:
		cmd = MTP_SEND_FILE;
		send_file = true;
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
		mtp_setup_perflock(true);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
		break;
	case COMPAT_MTP_RECEIVE_FILE:
		cmd = MTP_RECEIVE_FILE;
		send_file = true;
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
		mtp_setup_perflock(true);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
		break;
	case COMPAT_MTP_SEND_FILE_WITH_HEADER:
		cmd = MTP_SEND_FILE_WITH_HEADER;
		send_file = true;
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
		mtp_setup_perflock(true);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
		break;
	case COMPAT_MTP_SEND_EVENT:
		cmd = MTP_SEND_EVENT;
		break;
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	case COMPAT_MTP_SET_CPU_PERF:
	{
		if (value > 0) {
			printk(KERN_INFO "[USB][MTP] %s: ioctl MTP_SET_CPU_PERF, files = %d\n", __func__, (int)value);
			mtp_qos_enable((int)value);
		}
		break;
	}
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	case COMPAT_MTP_THREAD_SUPPORTED:
	{
		printk("[USB][MTP] %s: ioctl MTP_THREAD_SUPPORTED, val %d\n", __func__, (int)thread_supported);
		if (copy_to_user((void __user *)value, &thread_supported, sizeof(int))) {
			ret = -EFAULT;
			goto fail;
		} else
			ret = 0;
		break;
	}
/*-- 2014/11/20, USB Team, PCN00042 --*/
	default:
		DBG(dev->cdev, "unknown compat_ioctl code: %d\n", code);
		goto fail;
	}

	if (send_file) {
		if (copy_from_user(&cmfr, (void __user *)value, sizeof(cmfr))) {
			ret = -EFAULT;
			goto fail;
		}
		mfr.fd = cmfr.fd;
		mfr.offset = cmfr.offset;
		mfr.length = cmfr.length;
		mfr.command = cmfr.command;
		mfr.transaction_id = cmfr.transaction_id;
		ret = mtp_send_receive_ioctl(fp, cmd, &mfr);
	} else {
		if (mtp_lock(&dev->ioctl_excl))
			return -EBUSY;
		/* return here so we don't change dev->state below,
		 * which would interfere with bulk transfer state.
		 */
		if (copy_from_user(&cevent, (void __user *)value,
			sizeof(cevent))) {
			ret = -EFAULT;
			goto fail;
		}
		event.length = cevent.length;
		event.data = compat_ptr(cevent.data);
		ret = mtp_send_event(dev, &event);
		mtp_unlock(&dev->ioctl_excl);
	}
fail:
	return ret;
}
#endif

static int mtp_open(struct inode *ip, struct file *fp)
{
	printk(KERN_INFO "mtp_open\n");
/*++ 2014/10/21, USB Team, PCN00019 ++*/
	htc_mtp_open_state = 1;
/*-- 2014/10/21, USB Team, PCN00019 --*/
	if (mtp_lock(&_mtp_dev->open_excl))
		return -EBUSY;

	/* clear any error condition */
	if (_mtp_dev->state != STATE_OFFLINE)
		_mtp_dev->state = STATE_READY;

	fp->private_data = _mtp_dev;
	return 0;
}

static int mtp_release(struct inode *ip, struct file *fp)
{
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	struct usb_request *req = NULL;
	struct mtp_dev *dev = _mtp_dev;
	struct usb_composite_dev *cdev = dev->cdev;
/*-- 2014/11/20, USB Team, PCN00042 --*/
	printk(KERN_INFO "mtp_release\n");
/*++ 2014/10/21, USB Team, PCN00019 ++*/
	htc_mtp_open_state = 0;
/*-- 2014/10/21, USB Team, PCN00019 --*/
	mtp_unlock(&_mtp_dev->open_excl);
/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	mtp_setup_perflock(false);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	if (mtp_lock(&dev->read_excl)) {
		DBG(cdev, "%s: set state as STATE_CANCELED to flush read job\n", __func__);
		dev->state = STATE_CANCELED;
		return 0;
	}

	/* Ask all request on rx_done back to rx_idle */
	if (dev->state != STATE_OFFLINE) {
		while ((req = mtp_req_get(dev, &dev->rx_done))) {
			DBG(dev->cdev, "%s send %p from done to idle\n", __func__, req);
			mtp_req_put(dev, &dev->rx_idle, req);
		}
	}

	dev->read_count = 0;
	mtp_unlock(&dev->read_excl);
/*-- 2014/11/20, USB Team, PCN00042 --*/
	return 0;
}

/* file operations for /dev/mtp_usb */
static const struct file_operations mtp_fops = {
	.owner = THIS_MODULE,
	.read = mtp_read,
	.write = mtp_write,
	.unlocked_ioctl = mtp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_mtp_ioctl,
#endif
	.open = mtp_open,
	.release = mtp_release,
};

static struct miscdevice mtp_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = mtp_shortname,
	.fops = &mtp_fops,
};

static int mtp_ctrlrequest(struct usb_composite_dev *cdev,
				const struct usb_ctrlrequest *ctrl)
{
	struct mtp_dev *dev = _mtp_dev;
	int	value = -EOPNOTSUPP;
	u16	w_index = le16_to_cpu(ctrl->wIndex);
	u16	w_value = le16_to_cpu(ctrl->wValue);
	u16	w_length = le16_to_cpu(ctrl->wLength);
	unsigned long	flags;
/*++ 2015/01/15, USB Team, PCN00068 ++*/
	int	id;
/*-- 2015/01/15, USB Team, PCN00068 --*/

	VDBG(cdev, "mtp_ctrlrequest "
			"%02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);

	/* Handle MTP OS string */
	if (ctrl->bRequestType ==
			(USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE)
			&& ctrl->bRequest == USB_REQ_GET_DESCRIPTOR
			&& (w_value >> 8) == USB_DT_STRING
			&& (w_value & 0xFF) == MTP_OS_STRING_ID) {
		value = (w_length < sizeof(mtp_os_string)
				? w_length : sizeof(mtp_os_string));
		memcpy(cdev->req->buf, mtp_os_string, value);
	} else if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
		/* Handle MTP OS descriptor */
		DBG(cdev, "vendor request: %d index: %d value: %d length: %d\n",
			ctrl->bRequest, w_index, w_value, w_length);

		if (ctrl->bRequest == 1
				&& (ctrl->bRequestType & USB_DIR_IN)
				&& (w_index == 4 || w_index == 5)) {
			value = (w_length < sizeof(mtp_ext_config_desc) ?
					w_length : sizeof(mtp_ext_config_desc));
			memcpy(cdev->req->buf, &mtp_ext_config_desc, value);
		}
	} else if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
		DBG(cdev, "class request: %d index: %d value: %d length: %d\n",
			ctrl->bRequest, w_index, w_value, w_length);

/*++ 2015/01/15, USB Team, PCN00068 ++*/
		id = mtp_interface_desc.bInterfaceNumber;
		if (ctrl->bRequest == MTP_REQ_CANCEL
				&& (w_index == 0 || w_index == id)
				&& w_value == 0) {
/*-- 2015/01/15, USB Team, PCN00068 --*/
			DBG(cdev, "MTP_REQ_CANCEL\n");

			spin_lock_irqsave(&dev->lock, flags);
			if (dev->state == STATE_BUSY) {
				dev->state = STATE_CANCELED;
				wake_up(&dev->read_wq);
				wake_up(&dev->write_wq);
			}
			spin_unlock_irqrestore(&dev->lock, flags);

			/* We need to queue a request to read the remaining
			 *  bytes, but we don't actually need to look at
			 * the contents.
			 */
			value = w_length;
		} else if (ctrl->bRequest == MTP_REQ_GET_DEVICE_STATUS
				&& w_index == 0 && w_value == 0) {
			struct mtp_device_status *status = cdev->req->buf;
			status->wLength =
				__constant_cpu_to_le16(sizeof(*status));

			DBG(cdev, "MTP_REQ_GET_DEVICE_STATUS\n");
			spin_lock_irqsave(&dev->lock, flags);
			/* device status is "busy" until we report
			 * the cancelation to userspace
			 */
			if (dev->state == STATE_CANCELED)
				status->wCode =
					__cpu_to_le16(MTP_RESPONSE_DEVICE_BUSY);
			else
				status->wCode =
					__cpu_to_le16(MTP_RESPONSE_OK);
			spin_unlock_irqrestore(&dev->lock, flags);
			value = sizeof(*status);
		}
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		int rc;
		cdev->req->zero = value < w_length;
		cdev->req->length = value;
		rc = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		if (rc < 0)
			ERROR(cdev, "%s: response queue error\n", __func__);
	}
	return value;
}

static int
mtp_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct mtp_dev	*dev = func_to_mtp(f);
	int			id;
	int			ret;

	dev->cdev = cdev;
	DBG(cdev, "mtp_function_bind dev: %p\n", dev);

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	mtp_interface_desc.bInterfaceNumber = id;

	/* allocate endpoints */
	ret = mtp_create_bulk_endpoints(dev, &mtp_fullspeed_in_desc,
			&mtp_fullspeed_out_desc, &mtp_intr_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		mtp_highspeed_in_desc.bEndpointAddress =
			mtp_fullspeed_in_desc.bEndpointAddress;
		mtp_highspeed_out_desc.bEndpointAddress =
			mtp_fullspeed_out_desc.bEndpointAddress;
	}

	/* support super speed hardware */
	if (gadget_is_superspeed(c->cdev->gadget)) {
		mtp_superspeed_in_desc.bEndpointAddress =
			mtp_fullspeed_in_desc.bEndpointAddress;
		mtp_superspeed_out_desc.bEndpointAddress =
			mtp_fullspeed_out_desc.bEndpointAddress;
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, dev->ep_in->name, dev->ep_out->name);
	return 0;
}

static void
mtp_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct mtp_dev	*dev = func_to_mtp(f);
	struct usb_request *req;

	while ((req = mtp_req_get(dev, &dev->tx_idle)))
		mtp_request_free(req, dev->ep_in);

/*++ 2014/12/29, USB Team, PCN00062 ++*/
	/* Dequeue all request pending on ep_out */
	usb_ep_nuke(dev->ep_out);
/*-- 2014/12/29, USB Team, PCN00062 --*/

	while ((req = mtp_req_get(dev, &dev->rx_idle))) {
		DBG(dev->cdev, "%s: rx_idle release (%p)\n", __func__, req);
		mtp_request_free(req, dev->ep_out);
	}
	while ((req = mtp_req_get(dev, &dev->rx_done))) {
		DBG(dev->cdev, "%s: rx_done release (%p)\n", __func__, req);
		mtp_request_free(req, dev->ep_out);
	}
	while ((req = mtp_req_get(dev, &dev->intr_idle)))
		mtp_request_free(req, dev->ep_intr);
	dev->state = STATE_OFFLINE;
}

static int mtp_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct mtp_dev	*dev = func_to_mtp(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	DBG(cdev, "mtp_function_set_alt intf: %d alt: %d\n", intf, alt);

	ret = config_ep_by_speed(cdev->gadget, f, dev->ep_in);
	if (ret) {
		dev->ep_in->desc = NULL;
		ERROR(cdev, "config_ep_by_speed failes for ep %s, result %d\n",
			dev->ep_in->name, ret);
		return ret;
	}
	ret = usb_ep_enable(dev->ep_in);
	if (ret) {
		ERROR(cdev, "failed to enable ep %s, result %d\n",
			dev->ep_in->name, ret);
		return ret;
	}

	ret = config_ep_by_speed(cdev->gadget, f, dev->ep_out);
	if (ret) {
		dev->ep_out->desc = NULL;
		ERROR(cdev, "config_ep_by_speed failes for ep %s, result %d\n",
			dev->ep_out->name, ret);
		usb_ep_disable(dev->ep_in);
		return ret;
	}
	ret = usb_ep_enable(dev->ep_out);
	if (ret) {
		ERROR(cdev, "failed to enable ep %s, result %d\n",
			dev->ep_out->name, ret);
		usb_ep_disable(dev->ep_in);
		return ret;
	}
	dev->ep_intr->desc = &mtp_intr_desc;
	ret = usb_ep_enable(dev->ep_intr);
	if (ret) {
		usb_ep_disable(dev->ep_out);
		usb_ep_disable(dev->ep_in);
		return ret;
	}
	dev->state = STATE_READY;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	return 0;
}

static void mtp_function_disable(struct usb_function *f)
{
	struct mtp_dev	*dev = func_to_mtp(f);
	struct usb_composite_dev	*cdev = dev->cdev;

	DBG(cdev, "mtp_function_disable\n");
	dev->state = STATE_OFFLINE;
	usb_ep_disable(dev->ep_in);
	usb_ep_disable(dev->ep_out);
	usb_ep_disable(dev->ep_intr);

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);

	VDBG(cdev, "%s disabled\n", dev->function.name);
}

static int mtp_bind_config(struct usb_configuration *c, bool ptp_config)
{
	struct mtp_dev *dev = _mtp_dev;
	int ret = 0;

	printk(KERN_INFO "mtp_bind_config\n");

	/* allocate a string ID for our interface */
	if (mtp_string_defs[INTERFACE_STRING_INDEX].id == 0) {
		ret = usb_string_id(c->cdev);
		if (ret < 0)
			return ret;
		mtp_string_defs[INTERFACE_STRING_INDEX].id = ret;
		mtp_interface_desc.iInterface = ret;
/*++ 2014/12/12, USB Team, PCN00056 ++*/
		ptp_interface_desc.iInterface = ret;
/*-- 2014/12/12, USB Team, PCN00056 --*/
	}

	dev->cdev = c->cdev;
	dev->function.name = "mtp";
	dev->function.strings = mtp_strings;

/*++ 2015/10/12 USB Team, PCN00086 ++*/
	dev->function.fs_descriptors = fs_ptp_descs;
	dev->function.hs_descriptors = hs_ptp_descs;
	if (gadget_is_superspeed(c->cdev->gadget))
		dev->function.ss_descriptors = ss_ptp_descs;
/*-- 2015/10/12 USB Team, PCN00086 --*/

	dev->function.bind = mtp_function_bind;
	dev->function.unbind = mtp_function_unbind;
	dev->function.set_alt = mtp_function_set_alt;
	dev->function.disable = mtp_function_disable;

	return usb_add_function(c, &dev->function);
}

static int mtp_setup(void)
{
	struct mtp_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);
	init_waitqueue_head(&dev->intr_wq);
	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->ioctl_excl, 0);
/*++ 2014/11/20, USB Team, PCN00042 ++*/
	INIT_LIST_HEAD(&dev->rx_idle);
	INIT_LIST_HEAD(&dev->rx_done);
/*-- 2014/11/20, USB Team, PCN00042 --*/
	INIT_LIST_HEAD(&dev->tx_idle);
	INIT_LIST_HEAD(&dev->intr_idle);

	dev->wq = create_singlethread_workqueue("f_mtp");
	if (!dev->wq) {
		ret = -ENOMEM;
		goto err1;
	}
	INIT_WORK(&dev->send_file_work, send_file_work);
	INIT_WORK(&dev->receive_file_work, receive_file_work);

	_mtp_dev = dev;
/*++ 2014/10/21, USB Team, PCN00019 ++*/
	htc_mtp_open_state = 0;
/*-- 2014/10/21, USB Team, PCN00019 --*/

/*++ 2014/10/29, USB Team, PCN00031 ++*/
#ifdef CONFIG_PERFLOCK
	INIT_WORK(&dev->release_perflock_work, release_perflock_work_func);
	perf_lock_init(&dev->perf_lock, TYPE_PERF_LOCK, PERF_LOCK_HIGHEST, "htc_mtp");
	setup_timer(&dev->perf_timer, mtp_perf_lock_disable, (unsigned long)dev);
#endif
/*-- 2014/10/29, USB Team, PCN00031 --*/

	ret = misc_register(&mtp_device);
	if (ret)
		goto err2;

	return 0;

err2:
	destroy_workqueue(dev->wq);
err1:
	_mtp_dev = NULL;
	kfree(dev);
	printk(KERN_ERR "mtp gadget driver failed to initialize\n");
	return ret;
}

static void mtp_cleanup(void)
{
	struct mtp_dev *dev = _mtp_dev;

	if (!dev)
		return;

	misc_deregister(&mtp_device);
	destroy_workqueue(dev->wq);
	_mtp_dev = NULL;
	kfree(dev);
}
