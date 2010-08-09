/*
 Copyright (c) 2010, Scott Ellis
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the <organization> nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Scott Ellis ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Scott Ellis BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/ioctl.h>

#include "pwm.h"

/* default frequency of 1 kHz */
#define DEFAULT_TLDR	0xFFFFFFE0

/* default 50% duty cycle */
/* TMAR = (0xFFFFFFFF - ((0xFFFFFFFF - (DEFAULT_TLDR + 1)) / 2)) */
#define DEFAULT_TMAR	0xFFFFFFEF

/* default TCLR is off state */
#define DEFAULT_TCLR (GPT_TCLR_PT | GPT_TCLR_TRG_OVFL_MATCH | GPT_TCLR_CE | GPT_TCLR_AR)

#define DEFAULT_PWM_FREQUENCY 1024
#define DEFAULT_DUTY_CYCLE 100

#define MAX 1000		//some high value

static int frequency_param = DEFAULT_PWM_FREQUENCY;
module_param(frequency_param, int, S_IWUSR);
MODULE_PARM_DESC(frequency_param,
		 "The PWM frequency, power of two, max of 16384");

static int duty_cycle_param = DEFAULT_DUTY_CYCLE;
module_param(duty_cycle_param, int, S_IWUSR);

static int pwm9_enable = 0;
module_param(pwm9_enable, int, S_IWUSR);

static int pwm10_enable = 0;
module_param(pwm10_enable, int, S_IWUSR);

static int pwm11_enable = 0;
module_param(pwm11_enable, int, S_IWUSR);

int pwm_enable[3] = { 0, 0, 0 };

int pwm_major = PWM_MAJOR;
int pwm_minor = 0;
dev_t dv = 0;

#define USER_BUFF_SIZE	128

struct gpt {
	u32 timer_num;
	u32 mux_offset;
	u32 gpt_base;
	u32 input_freq;
	u32 old_mux;
	u32 tldr;
	u32 tmar;
	u32 tclr;
	u32 num_freqs;
};

struct pwm_dev {
	struct cdev cdev;
	struct class *class;
	struct semaphore sem;
	struct gpt gpt;
	int frequency, duty_cycle;
	char *user_buff;
};
struct pwm_dev *pwm_devs;
//unsigned int duty_cycle;

static int init_mux(struct pwm_dev *dev)
{
	void __iomem *base;

	base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);
	if (!base) {
		printk(KERN_ALERT "init_mux(): ioremap() failed\n");
		return -1;
	}

	dev->gpt.old_mux = ioread16(base + dev->gpt.mux_offset);
	iowrite16(PWM_ENABLE_MUX, base + dev->gpt.mux_offset);
	iounmap(base);

	return 0;
}

static int restore_mux(struct pwm_dev *dev)
{
	void __iomem *base;

	if (dev->gpt.old_mux) {
		base = ioremap(OMAP34XX_PADCONF_START, OMAP34XX_PADCONF_SIZE);

		if (!base) {
			printk(KERN_ALERT "restore_mux(): ioremap() failed\n");
			return -1;
		}

		iowrite16(dev->gpt.old_mux, base + dev->gpt.mux_offset);
		iounmap(base);
	}

	return 0;
}

static int set_pwm_frequency(struct pwm_dev *dev, int freq)
{
	void __iomem *base;
	//int frequency = dev->frequency;
	dev->frequency=freq;
	base = ioremap(dev->gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "set_pwm_frequency(): ioremap failed\n");
		return -1;
	}

	if (dev->frequency < 0) {
		dev->frequency = DEFAULT_PWM_FREQUENCY;
	} else {
		/* only powers of two, for simplicity */
		dev->frequency &= ~0x01;

		if (dev->frequency > (dev->gpt.input_freq / 2))
			dev->frequency = dev->gpt.input_freq / 2;
		else if (dev->frequency == 0)
			dev->frequency = DEFAULT_PWM_FREQUENCY;
	}

	/* PWM_FREQ = 32768 / ((0xFFFF FFFF - TLDR) + 1) */
	dev->gpt.tldr =
	    0xFFFFFFFF - ((dev->gpt.input_freq / dev->frequency) - 1);

	/* just for convenience */
	dev->gpt.num_freqs = 0xFFFFFFFE - dev->gpt.tldr;

	iowrite32(dev->gpt.tldr, base + GPT_TLDR);

	/* initialize TCRR to TLDR, have to start somewhere */
	iowrite32(dev->gpt.tldr, base + GPT_TCRR);

	iounmap(base);

	return 0;
}

static int pwm_off(struct pwm_dev *dev)
{
	void __iomem *base;

	base = ioremap(dev->gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	dev->gpt.tclr &= ~GPT_TCLR_ST;
	iowrite32(dev->gpt.tclr, base + GPT_TCLR);
	iounmap(base);

	return 0;
}

static int pwm_on(struct pwm_dev *dev)
{
	void __iomem *base;

	base = ioremap(dev->gpt.gpt_base, GPT_REGS_PAGE_SIZE);

	if (!base) {
		printk(KERN_ALERT "pwm_on(): ioremap failed\n");
		return -1;
	}

	/* set the duty cycle */
	iowrite32(dev->gpt.tmar, base + GPT_TMAR);

	/* now turn it on */
	dev->gpt.tclr = ioread32(base + GPT_TCLR);
	dev->gpt.tclr |= GPT_TCLR_ST;
	iowrite32(dev->gpt.tclr, base + GPT_TCLR);
	iounmap(base);

	return 0;
}

static int scpwm(struct pwm_dev *dev, int sc)
{
	void __iomem *base;

	base = ioremap(dev->gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	if (sc == 1)
		dev->gpt.tclr |= GPT_TCLR_SCPWM;
	else
		dev->gpt.tclr &= ~GPT_TCLR_SCPWM;

	iowrite32(dev->gpt.tclr, base + GPT_TCLR);
	iounmap(base);

	return 0;
}

static int prescale(struct pwm_dev *dev, int div)
{
	void __iomem *base;
	int i = 0;
	base = ioremap(dev->gpt.gpt_base, GPT_REGS_PAGE_SIZE);
	if (!base) {
		printk(KERN_ALERT "pwm_off(): ioremap failed\n");
		return -1;
	}

	while (div > 2) {
		i++;
		div /= 2;
	}

	dev->gpt.tclr |= GPT_TCLR_PRE;	//enable prescaler
	dev->gpt.tclr &= i << 2;	//set prescaler ratio
	iowrite32(dev->gpt.tclr, base + GPT_TCLR);
	iounmap(base);

	return 0;
}

static int set_duty_cycle(struct pwm_dev *dev,int duty_cycle)
{
	unsigned int new_tmar;
	pwm_off(dev);
	dev->duty_cycle=duty_cycle;

	if (dev->duty_cycle == 0)
		return 0;

	new_tmar = (dev->duty_cycle * dev->gpt.num_freqs) / 100;

	if (new_tmar < 1) {
		new_tmar = 1;
		printk(KERN_ALERT "new_tmar = 1\n");
	} else if (new_tmar > dev->gpt.num_freqs) {
		new_tmar = dev->gpt.num_freqs;
		printk(KERN_ALERT "new_tmar = dev->gpt.num_freqs\n");
	}

	dev->gpt.tmar = dev->gpt.tldr + new_tmar;

	return pwm_on(dev);
}

int pwm_ioctl(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{

	//int err = 0, tmp;
	int retval = 0;
	struct pwm_dev *dev = filp->private_data;
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != PWM_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > PWM_IOC_MAXNR)
		return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	/*if (_IOC_DIR(cmd) & _IOC_READ)
	   err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	   else if (_IOC_DIR(cmd) & _IOC_WRITE)
	   err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	   if (err) return -EFAULT; */

	switch (cmd) {

	case PWM_ON:
		if (pwm_on(dev))
			retval = -EIO;
		break;

	case PWM_OFF:
		if (pwm_off(dev))
			retval = -EIO;
		break;

	case PWM_SET_DUTYCYCLE:
		//dev->duty_cycle = arg;
		if (set_duty_cycle(dev,arg))
			retval = -EIO;
		break;

	case PWM_GET_DUTYCYCLE:

		if (dev->gpt.tclr & GPT_TCLR_ST) {	//PWM is on
			retval = (100 * (dev->gpt.tmar - dev->gpt.tldr))
			    / dev->gpt.num_freqs;	//real duty cycle
		} else {
			printk(KERN_ALERT "PWM%d is OFF\n", dev->gpt.timer_num);
			retval = -EIO;
		}

		break;

	case PWM_SET_FREQUENCY:
		//dev->frequency = arg;
		if (set_pwm_frequency(dev,arg))
			retval = -EIO;

		//if(set_duty_cycle(dev))
		//retval = -EIO;
		break;

	case PWM_GET_FREQUENCY:
		retval = dev->frequency;
		break;

	case PWM_SET_POLARITY:
		if (scpwm(dev, arg))
			retval = -EIO;
		break;

	case PWM_SET_CLK:
		if (dev->gpt.timer_num == 9) {
			printk(KERN_ALERT
			       "Only 32K clk can be used with GPT9\n");
			retval = -EIO;
		} else {
			if (arg == 1)
				dev->gpt.input_freq = CLK_13K_FREQ;
			else
				dev->gpt.input_freq = CLK_32K_FREQ;
		}
		break;

	case PWM_SET_PRE:
		if (prescale(dev, arg))
			retval = -EIO;
		break;

	default:		/* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}

static ssize_t pwm_read(struct file *filp, char __user * buff, size_t count,
			loff_t * offp)
{
	size_t len;
	ssize_t error = 0;
	struct pwm_dev *dev = filp->private_data;

	if (!buff)
		return -EFAULT;

	/* tell the user there is no more */
	if (*offp > 0)
		return 0;

	if (down_interruptible(&(dev->sem)))
		return -ERESTARTSYS;

	if (set_pwm_frequency(dev,dev->frequency))
		error = -EIO;

	if (dev->gpt.tclr & GPT_TCLR_ST) {
		dev->duty_cycle = (100 * (dev->gpt.tmar - dev->gpt.tldr))
		    / dev->gpt.num_freqs;

		snprintf(dev->user_buff, USER_BUFF_SIZE,
			 "PWM%d Frequency %u Hz Duty Cycle %u%%\n",
			 dev->gpt.timer_num, dev->frequency, dev->duty_cycle);
	} else {
		snprintf(dev->user_buff, USER_BUFF_SIZE,
			 "PWM%d Frequency %u Hz Stopped\n",
			 dev->gpt.timer_num, dev->frequency);
	}

	len = strlen(dev->user_buff);

	if (len + 1 < count)
		count = len + 1;

	if (copy_to_user(buff, dev->user_buff, count)) {
		printk(KERN_ALERT "pwm_read(): copy_to_user() failed\n");
		error = -EFAULT;
	} else {
		*offp += count;
		error = count;
	}

	up(&(dev->sem));

	return error;
}

static ssize_t pwm_write(struct file *filp, const char __user * buff,
			 size_t count, loff_t * offp)
{
	size_t len;

	ssize_t error = 0;
	struct pwm_dev *dev = filp->private_data;

	if (down_interruptible(&(dev->sem)))
		return -ERESTARTSYS;

	if (!buff || count < 1) {
		printk(KERN_ALERT "pwm_write(): input check failed\n");
		error = -EFAULT;
		goto pwm_write_done;
	}

	/* we are only expecting a small integer, ignore anything else */
	if (count > 8)
		len = 8;
	else
		len = count;

	if (set_pwm_frequency(dev,dev->frequency))
		error = -EIO;

	memset(dev->user_buff, 0, 16);

	if (copy_from_user(dev->user_buff, buff, len)) {
		printk(KERN_ALERT "pwm_write(): copy_from_user() failed\n");
		error = -EFAULT;
		goto pwm_write_done;
	}

	dev->duty_cycle = simple_strtoul(dev->user_buff, NULL, 0);

	set_duty_cycle(dev,dev->duty_cycle);

	/* pretend we ate it all */
	*offp += count;

	error = count;

      pwm_write_done:

	up(&(dev->sem));

	return error;
}

static int pwm_open(struct inode *inode, struct file *filp)
{
	int error = 0;
	struct pwm_dev *dev;	/* device information */
	/*int d=PWM_DUTYCYCLE;
	   int f=PWM_FREQUENCY;
	   int on=PWM_ON;
	   int off=PWM_OFF; */
	int f = PWM_SET_DUTYCYCLE;
	dev = container_of(inode->i_cdev, struct pwm_dev, cdev);
	filp->private_data = dev;	/* for other methods */

	if (down_interruptible(&(dev->sem)))
		return -ERESTARTSYS;

	if (dev->gpt.old_mux == 0) {
		if (init_mux(dev))
			error = -EIO;
		else if (set_pwm_frequency(dev,dev->frequency))
			error = -EIO;
	}

	if (!dev->user_buff) {
		dev->user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL);
		if (!dev->user_buff)
			error = -ENOMEM;
	}
	printk(KERN_ALERT "%d \n", f);

	up(&(dev->sem));

	return error;
}

static struct file_operations pwm_fops = {
	.owner = THIS_MODULE,
	.read = pwm_read,
	.write = pwm_write,
	.open = pwm_open,
	.ioctl = pwm_ioctl,
};

static int __init pwm_init_cdev(struct pwm_dev *dev, int index)
{
	int error;
	dev_t d;
	d = MKDEV(MAJOR(dv), MINOR(dv) + index);
	cdev_init(&(dev->cdev), &pwm_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &pwm_fops;
	error = cdev_add(&(dev->cdev), d, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(d, 1);
		return -1;
	}

	return 0;
}

static int __init pwm_init_class(struct pwm_dev *dev, int index)
{
	dev_t d;
	char name[] = "pwm";
	char temp = index + 9;
	strcat(name, &temp);

	dev->class = class_create(THIS_MODULE, name);
	d = MKDEV(MAJOR(dv), MINOR(dv) + index);
	if (!dev->class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(dev->class, NULL, d, NULL, "pwm%d", 9 + index)) {	//MINOR(pwm_dev.devt)
		printk(KERN_ALERT "device_create(..., pwm) failed\n");
		class_destroy(dev->class);
		return -1;
	}

	return 0;
}

static void __exit pwm_exit(void)
{
	int i = 0;
	dev_t d;
	for (i = 0; i < PWM_NR; i++) {
		if (pwm_enable[i]) {
			d = MKDEV(MAJOR(dv), MINOR(dv) + i);
			device_destroy(pwm_devs[i].class, d);
			class_destroy(pwm_devs[i].class);
			cdev_del(&pwm_devs[i].cdev);
			unregister_chrdev_region(d, 1);
			pwm_off(&pwm_devs[i]);
			restore_mux(&pwm_devs[i]);
			if (pwm_devs[i].user_buff)
				kfree(pwm_devs[i].user_buff);
		}
	}
}

module_exit(pwm_exit);

static int __init pwm_init(void)
{
	int error = 0;
	int count = 0;
	int min = MAX;
	int i = 0, j = 0;
	dev_t d;
	pwm_enable[0] = pwm9_enable;
	pwm_enable[1] = pwm10_enable;
	pwm_enable[2] = pwm11_enable;

	for (i = 0; i < PWM_NR; i++) {
		if (pwm_enable[i]) {
			if (i + 9 < min)
				min = i + 9;
			count++;
		}
	}

	error = alloc_chrdev_region(&dv, min, count, "pwm");

	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", error);
		return -1;
	}

	pwm_devs = kmalloc(PWM_NR * sizeof(struct pwm_dev), GFP_KERNEL);
	if (!pwm_devs) {
		error = -ENOMEM;
		goto init_fail;	/* Make this more graceful */
	}

	memset(pwm_devs, 0, PWM_NR * sizeof(struct pwm_dev));

	for (i = 0; i < PWM_NR; i++) {
		if (pwm_enable[i]) {
			/* change these 4 values to use a different PWM */

			pwm_devs[i].gpt.timer_num = 9 + i;
			pwm_devs[i].gpt.mux_offset = gpt_offset[i];
			pwm_devs[i].gpt.gpt_base = gpt_base[i];
			pwm_devs[i].gpt.input_freq = CLK_32K_FREQ;
			pwm_devs[i].gpt.tldr = DEFAULT_TLDR;
			pwm_devs[i].gpt.tmar = DEFAULT_TMAR;
			pwm_devs[i].gpt.tclr = DEFAULT_TCLR;
			pwm_devs[i].frequency = frequency_param;
			pwm_devs[i].duty_cycle = duty_cycle_param;
			sema_init(&pwm_devs[i].sem, 1);
			if (pwm_init_cdev(&pwm_devs[i], i))
				goto init_fail_1;
			if (pwm_init_class(&pwm_devs[i], i))
				//er_count=i;
				goto init_fail_2;
		}

	}
	return 0;
      init_fail_2:
	for (j = 0; j < PWM_NR; j++) {
		if (pwm_enable[j]) {
			cdev_del(&pwm_devs[j].cdev);

			d = MKDEV(MAJOR(dv), MINOR(dv) + j);
			unregister_chrdev_region(d, 1);
		}
	}
	return error;
      init_fail_1:
	return error;

      init_fail:
	pwm_exit();
	return error;
}

module_init(pwm_init);

EXPORT_SYMBOL(pwm_devs);
EXPORT_SYMBOL(set_pwm_frequency);
EXPORT_SYMBOL(set_duty_cycle);
EXPORT_SYMBOL(pwm_off);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Ellis - Jumpnow");
MODULE_DESCRIPTION("PWM example for OMAP3");
