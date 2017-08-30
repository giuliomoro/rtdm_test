#include <linux/module.h>
#include <rtdm/driver.h>
MODULE_LICENSE("GPL");

#define CM_PER 0x44e00000
#define CM_PER_SIZE 0x3fff
#define CM_WKUP_ADC 0x4bc
#define ADC_BASE 0x44e0d000
#define ADC_SIZE 0x1fff
#define ADC_IRQSTATUS 0x28
#define ADC_IRQENABLE_SET 0x2c
#define ADC_IRQENABLE_CLR 0x30
#define ADC_CTRL 0x40
#define ADC_STATUS 0x44
#define ADC_CLKDIV 0x4c
#define ADC_STEPENABLE 0x54
#define ADC_STEPCONFIG1 0x64
#define ADC_STEPDELAY1 0x68
#define ADC_FIFO0_COUNT 0xe4
#define ADC_FIFO0_THRESHOLD 0xe8
#define ADC_FIFO0_DATA 0x100

struct hello_rt_context {
	int* buf;
	int size;
	void* gpio1_addr;
	void* adc_addr;
	rtdm_irq_t irq_n;
};

static int irq_handler(rtdm_irq_t *irq_handle){
	unsigned int irq_status;
	struct hello_rt_context *ctx = ((struct hello_rt_context*)irq_handle->cookie);
	int numSamples, i;

	numSamples = ioread32(ctx->adc_addr + ADC_FIFO0_COUNT);

	// empty fifo into buffer
	for (i=0; i<numSamples; i++){
		ctx->buf[i] = ioread32(ctx->adc_addr + ADC_FIFO0_DATA);
	}
	
	// clear interrupt bit in adc registers to prevent irq refiring
	irq_status = ioread32(ctx->adc_addr + ADC_IRQSTATUS);
	iowrite32(irq_status, ctx->adc_addr + ADC_IRQSTATUS);
	rtdm_printk("irq %i: %08x : %i : %i\n", *((int*)irq_handle), irq_status, numSamples, ctx->buf[2]);

	return RTDM_IRQ_HANDLED;
}

void init_adc(struct hello_rt_context *ctx){
	void* cm_per_addr;
	unsigned int adc_clk;

	// turn on the adc interface clock
	cm_per_addr = ioremap(CM_PER, CM_PER_SIZE);
	adc_clk = ioread32(cm_per_addr + CM_WKUP_ADC);
	printk(KERN_ALERT "adc_clk: %08x\n", adc_clk);
	if (adc_clk != 0x2){
		iowrite32(0x2, cm_per_addr + CM_WKUP_ADC);
		printk(KERN_ALERT "turned on adc ocp clock\n");
	}
	iounmap(cm_per_addr);

	// init adc
	ctx->adc_addr = ioremap(ADC_BASE, ADC_SIZE);
	printk(KERN_ALERT "adc rev: %08x\n", ioread32(ctx->adc_addr));
	iowrite32(0x4, ctx->adc_addr + ADC_CTRL);		// remove stepconfig write protection
	iowrite32(0xfff, ctx->adc_addr + ADC_CLKDIV); 		// set clockdivider to max
	iowrite32(0x2, ctx->adc_addr + ADC_STEPENABLE);
	iowrite32(0x1, ctx->adc_addr + ADC_STEPCONFIG1);	// set stepconfig to continuous
	iowrite32(0xff000000, ctx->adc_addr + ADC_STEPDELAY1);	// set sampledelay to 255
	iowrite32(0x4, ctx->adc_addr + ADC_IRQENABLE_SET);	// enable fifo0 threshold irq
	iowrite32(0x4, ctx->adc_addr + ADC_FIFO0_THRESHOLD);	// set fifo0 threshold to 4 samples

	printk(KERN_ALERT "adc config: %08x\n", ioread32(ctx->adc_addr + ADC_STEPCONFIG1));
	printk(KERN_ALERT "adc enable: %08x\n", ioread32(ctx->adc_addr + ADC_STEPENABLE));

	iowrite32(0x1, ctx->adc_addr + ADC_CTRL);

}

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
	unsigned int irq = irq_create_mapping(intc_domain, 16);
	int res = rtdm_irq_request(&ctx->irq_n, irq, irq_handler, 0, "hello_rt_irq", (void*)ctx);
	printk("rtdm interrupt %i registered: %i\n", irq, res);
}

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);

	init_intc(ctx);
	// init_gpio(ctx);
	init_adc(ctx);

	ctx->buf = (int*)rtdm_malloc(4096);

	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	rtdm_free(ctx->buf);
	rtdm_irq_free(&ctx->irq_n);
	rtdm_printk("BYE\n");
}

static ssize_t hello_rt_write_nrt(struct rtdm_fd *fd, const void __user *buf, size_t size){
	int res;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	
	// res = rtdm_copy_from_user(fd, ctx->buf, buf, size);
	// ctx->size = size;

	rtdm_printk("write!\n");
	return size;
}

static ssize_t hello_rt_read_nrt(struct rtdm_fd *fd, void __user *buf, size_t size){
	int ret, read_count;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	// ret = rtdm_copy_to_user(fd, buf, ctx->buf, read_count);
	int status = ioread32(ctx->adc_addr + ADC_STATUS);
	int count = ioread32(ctx->adc_addr + ADC_FIFO0_COUNT);
	int config = ioread32(ctx->adc_addr + ADC_STEPCONFIG1);
	int delay = ioread32(ctx->adc_addr + ADC_STEPDELAY1);

	rtdm_printk("status: %08x\ncount: %i\nconfig: %08x\ndelay: %08x\n", status, count, config, delay);

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
