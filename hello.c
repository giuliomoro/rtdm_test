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
};

// global pointer to main device structure, inited in hello_init
struct hello_dev *hello_device;

static int hello_open(struct inode *inode, struct file *filp){
	printk(KERN_ALERT "hello_open!\n");
	return 0;
}

// function pointer struct
struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.open = hello_open,
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
	unregister_chrdev_region(devno, 1);
	printk(KERN_ALERT "Goodbye world\n");
}

module_init(hello_init);
module_exit(hello_exit);
