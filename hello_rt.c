#include <linux/module.h>
#include <rtdm/driver.h>
MODULE_LICENSE("GPL");

#define CM_PER 0x44e00000
#define CM_PER_SIZE 0x3fff

#define NUM_SAMPLES 8

struct hello_rt_context {
	int* buf;
	int size;
	void* gpio1_addr;
	rtdm_irq_t irq_n;
	unsigned int linux_irq;
	rtdm_event_t event;
	nanosecs_abs_t irq_start;
	nanosecs_abs_t irq_stop;
};

#if 0
static int irq_handler(rtdm_irq_t *irq_handle){
	unsigned int irq_status;
	struct hello_rt_context *ctx = ((struct hello_rt_context*)irq_handle->cookie);
	int numSamples, i;

	ctx->irq_start = rtdm_clock_read();

	rtdm_printk(KERN_WARNING "event pulse\n");
	rtdm_event_pulse(&ctx->event);

	ctx->irq_stop = rtdm_clock_read();

	return RTDM_IRQ_HANDLED;
}
#else
static int irq_handler(rtdm_irq_t *irq_handle){
	unsigned int irq_status;
	struct hello_rt_context *ctx;

 	ctx = ((struct hello_rt_context*)irq_handle->cookie);
	rtdm_event_pulse(&ctx->event);
	//rtdm_printk("irq %i: %08x\n", *((int*)irq_handle), irq_status);

	// clear interrupt bit in gpio registers to prevent irq refiring
	irq_status = ioread32(ctx->gpio1_addr + 0x2c);
	iowrite32(irq_status, ctx->gpio1_addr + 0x2c);

	return RTDM_IRQ_HANDLED;
}
#endif

void init_gpio(struct hello_rt_context *ctx){
	void* cm_per_addr;
	unsigned int gpio1_clk;

	// turn on the gpio1 interface clock
	cm_per_addr = ioremap(0x44e00000, 0x3FFF);
	gpio1_clk = ioread32(cm_per_addr + 0xac);
	printk(KERN_ALERT "gpio1_clk: %08x\n", gpio1_clk);
	if (gpio1_clk != 0x2){
		iowrite32(0x2, cm_per_addr + 0xac);
		printk(KERN_ALERT "turned on gpio1 ocp clock\n");
	}
	iounmap(cm_per_addr);

	// activate falling-edge interrupts on gpio1_28
	ctx->gpio1_addr = ioremap(0x4804c000, 0xfff);
	printk(KERN_ALERT "gpio1 rev: %08x\n", ioread32(ctx->gpio1_addr));
	iowrite32(0x10000000, ctx->gpio1_addr+0x34);
	iowrite32(0x10000000, ctx->gpio1_addr+0x14c);
}


void init_intc(struct hello_rt_context *ctx){
	struct device_node *of_node = of_find_node_by_name(NULL, "interrupt-controller");
	struct irq_domain *intc_domain = irq_find_matching_fwnode(&of_node->fwnode, DOMAIN_BUS_ANY); 
	unsigned int irq = irq_create_mapping(intc_domain, 98);
	int res = rtdm_irq_request(&ctx->irq_n, irq, irq_handler, 0, "hello_rt_irq", (void*)ctx);
	printk("rtdm interrupt registered: %i\n", res);
}

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);

	init_intc(ctx);
	init_gpio(ctx);

	rtdm_event_init(&ctx->event, 0);

	ctx->buf = (int*)rtdm_malloc(4096);

	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	rtdm_irq_free(&ctx->irq_n);
	irq_dispose_mapping(ctx->linux_irq);
	rtdm_free(ctx->buf);
	rtdm_event_pulse(&ctx->event);
	rtdm_event_destroy(&ctx->event);
	rtdm_printk("BYE\n");
}

static ssize_t hello_rt_write(struct rtdm_fd *fd, const void __user *buf, size_t size){
//	int res;
//	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	
	// res = rtdm_copy_from_user(fd, ctx->buf, buf, size);
	// ctx->size = size;

	rtdm_printk(KERN_WARNING "write! Nothing happening\n");
	return size;
}

static ssize_t hello_rt_read_nrt(struct rtdm_fd *fd, void __user *buf, size_t size){
	printk(KERN_ALERT "Trying to read non-realtime. \n");
	return 0;
}
static ssize_t hello_rt_read(struct rtdm_fd *fd, void __user *buf, size_t size){
	int ret, read_count;
	int value = 0;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);

	//rtdm_printk(KERN_WARNING "hello_rt_read got called and waits\n");
	rtdm_event_wait(&ctx->event);
	//rtdm_printk(KERN_WARNING "hello_rt_read got released\n");
	//ret = rtdm_copy_to_user(fd, buf, &value, sizeof(value));

	return 0;
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
		.write_nrt	= hello_rt_write,
		.read_rt	= hello_rt_read,
		.write_rt	= hello_rt_write,
	},
};


static struct rtdm_device device = {
	.driver = &hello_rt_driver,
	.label = "rtdm_hello_%d",
};

static int __init hello_rt_init(void){
	printk(KERN_ALERT "hello_rt loaded\n");
	return rtdm_dev_register(&device);
}

static void __exit hello_rt_exit(void){
	printk(KERN_ALERT "hello_rt unloaded\n");
	return rtdm_dev_unregister(&device);
}

module_init(hello_rt_init);
module_exit(hello_rt_exit);
