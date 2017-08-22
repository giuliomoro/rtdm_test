#include <linux/module.h>
#include <rtdm/driver.h>
MODULE_LICENSE("GPL");

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	printk("HI\n");
	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	printk("BYE\n");
}

static struct rtdm_driver hello_rt_driver = {
	.profile_info		= RTDM_PROFILE_INFO(rtdm_test_basic,
						    RTDM_CLASS_EXPERIMENTAL,
						    RTDM_SUBCLASS_GENERIC,
						    0),
	.device_flags		= RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
	.device_count		= 1,
	.context_size		= 0,
	.ops = {
		.open		= hello_rt_open,
		.close		= hello_rt_close,
		//.ioctl_rt	= rtdm_basic_ioctl_rt,
		//.ioctl_nrt	= rtdm_basic_ioctl_nrt,
	},
};


static struct rtdm_device device = {
	.driver = &hello_rt_driver,
	.label = "rtdm_hello_%d",
};

static int __init hello_rt_init(void){
	return rtdm_dev_register(&device);
}

static void __exit hello_rt_exit(void){
	return rtdm_dev_unregister(&device);
}

module_init(hello_rt_init);
module_exit(hello_rt_exit);
