#include "./usb_driver_header.h"

//////////////////////////////////////////////////////////////////////////
//
//  Initializing kernel Structures
//
//////////////////////////////////////////////////////////////////////////

//  file_operations structure initializing
static const struct file_operations fops = 
{
    .owner = THIS_MODULE,
    .open = usb_open,
    .write = usb_write,
    .read = usb_read,
    .llseek  = default_llseek,
};

//  class structure initializing
static struct usb_class_driver class = 
{
    .name = "usb_driver%d",
    .fops = &fops,
    .minor_base = 0,
};

//  usb_device_id table initializing
static struct usb_device_id table[] = {
    {USB_DEVICE(0, 0)},     // by default accept all usb devices
    {}
};
MODULE_DEVICE_TABLE(usb, table);

//  usb_driver struct initializing
static struct usb_driver skel_usb_driver = {
    .name = "usb_driver",
    .probe = usb_probe,
    .disconnect = usb_disconnect,
    .id_table = table,
};

//////////////////////////////////////////////////////////////////////////
//
//  Helper functions defination
//
//////////////////////////////////////////////////////////////////////////

// used to update sector 0 after every usb_write() to maintain metadata
static int write_sector_zero(struct usb_dev *dev)
{

    struct bulk_cbw *cbw = NULL;
    struct bulk_csw *csw = NULL;
    u8 *buffer = NULL;
    int len = 0, ret = 0;

    // dynamically allocate memory for the 3 phases 
    // kzalloc is similar to calloc where heap memory is zeroed out
    // GFP_KERNEL mandatorily allocate heap memory
    cbw = kzalloc(sizeof(struct bulk_cbw), GFP_KERNEL);
    if (cbw == NULL)
    {
        return -ENOMEM;
    }

    csw = kzalloc(sizeof(struct bulk_csw), GFP_KERNEL);
    if (csw == NULL)
    {
        kfree(cbw);
        return -ENOMEM;
    }

    buffer = kzalloc(512, GFP_KERNEL);
    if (buffer == NULL)
    {
        kfree(cbw);
        kfree(csw);
        return -ENOMEM;
    }

    // Initialize the first 4 bytes of buffer to copy them in to sector 0 (Big-Endian)
    buffer[0] = (dev->next_sector >> 24) & 0xFF;
    buffer[1] = (dev->next_sector >> 16) & 0xFF;
    buffer[2] = (dev->next_sector >> 8) & 0xFF;
    buffer[3] = dev->next_sector & 0xFF;
    // Bytes 4-511 will remain 0x00 due to kzalloc

    // Construct the CBW for WRITE(10) at Sector 0
    cbw->Signature = cpu_to_le32(0x43425355);
    cbw->Tag = cpu_to_le32(0xEE);      
    cbw->DataTransferLength = cpu_to_le32(512);
    cbw->Flags = 0x00;    // Direction of data phase:  Out (Host to Device)
    cbw->Length = 10;
    
    cbw->CDB[0] = 0x2A;   // WRITE(10) command
    cbw->CDB[2] = 0x00;   // LBA (Sector 0)
    cbw->CDB[3] = 0x00;
    cbw->CDB[4] = 0x00;
    cbw->CDB[5] = 0x00;               
    cbw->CDB[8] = 0x01;   // Number of blocks: 1

    // Execute the 3-Phase BOT Sequence (CBW, Data, CSW)

    // Phase 1: Command Block Wrapper (host -> device)
    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), 
                 cbw, 31, &len, 5000);

    // Phase 2: Data (host -> device)
    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), 
                 buffer, 512, &len, 5000);

    // Phase 3: Command Status Wrapper (device -> host)
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), 
                 csw, 13, &len, 5000);

    // Verify Hardware Status
    if (csw->Status != 0) {
        printk(KERN_ERR "usb_driver: Hardware failed to update Sector 0 metadata\n");
        ret = -EIO;
    }

    // Cleanup the kernel heap memory
    kfree(cbw);
    kfree(csw);
    kfree(buffer);
    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// used to read sector 0 inside usb_probe() to initiliaze usb_device->next_sector
static int read_sector_zero(struct usb_dev *dev, __u8 *data)
{
    struct bulk_cbw *cbw = NULL;
    struct bulk_csw *csw = NULL;
    int len = 0, ret = 0;

    // dynamically allocate memory for cbw and csw
    // kzalloc is similar to calloc where heap memory is zeroed out
    // GFP_KERNEL mandatorily allocate heap memory
    cbw = kzalloc(sizeof(struct bulk_cbw), GFP_KERNEL);
    if (cbw == NULL) 
    {
        return -ENOMEM;
    }
    csw = kzalloc(sizeof(struct bulk_csw), GFP_KERNEL);
    if (csw == NULL) 
    {
        kfree(cbw); 
        return -ENOMEM;
    }

    // Construct the CBW for READ(10) at Sector 0
    cbw->Signature = cpu_to_le32(0x43425355);
    cbw->Tag = cpu_to_le32(1);
    cbw->DataTransferLength = cpu_to_le32(512);
    cbw->Flags = 0x80; //// Direction of data phase: IN (Device to Host)
    cbw->Length = 10;
    cbw->CDB[0] = 0x28; // READ(10)
    cbw->CDB[8] = 0x01; // 1 Block

    // Execute 3-Phase BOT directly into buffer
    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), cbw, 31, &len, 5000);
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), data, 512, &len, 5000);
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), csw, 13, &len, 5000);

    if (csw->Status != 0) 
    {
        ret = -EIO;
    }

    kfree(cbw);
    kfree(csw);
    return ret;
}

//////////////////////////////////////////////////////////////////////////
//
//  Kernel Functions defination
//
//////////////////////////////////////////////////////////////////////////

//  Write Function defination
// used to write in the flash drive by the user using write() syscall(user space)
static ssize_t usb_write(struct file *f, const char __user *buf, size_t cnt, loff_t *pos)
{
    struct usb_dev *dev = NULL;
    struct bulk_cbw *cbw = NULL;
    struct bulk_csw *csw = NULL;
    int len = 0, ret = 0, data_len = 0, dynamic_write = 0;
    __u8 *data;
    __u32 lba;  // to hold the next_sector(append)

    // used to get usb_device, initialized inside usb_open()
    // private_data is a void *
    dev = f->private_data;

    // if user is writing without lseek then auto append
    if((*pos) == 0)
    {
        lba = dev->next_sector;
        (*pos) = dev->next_sector;
    }
    // if user want to write to any specific sector
    else
    {
        lba = (__u32)(*pos);
        dynamic_write = 1;
    }

    printk(KERN_INFO "usb_driver: writing to sector : %lld",(*pos));

    // dynamically allocate memory for 3 phase bot
    // kzalloc is similar to calloc where heap memory is zeroed out
    // GFP_KERNEL mandatorily allocate heap memory

    cbw = kzalloc(sizeof(struct bulk_cbw), GFP_KERNEL);
    if (cbw == NULL)
    {
        return -ENOMEM;
    }

    csw = kzalloc(sizeof(struct bulk_csw), GFP_KERNEL);
    if (csw == NULL)
    {
        kfree(cbw);
        return -ENOMEM;
    }

    data = kzalloc(512, GFP_KERNEL);
    if (data == NULL)
    {
        kfree(cbw);
        kfree(csw);
        return -ENOMEM;
    }

    ret = copy_from_user(data, buf, min((size_t)512, cnt));
    if (ret != 0)
    {
        kfree(cbw);
        kfree(csw);
        kfree(data);
        return -EFAULT;
    }

    // Construct the CBW for WRITE(10) 
    cbw->Signature = cpu_to_le32(0x43425355);
    cbw->Tag = cpu_to_le32(1);
    cbw->DataTransferLength = cpu_to_le32(512);
    cbw->Flags = 0x00; // Direction of data phase: OUT (Host -> Device)
    cbw->Length = 10;
    cbw->CDB[0] = 0x2A; // WRITE(10)
    cbw->CDB[2] = (lba >> 24) & 0xFF; // Big-Endian LBA
    cbw->CDB[3] = (lba >> 16) & 0xFF;
    cbw->CDB[4] = (lba >> 8) & 0xFF;
    cbw->CDB[5] = lba & 0xFF;
    cbw->CDB[8] = 0x01; // 1 block

    // Execute 3-Phase BOT 
    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), cbw, 31, &len, 5000);
    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), data, 512, &data_len, 5000);
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), csw, 13, &len, 5000);

    if (csw->Status != 0)
    {

        kfree(cbw);
        kfree(csw);
        kfree(data);
        return -EIO;
    }

    printk(KERN_INFO "usb_driver: Write status %d, sent %d bytes\n", csw->Status, data_len);


    *pos += 1; 
    if (*pos > dev->next_sector) // to maintain global sector no in sector 0
    {
        dev->next_sector = *pos;
        write_sector_zero(dev);
    }
    // to allow dynamic sector write only for one sector making pos back to 0 (for auto appending at end)
    else if(dynamic_write == 1) 
    {
        (*pos) = 0;
    }
    ret = 512;

    printk(KERN_INFO "usb_driver: updated pos after write : %lld",(*pos));

    kfree(cbw);
    kfree(csw);
    kfree(data);

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

//  Read Function defination
// used to read from the flash drive by the user using read() syscall(user space)
static ssize_t usb_read(struct file *f, char __user *buf, size_t cnt, loff_t *pos)
{
    struct usb_dev *dev = NULL;
    struct bulk_cbw *cbw = NULL;
    struct bulk_csw *csw = NULL;
    __u8 *data;
    int len = 0, ret = 0, data_len = 0;
    __u32 lba = (__u32)(*pos);

    // used to get usb_device, initialized inside usb_open()
    // private_data is a void *
    dev = f->private_data;

    // dynamically allocate memory for 3 phase bot
    // kzalloc is similar to calloc where heap memory is zeroed out
    // GFP_KERNEL mandatorily allocate heap memory

    cbw = kzalloc(sizeof(struct bulk_cbw), GFP_KERNEL);
    if (cbw == NULL)
    {
        return -ENOMEM;
    }

    csw = kzalloc(sizeof(struct bulk_csw), GFP_KERNEL);
    if (csw == NULL)
    {
        kfree(cbw);
        return -ENOMEM;
    }

    data = kzalloc(512, GFP_KERNEL);
    if (data == NULL)
    {
        kfree(cbw);
        kfree(csw);
        return -ENOMEM;
    }

    // Construct the CBW for READ(10) 
    cbw->Signature = cpu_to_le32(0x43425355);
    cbw->Tag = cpu_to_le32(1);
    cbw->DataTransferLength = cpu_to_le32(512);
    cbw->Flags = 0x80; // Direction of data phase: IN (device -> host)
    cbw->Length = 10;
    cbw->CDB[0] = 0x28; // READ(10)
    cbw->CDB[2] = (lba >> 24) & 0xFF;
    cbw->CDB[3] = (lba >> 16) & 0xFF;
    cbw->CDB[4] = (lba >> 8) & 0xFF;
    cbw->CDB[5] =  lba & 0xFF;
    cbw->CDB[8] = 0x01; // 1 block

    usb_bulk_msg(dev->device, usb_sndbulkpipe(dev->device, dev->out_ep), cbw, 31, &len, 5000);
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), data, 512, &data_len, 5000);
    usb_bulk_msg(dev->device, usb_rcvbulkpipe(dev->device, dev->in_ep), csw, 13, &len, 5000);

    ret = copy_to_user(buf, data, min((size_t)512, cnt));
    
    if(ret != 0)
    {
        kfree(cbw);
        kfree(csw);
        kfree(data);
        return -EFAULT;
    }

    if(csw->Status != 0)
    {

        kfree(cbw);
        kfree(csw);
        kfree(data);
        return -EIO;
    }

    kfree(cbw);
    kfree(csw);
    kfree(data);

    *pos = 0; // allow only 1 sector to read at a time , do lseek for more sectors read every time
    return 512;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

//  Open Function defination
// used to store user struct usb_dev inside file->private_data
static int usb_open(struct inode *i, struct file *f)
{
    struct usb_interface *interf = NULL;
    unsigned int major = 0;
    unsigned int minor = 0;

    major = imajor(i);
    minor = iminor(i);

    printk(KERN_INFO "usb_driver: Opening device with Major no: %u, Minor no: %u\n", major, minor);

    // get the interface, usbcore stores a list of interfaces attached to usb_driver(struct) when probe is called and they are mapped using minor no
    interf = usb_find_interface(&skel_usb_driver, iminor(i));
    if (interf == NULL)
    {
        return -ENODEV;
    }

    // get the usb_dev *dev(user struct) stored inside interface during probe() using usb_set_intfdata()
    f->private_data = usb_get_intfdata(interf);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// probe function defination
// this function is called when usb of matched vendor_id and product_id is inserted
// here we initialise our user usb_dev struct
// Note : we are storing usb_dev struct inside interface because a single driver can be
// connected to multiple interfaces or flash drives with differnt minor number
// so to isolate every flash drive's usb_dev we store inside the interface of that flash drives' interface

static int usb_probe(struct usb_interface *interf, const struct usb_device_id *id)
{
    struct usb_dev *dev = NULL;
    struct usb_endpoint_descriptor *in = NULL, *out = NULL;
    __u8 *buffer = NULL;
    int ret = 0;

    // allocating memory for user struct usb_dev 
    dev = kzalloc(sizeof(struct usb_dev), GFP_KERNEL);
    if (dev == NULL)
    {
        return -ENOMEM;
    }

    // get the usb_device (device descriptor structure) from interface
    dev->device = usb_get_dev(interface_to_usbdev(interf));
    dev->interf = interf;

    ret = usb_find_common_endpoints(interf->cur_altsetting, &in, &out, NULL, NULL);
    if( ret != 0)
    {
        usb_put_dev(dev->device);
        kfree(dev);
        printk(KERN_ERR "usb_driver: bulk endpoints are not found\n");
        return ret;
    }

    dev->in_ep = in->bEndpointAddress;
    dev->out_ep = out->bEndpointAddress;

    // read the sector 0 to initialize dev->next_sector
    buffer = kzalloc(512, GFP_KERNEL);
    if(buffer == NULL)
    {
        usb_put_dev(dev->device);
        kfree(dev);
        printk(KERN_ERR "Failed to allocate Sector 0 buffer\n");    
        return -ENOMEM;
    }

    ret = read_sector_zero(dev, buffer);
    if(ret != 0)
    {
        usb_put_dev(dev->device);
        kfree(dev);
        kfree(buffer);
        printk(KERN_ERR "Failed to read Sector 0\n");    
        return ret;
    }

    // append all 4 bytes together in big endian format but it is flipped into little endian when stored in dev->next_sector in RAM as cpu understands little endian
    // note compiler promotes 1byte as 4 bytes that's why we are shifting it to let 
    // ex 0x02 as => 0x00000002
    dev->next_sector = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
    if(dev->next_sector == 0)
    {
        dev->next_sector = 1;
    }
    printk(KERN_INFO "usb_driver: Resuming from sector %u\n", dev->next_sector);

    // storing the user usb_dev inside interface
    usb_set_intfdata(interf, dev);

    // this function creates a device file inside /dev for user space interaction ex => /dev/usb_driver0
    usb_register_dev(interf, &class);

    printk(KERN_INFO "usb_driver: flash drive connected and registered\n");
    printk(KERN_INFO "usb_driver: helper struct usb_dev initialised inside probe\n");

    kfree(buffer);
    return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

// disconnect function defination
// this function is called when flash drive is removed
static void usb_disconnect(struct usb_interface *interf)
{
    struct usb_dev *dev = usb_get_intfdata(interf);
    usb_deregister_dev(interf, &class);
    usb_set_intfdata(interf, NULL);
    usb_put_dev(dev->device);
    kfree(dev);
    printk(KERN_INFO "usb_driver: flash drive successfully removed\n");
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

// init function defination
// this function is called when module is inserted (insmod)
static int __init usb_init(void)
{
    int ret = 0;
    table[0].idVendor = vendor_id;
    table[0].idProduct = product_id;
    table[0].match_flags = USB_DEVICE_ID_MATCH_DEVICE;

    ret = usb_register(&skel_usb_driver);
    if (ret != 0)
        printk(KERN_ERR "usb_driver: Registration failed\n");
    else
        printk(KERN_INFO "usb_driver: Driver loaded with vendorid:productid = %04x:%04x\n", vendor_id, product_id);
    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// exit function defination
// this function is called when module is removed (rmmod)
static void __exit usb_exit(void)
{
    usb_deregister(&skel_usb_driver);
    printk(KERN_INFO "usb_driver: Driver unloaded\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

module_init(usb_init);
module_exit(usb_exit);

