#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
MODULE_LICENSE("Dual BSD/GPL");

int major= 0;
int minor = 0;

static int hello_init(void){
	dev_t dev = 0;
	int res;

	printk(KERN_ALERT "Hello, world\n");

	res = alloc_chrdev_region(&dev, 0, 1, "hello");
	if (res < 0){
		printk(KERN_WARNING "can't allocate major number\n");
		return res;
	}
	major = MAJOR(dev);
	printk(KERN_ALERT "Major number: %d\n", major);
	return 0; 
}

static void hello_exit(void){
	dev_t devno = MKDEV(major, minor);
	unregister_chrdev_region(devno, 1);
	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_exit);
