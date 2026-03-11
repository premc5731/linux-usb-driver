//////////////////////////////////////////////////////////////////////////
//
//  Including Header files
//
//////////////////////////////////////////////////////////////////////////

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/types.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prem Choudhary");
MODULE_DESCRIPTION("USB Flash Drive Device Driver");

//////////////////////////////////////////////////////////////////////////
//
//  Initializing vendor_id and product_id from user as module parameters
//
//////////////////////////////////////////////////////////////////////////

// default ids for my flash drive
static __u16 vendor_id = 0x0951;
static __u16 product_id = 0x1642;
// static __u16 vendor_id;
// static __u16 product_id;

module_param(vendor_id, ushort, 0644);
MODULE_PARM_DESC(vendor_id, "USB Vendor ID (Hex)");

module_param(product_id, ushort, 0644);
MODULE_PARM_DESC(product_id, "USB product ID (Hex)");

//////////////////////////////////////////////////////////////////////////
//
//  User Structure Decelarations
//
//////////////////////////////////////////////////////////////////////////

// custom command block wrapper
struct bulk_cbw
{
    __le32 Signature;
    __le32 Tag;
    __le32 DataTransferLength;
    __u8 Flags;
    __u8 Lun;
    __u8 Length;
    __u8 CDB[16];
}__packed;

// custom command status wrapper
struct bulk_csw
{
    __le32 Signature;
    __le32 Tag;
    __le32 DataResidue;
    __u8 Status;
}__packed;

// user structure used to maintain data across functions
struct usb_dev
{
    struct usb_device *device;
    struct usb_interface *interf;
    __u8 in_ep;
    __u8 out_ep;
    __u32 next_sector;
}__packed;

//////////////////////////////////////////////////////////////////////////
//
//  Functions deceleration
//
//////////////////////////////////////////////////////////////////////////

static int __init usb_init(void);
static void __exit usb_exit(void);

static int usb_probe(struct usb_interface *interf, const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *interf);

static int usb_open(struct inode *i, struct file *f);
static ssize_t usb_read(struct file *f, char __user *buf, size_t cnt, loff_t *pos);
static ssize_t usb_write(struct file *f, const char __user *buf, size_t cnt, loff_t *pos);

static int read_sector_zero(struct usb_dev *dev, __u8 *data);
static int write_sector_zero(struct usb_dev *dev);
