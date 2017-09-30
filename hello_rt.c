#include <linux/module.h>
#include <rtdm/driver.h>
MODULE_LICENSE("GPL");

#define CM_PER 0x44e00000
#define CM_PER_SIZE 0x3fff

#define PRU_IRQ

#ifdef GPIO_IRQ
#define irq_number 98
#endif
#ifdef PRU_IRQ
// ARM interrupt number for PRU event EVTOUT2
// this is Host 4 in the PRU intc, which maps to EVTOUT2
//#define irq_number 22
#define irq_number 22

#define PRU_SYSTEM_EVENT 20
#define PRU_INTC_CHANNEL 4
#define PRU_INTC_HOST PRU_INTC_CHANNEL

#define PRU_INTC_HMR1_REG    0x804 // this is PRU_INTC_HMR2_REG in __prussdrv.h
#define AM33XX_INTC_PHYS_BASE 0x4a320000
#define PRU_INTC_SIZE 0x2000
#define PRU_INTC_SIPR1_REG   0xD00
#define PRU_INTC_SIPR2_REG   0xD04

#define PRU_INTC_ESR0_REG    0x300 // this is PRU_INTC_ESR1_REG in __prussdrv.h

#define PRU_INTC_SITR1_REG   0xD80
#define PRU_INTC_SITR2_REG   0xD84

#define PRU_INTC_CMR5_REG    0x414 // note: __prussdrv.h calls this PRU_INTC_CMR6_REG
#define PRU_INTC_SECR1_REG   0x280
#define PRU_INTC_SECR2_REG   0x284
#define PRU_INTC_HIER_REG    0x1500
#define PRU_INTC_GER_REG     0x010
#define PRU_INTC_EISR_REG    0x028
#define PRU_INTC_HIEISR_REG  0x034

#endif /* ifdef PRU_IRQ */

struct hello_rt_context {
	int* buf;
	int size;
	void* gpio1_addr;
	rtdm_irq_t irq_n;
	void* pruintc_io;
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
	int status;

 	ctx = ((struct hello_rt_context*)irq_handle->cookie);
#ifdef PRU_IRQ
	// 4.4.2.3.5 Interrupt status clearing
	// check the pending enabled status (is it enabled AND has it been triggered?)
	status = ioread32(ctx->pruintc_io + PRU_INTC_SECR1_REG) & (1 << PRU_SYSTEM_EVENT);
	if(status)
		rtdm_event_pulse(&ctx->event);

	//rtdm_printk(KERN_WARNING "Received interrupt, status: %d\n", status);
	// clear the event
	iowrite32((1 << PRU_SYSTEM_EVENT), ctx->pruintc_io + PRU_INTC_SECR1_REG);
#endif
	//rtdm_printk("irq %i: %08x\n", *((int*)irq_handle), irq_status);

	// clear interrupt bit in gpio registers to prevent irq refiring
#ifdef GPIO_IRQ
	rtdm_event_pulse(&ctx->event);
	irq_status = ioread32(ctx->gpio1_addr + 0x2c);
	iowrite32(irq_status, ctx->gpio1_addr + 0x2c);
#endif
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
#ifdef PRU_IRQ
void init_pru(struct hello_rt_context *ctx){
	unsigned int value;
	ctx->pruintc_io = ioremap(AM33XX_INTC_PHYS_BASE, PRU_INTC_SIZE);
	iowrite32(0xFFFFFFFF, ctx->pruintc_io + PRU_INTC_SIPR1_REG);
	iowrite32(0xFFFFFFFF,  ctx->pruintc_io + PRU_INTC_SIPR2_REG);
	iowrite32(0x0, ctx->pruintc_io + PRU_INTC_SITR1_REG);
	iowrite32(0x0, ctx->pruintc_io + PRU_INTC_SITR2_REG);

	// we are triggering event 4 from the PRU by doing:
	// MOV R31.b0, (1 << 5) | 4
	// which is system event 20 of the PRU INTC controller

	// 4.4.2.5 INTC Basic Programming Model
	// map system event 20 to interrupt controller channel 4
	// we are using CMR5 because that is system event 20
	iowrite32(PRU_INTC_CHANNEL, ctx->pruintc_io + PRU_INTC_CMR5_REG);

	// map PRU channel interrupt to host 
	iowrite32(PRU_INTC_HOST, ctx->pruintc_io + PRU_INTC_HMR1_REG); //TODO: write only 8 bits

	//clear system events
	iowrite32(0xFFFFFFFF, ctx->pruintc_io + PRU_INTC_SECR1_REG);
	iowrite32(0xFFFFFFF, ctx->pruintc_io + PRU_INTC_SECR2_REG);
	
	//enable host interrupt
	//iowrite32((1 << PRU_INTC_HOST), ctx->pruintc_io + PRU_INTC_HIER_REG);
	//value = ioread32(ctx->pruintc_io + PRU_INTC_HIER_REG);
	//printk(KERN_WARNING "PRU_INTC_HIER_REG: %#x\n", value);

	// 4.4.3.2.1 INTC methodology > Interrupt Processing > Interrupt Enabling
	iowrite32(PRU_SYSTEM_EVENT, ctx->pruintc_io + PRU_INTC_EISR_REG);
	value = ioread32(ctx->pruintc_io + PRU_INTC_EISR_REG);
	// printk(KERN_WARNING "PRU_INTC_EISR_REG: %#x\n", value);

	iowrite32(PRU_INTC_CHANNEL, ctx->pruintc_io + PRU_INTC_HIEISR_REG);

	// not written in the manual
	iowrite32((1 << PRU_SYSTEM_EVENT), ctx->pruintc_io + PRU_INTC_ESR0_REG);

	// enable Global Enable Register
	iowrite32(1, ctx->pruintc_io + PRU_INTC_GER_REG);
	value = ioread32(ctx->pruintc_io + PRU_INTC_HIER_REG);
	printk(KERN_WARNING "PRU_INTC_HIER_REG: %#x\n", value);
}
#endif

void init_intc(struct hello_rt_context *ctx){
	struct device_node *of_node = of_find_node_by_name(NULL, "interrupt-controller");
	struct irq_domain *intc_domain = irq_find_matching_fwnode(&of_node->fwnode, DOMAIN_BUS_ANY); 
	unsigned int irq = irq_create_mapping(intc_domain, irq_number);
	printk(KERN_WARNING "init_intc\n");
	int res = rtdm_irq_request(&ctx->irq_n, irq, irq_handler, 0, "hello_rt_irq", (void*)ctx);
	printk(KERN_ALERT "rtdm irq: %i\n", irq);
	if(res != 0)
		printk(KERN_ALERT "rtdm interrupt registered: %i FAILED\n", res);
}

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	
	printk(KERN_WARNING "Hello_rt_open\n");

	init_intc(ctx);
#ifdef PRU_IRQ
	init_pru(ctx);
#endif
#ifdef GPIO_IRQ
	init_gpio(ctx);
#endif
	rtdm_event_init(&ctx->event, 0);

	ctx->buf = (int*)rtdm_malloc(4096);

	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
#ifdef PRU_IRQ
	// disable the Global Enable Register of the PRU INTC
	// ctx->pruintc_io[PRU_INTC_GER_REG >> 2] = 0;
	iowrite32(0, ctx->pruintc_io + PRU_INTC_GER_REG);
#endif
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
