#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <rtdm/driver.h>
#include <linux/platform_device.h>
MODULE_LICENSE("GPL");

#define CM_PER 0x44e00000
#define CM_PER_SIZE 0x3fff
#define CM_WKUP_ADC 0x4bc

#define EDMA_BASE 0x49000000
#define EDMA_SIZE 0xfffff
#define EDMA_EERH 0x1024
#define EDMA_EESRH 0x1034
#define EDMA_IESR 0x1060
#define EDMA_IPR 0x1068
#define EDMA_ICR 0x1070

#define PARAM_BASE EDMA_BASE+0x4000
#define PARAM_SIZE 0x1f
#define PARAM_OPT 0x0
#define PARAM_SRC 0x4
#define PARAM_CNT 0x8
#define PARAM_DST 0xc
#define PARAM_BIDX 0x10
#define PARAM_LINK 0x14
#define PARAM_CIDX 0x18
#define PARAM_CCNT 0x1c

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
#define ADC_DMA_PORT 0x54c00000

static int NUM_SAMPLES;
module_param(NUM_SAMPLES, int, S_IRUGO|S_IWUSR);

static int CLKDIV_VAL;
module_param(CLKDIV_VAL, int, S_IRUGO|S_IWUSR);

static struct platform_device hello_plat_device = {
	.name = "hello_rt",
};

dma_addr_t dma_handle;
void* dma_buffer_k;

struct hello_rt_context {
	int* buf;
	int size;
	void* gpio1_addr;
	void* adc_addr;
	void* edma_addr;
	void* param_addr;;
	rtdm_irq_t irq_n;
	unsigned int linux_irq;
	rtdm_event_t event;
	nanosecs_abs_t irq_start;
	nanosecs_abs_t irq_stop;
	struct device *dev;
	void *dma_buffer_k;
	dma_addr_t dma_handle;
};

static int irq_handler_dma(rtdm_irq_t * irq_handle){
	struct hello_rt_context *ctx = ((struct hello_rt_context*)irq_handle->cookie);
	unsigned int irq_status;

	rtdm_printk("dma interrupt recieved\n");
	irq_status = ioread32(ctx->edma_addr + EDMA_IPR);
	if (irq_status == 0x7){
		iowrite32(irq_status, ctx->edma_addr + EDMA_ICR);
		rtdm_printk("handled\n");
		return RTDM_IRQ_HANDLED;
	}

	return RTDM_IRQ_NONE;

}

static int irq_handler(rtdm_irq_t *irq_handle){
	unsigned int irq_status;
	struct hello_rt_context *ctx = ((struct hello_rt_context*)irq_handle->cookie);
	int numSamples, i, numS;

	ctx->irq_start = rtdm_clock_read();

//	numSamples = ioread32(ctx->adc_addr + ADC_FIFO0_COUNT);
//	if (numSamples != NUM_SAMPLES){
//		rtdm_printk("number of samples in FIFO: %i\n", numSamples);
//	}

	// empty fifo into buffer
//	for (i=0; i<numSamples; i++){
//		ctx->buf[i] = ioread32(ctx->adc_addr + ADC_FIFO0_DATA);
//	}
	memcpy_fromio(ctx->buf, ctx->adc_addr + ADC_FIFO0_DATA, NUM_SAMPLES*4);
	
//	numS = ioread32(ctx->adc_addr + ADC_FIFO0_COUNT);
//	if (numS != 0){
//		rtdm_printk("number of samples in after FIFO: %i\n", numS);
//	}

	// clear interrupt bit in adc registers to prevent irq refiring
	irq_status = ioread32(ctx->adc_addr + ADC_IRQSTATUS);
	iowrite32(irq_status, ctx->adc_addr + ADC_IRQSTATUS);

//	rtdm_printk("event pulse\n");
	rtdm_event_pulse(&ctx->event);

	ctx->irq_stop = rtdm_clock_read();

	return RTDM_IRQ_HANDLED;
}

void init_adc(struct hello_rt_context *ctx){
	void* cm_per_addr;
	unsigned int adc_clk;

	// turn on the adc interface clock
	cm_per_addr = ioremap(CM_PER, CM_PER_SIZE);
	adc_clk = ioread32(cm_per_addr + CM_WKUP_ADC);
//	printk(KERN_ALERT "adc_clk: %08x\n", adc_clk);
	if (adc_clk != 0x2){
		iowrite32(0x2, cm_per_addr + CM_WKUP_ADC);
		printk(KERN_ALERT "turned on adc ocp clock\n");
	}
	iounmap(cm_per_addr);

	// init adc
	ctx->adc_addr = ioremap(ADC_BASE, ADC_SIZE);
	printk(KERN_ALERT "adc rev: %08x\n", ioread32(ctx->adc_addr));
	iowrite32(0x4, ctx->adc_addr + ADC_CTRL);		// remove stepconfig write protection
	iowrite32(CLKDIV_VAL, ctx->adc_addr + ADC_CLKDIV); 		// set clockdivider to max
	iowrite32(0x2, ctx->adc_addr + ADC_STEPENABLE);
	iowrite32(0x1, ctx->adc_addr + ADC_STEPCONFIG1);	// set stepconfig to continuous
	iowrite32(0x35000000, ctx->adc_addr + ADC_STEPDELAY1);	// set sampledelay to 0
	iowrite32(0x4, ctx->adc_addr + ADC_IRQENABLE_SET);	// enable fifo0 threshold irq
	iowrite32(NUM_SAMPLES-1, ctx->adc_addr + ADC_FIFO0_THRESHOLD);	// set fifo0 threshold 

//	printk(KERN_ALERT "adc config: %08x\n", ioread32(ctx->adc_addr + ADC_STEPCONFIG1));
//	printk(KERN_ALERT "adc enable: %08x\n", ioread32(ctx->adc_addr + ADC_STEPENABLE));

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
	int res;
	struct device_node *of_node = of_find_node_by_name(NULL, "interrupt-controller");
	struct irq_domain *intc_domain = irq_find_matching_fwnode(&of_node->fwnode, DOMAIN_BUS_ANY); 
	ctx->linux_irq = irq_find_mapping(intc_domain, 12);
	if (!ctx->linux_irq){
		printk(KERN_ALERT "bad linux irq\n");
		return;
	}
//	res = rtdm_irq_request(&ctx->irq_n, ctx->linux_irq, irq_handler_dma, 0, "hello_rt_dma_irq", (void*)ctx);
	printk("rtdm interrupt %i registered: %i\n", ctx->linux_irq, res);
	of_node_put(of_node);
}

void init_dma(struct hello_rt_context *ctx){
	int ret;

	ctx->edma_addr = ioremap(EDMA_BASE, EDMA_SIZE);
	ctx->param_addr = ioremap(PARAM_BASE, PARAM_SIZE);
	printk(KERN_ALERT "edma rev: %08x\n", ioread32(ctx->edma_addr));

//	ret = dma_set_coherent_mask(ctx->dev, DMA_BIT_MASK(32));
//	printk(KERN_WARNING "ret: %i\n", DMA_BIT_MASK(32));
	ctx->dma_buffer_k = dma_buffer_k;
	ctx->dma_handle = dma_handle;

	// set edma channel 53 (adc fifo 0) to use param 0
	iowrite32(0x0, ctx->edma_addr + 0x1d4);	
	// enable event 53
	iowrite32((1 << (53-33)), ctx->edma_addr + EDMA_EESRH);	

	// PARAM
	
	// enable transfer-complete interrupt tcc=7
	iowrite32((1 << 20)&(0x7 << 12), ctx->param_addr + PARAM_OPT);	
	// set the source address to ADC_FIFO0
	iowrite32(ADC_BASE + ADC_FIFO0_DATA, ctx->param_addr + PARAM_SRC);
	// set ACNT to NUM_SAMPLES (number of samples in FIFO0) and BCNT to 1 (we are taking a one-dimensional array)
	iowrite32((NUM_SAMPLES << 16)&(1), ctx->param_addr + PARAM_CNT);
	// set the destination address to our DMA buffer
	iowrite32(ctx->dma_handle, ctx->param_addr + PARAM_DST);
	// set address offsets to 0
	iowrite32(0, ctx->param_addr + PARAM_BIDX);
	// set the link address to null (0xffff) to bcntrld to 1
	iowrite32((0xffff << 16)&(0x1), ctx->param_addr + PARAM_LINK);
	// set address offsets to 0
	iowrite32(0, ctx->param_addr + PARAM_CIDX);
	// set ccount to 1
	iowrite32((1 << 16), ctx->param_addr + PARAM_CCNT);

	// enable transfer-complete interrupt for tcc=7
	iowrite32(0x7, ctx->edma_addr + EDMA_IESR);

}

static int hello_rt_open(struct rtdm_fd *fd, int oflags){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);

	ctx->dev = &hello_plat_device.dev;
//	ctx->dev->coherent_dma_mask = DMA_BIT_MASK(32);
//	ctx->dev->dma_mask = &ctx->dev->coherent_dma_mask;

	init_intc(ctx);
	init_dma(ctx);
	// init_gpio(ctx);
	init_adc(ctx);

	rtdm_event_init(&ctx->event, 0);

	ctx->buf = (int*)rtdm_malloc(4096);

	return 0;
}

static void hello_rt_close(struct rtdm_fd *fd){
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	iowrite32(0x4, ctx->adc_addr + ADC_IRQENABLE_CLR);	// disable fifo0 threshold irq
	iowrite32(0x0, ctx->adc_addr + ADC_CTRL);		// disable ADC - neccesary!
	rtdm_irq_free(&ctx->irq_n);
	irq_dispose_mapping(ctx->linux_irq);
	rtdm_free(ctx->buf);
	rtdm_event_pulse(&ctx->event);
	rtdm_event_destroy(&ctx->event);
	rtdm_printk("BYE\n");
}

static ssize_t hello_rt_write_nrt(struct rtdm_fd *fd, const void __user *buf, size_t size){
//	int res;
//	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	
	// res = rtdm_copy_from_user(fd, ctx->buf, buf, size);
	// ctx->size = size;

	rtdm_printk("write!\n");
	return size;
}

static ssize_t hello_rt_read_nrt(struct rtdm_fd *fd, void __user *buf, size_t size){
	int ret, read_count;
	struct hello_rt_context *ctx = rtdm_fd_to_private(fd);
	/* int status = ioread32(ctx->adc_addr + ADC_STATUS);
	int count = ioread32(ctx->adc_addr + ADC_FIFO0_COUNT);
	int config = ioread32(ctx->adc_addr + ADC_STEPCONFIG1);
	int delay = ioread32(ctx->adc_addr + ADC_STEPDELAY1);

	rtdm_printk("status: %08x\ncount: %i\nconfig: %08x\ndelay: %08x\n", status, count, config, delay);
	*/

	rtdm_event_wait(&ctx->event);
	

	ctx->buf[NUM_SAMPLES] = ctx->irq_stop - ctx->irq_start;
	ctx->buf[NUM_SAMPLES+1] = rtdm_clock_read() - ctx->irq_start;
	ret = rtdm_copy_to_user(fd, buf, ctx->buf, (NUM_SAMPLES+2)*4);

	// rtdm_printk("irq: %llu\n handler: %llu\n\n\n", ctx->irq_stop - ctx->irq_start, rtdm_clock_read() - ctx->irq_start);

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

int hello_rt_probe(struct platform_device* pdev){
	return 0;
}

int hello_rt_remove(struct platform_device* pdev){
	printk(KERN_ALERT "remove\n");
	return 0;
}

static struct platform_driver hello_plat_driver = {
	.driver		= {
		.name	= "hello_rt",
		.owner	= THIS_MODULE,
	},
	.probe		= hello_rt_probe,
	.remove		= hello_rt_remove,
};

static int __init hello_rt_init(void){
	platform_device_register(&hello_plat_device);
	platform_driver_register(&hello_plat_driver);
	dma_buffer_k = dma_alloc_coherent(NULL, 4096, &dma_handle, GFP_KERNEL);
	printk(KERN_ALERT "hello_rt loaded %p %p\n", dma_buffer_k, (void*)dma_handle);
	return rtdm_dev_register(&device);
}

static void __exit hello_rt_exit(void){
	platform_driver_unregister(&hello_plat_driver);
	platform_device_del(&hello_plat_device);
	dma_free_coherent(NULL, 4096, dma_buffer_k, dma_handle);
	printk(KERN_ALERT "hello_rt unloaded\n");
	return rtdm_dev_unregister(&device);
}

module_init(hello_rt_init);
module_exit(hello_rt_exit);
