#include <linux/module.h>
#include <rtdm/driver.h>
MODULE_LICENSE("GPL");

struct hello_rt_context {
	void* buf;
	int size;
};

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	ctx->buf = rtdm_malloc(4096);
	rtdm_printk("HI\n");
	printk(KERN_ALERT "UM\n");
	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	rtdm_free(ctx->buf);
	rtdm_printk("BYE\n");
}

static ssize_t hello_rt_write_nrt(struct rtdm_fd *fd, const void __user *buf, size_t size){
	int res;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	
	res = rtdm_copy_from_user(fd, ctx->buf, buf, size);
	ctx->size = size;

	rtdm_printk("write!\n");
	return size;
}

static ssize_t hello_rt_read_nrt(struct rtdm_fd *fd, void __user *buf, size_t size){
	int ret, read_count;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	if (ctx->size < size){
		read_count = ctx->size;
	} else {
		read_count = size;
	}
	ret = rtdm_copy_to_user(fd, buf, ctx->buf, read_count);
	ctx->size -= read_count;
	rtdm_printk("read! %i\n", ret);
	return read_count;
}

static struct rtdm_driver hello_rt_driver = {
	.profile_info		= RTDM_PROFILE_INFO(rtdm_test_hello_rt,
						    RTDM_CLASS_EXPERIMENTAL,
						    RTDM_SUBCLASS_GENERIC,
						    0),
	.device_flags		= RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
	.device_count		= 1,
	.context_size		= sizeof(struct hello_rt_context),
	.ops = {
		.open		= hello_rt_open,
		.close		= hello_rt_close,
		//.ioctl_rt	= rtdm_basic_ioctl_rt,
		//.ioctl_nrt	= rtdm_basic_ioctl_nrt,
		.read_nrt	= hello_rt_read_nrt,
		.write_nrt	= hello_rt_write_nrt,
		.read_rt	= hello_rt_read_nrt,
		.write_rt	= hello_rt_write_nrt,
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
