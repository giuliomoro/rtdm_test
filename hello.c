#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
MODULE_LICENSE("Dual BSD/GPL");

int major= 0;
int minor = 0;

// main device structure
struct hello_dev {
	struct cdev cdev;
	void *buf;
	size_t size;
	bool allocated;
};

// global pointer to main device structure, inited in hello_init
struct hello_dev *hello_device;

// function called when character device is opened
static int hello_open(struct inode *inode, struct file *filp){
	// inode is a struct representing the kernels internal view of the device file
	// filp is a struct representing the currently open(ing) file descriptor
	
	// inode contains a pointer i_cdev to the character device struct initialised in hello_init
	// here we use container_of to get a reference to the global device struct from the i_cdev pointer
	// in this case, this is trivial and the same as: dev = hello_device; but is neccessary if there are multiple devices
	struct hello_dev *dev;
	dev = container_of(inode->i_cdev, struct hello_dev, cdev);
	
	// the reference to the global struct is then stored in the file descriptor struct for easy access later
	filp->private_data = dev;

	printk(KERN_ALERT "hello_open!\n");
	return 0;
}

// function called when the device is closed
static int hello_release(struct inode *inode, struct file *filp){

	printk(KERN_ALERT "hello_release!\n");
	return 0;
}

ssize_t hello_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
	// retrieve the global dev struct from the function descriptor
	struct hello_dev *dev;
	dev = filp->private_data;

	// save the user buffer into the device's persistant buffer
	printk(KERN_ALERT "saving %d bytes\n", count);
	copy_from_user(dev->buf, buf, count);
	dev->size = count;

	return count;
}

ssize_t hello_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
	size_t read_count;
	// retrieve the global dev struct from the function descriptor
	struct hello_dev *dev;
	dev = filp->private_data;

	printk(KERN_ALERT "attempting to read %i bytes, size: %i\n", count, dev->size);

	if (count > dev->size){
		printk(KERN_ALERT "truncating read to %i bytes\n", dev->size);
		read_count = dev->size;
	} else {
		read_count = count;
	}

	// copy the contents of the persistant buffer into the user buffer
	copy_to_user(buf, dev->buf, read_count);
	dev->size -= read_count;

	return read_count;
}

// function pointer struct
struct file_operations hello_fops = {
	.owner		= THIS_MODULE,
	.open		= hello_open,
	.release	= hello_release,
	.read		= hello_read,
	.write		= hello_write,
};

static int hello_init(void){
	// init a device number variable
	dev_t devno = 0;
	int res;

	printk(KERN_ALERT "Hello world\n");

	// allocate a major number for our device, store the number in devno
	res = alloc_chrdev_region(&devno, 0, 1, "hello");
	if (res < 0){
		printk(KERN_WARNING "can't allocate major number\n");
		return res;
	}
	major = MAJOR(devno);
	printk(KERN_ALERT "Major number: %d\n", major);

	// allocate memory for the main dev struct
	hello_device = kmalloc(sizeof(struct hello_dev), GFP_KERNEL);
	memset(hello_device, 0, sizeof(struct hello_dev));

	// and the main persistant buffer
	hello_device->buf = kmalloc(4096, GFP_KERNEL);
	memset(hello_device->buf, 0, 4096);

	// initialise the underlying character device, storing its address in the global dev struct
	cdev_init(&hello_device->cdev, &hello_fops);
	hello_device->cdev.owner = THIS_MODULE;

	// add the device to the kernel
	cdev_add(&hello_device->cdev, devno, 1);

	return 0; 
}

static void hello_exit(void){
	dev_t devno = MKDEV(major, minor);
	cdev_del(&hello_device->cdev);
	kfree(hello_device->buf);
	kfree(hello_device);
	unregister_chrdev_region(devno, 1);
	printk(KERN_ALERT "Goodbye world\n");
}

module_init(hello_init);
module_exit(hello_exit);
