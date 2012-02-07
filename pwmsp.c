#include <linux/init.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <asm/bitops.h>
#include "pwmsp.h"

MODULE_DESCRIPTION("PWM-Speaker driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{PWM-Speaker, pwmsp}}");
MODULE_ALIAS("platform:pwmspkr");

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int enable = SNDRV_DEFAULT_ENABLE1;	/* Enable this card */

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for pwmsp soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for pwmsp soundcard.");
module_param(enable, bool, 0444);
MODULE_PARM_DESC(enable, "Enable PWM-Speaker sound.");

struct snd_pwmsp pwmsp_chip;

static int __devinit snd_pwmsp_create(struct snd_card *card)
{
	static struct snd_device_ops ops = { };
//      struct timespec tp;
	int err;
#if PWMSP_DEBUG
	printk("pwmsp: lpj=%li, min_div=%i, res=%li\n",
	       loops_per_jiffy, min_div, tp.tv_nsec);
#endif

	pwmsp_chip.max_treble = PWMSP_MAX_TREBLE;	//min(order, PWMSP_MAX_TREBLE);
	pwmsp_chip.treble = min(pwmsp_chip.max_treble, PWMSP_DEFAULT_TREBLE);
	pwmsp_chip.playback_ptr = 0;
	pwmsp_chip.period_ptr = 0;
	atomic_set(&pwmsp_chip.active, 0);
	pwmsp_chip.enable = 1;

	spin_lock_init(&pwmsp_chip.substream_lock);

	pwmsp_chip.card = card;
	pwmsp_chip.port = 0x61;	//what?
	pwmsp_chip.irq = -1;
	pwmsp_chip.dma = -1;

	/* Register device */
	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, &pwmsp_chip, &ops);
	if (err < 0)
		return err;

	return 0;
}

static int __devinit snd_card_pwmsp_probe(int devnum, struct device *dev)
{
	struct snd_card *card;
	int err;

	if (devnum != 0)
		return -EINVAL;

	snd_card_create(index, id, THIS_MODULE, 0,&card);
	if (!card)
		return -ENOMEM;

	err = snd_pwmsp_create(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	err = snd_pwmsp_new_pcm(&pwmsp_chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	snd_card_set_dev(pwmsp_chip.card, dev);

	strcpy(card->driver, "PWM-Speaker");
	strcpy(card->shortname, "pwmsp");
	sprintf(card->longname, "Internal PWM-Speaker at port 0x%x",
		pwmsp_chip.port);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	return 0;
}

static int __devinit alsa_card_pwmsp_init(struct device *dev)
{
	int err;

	err = snd_card_pwmsp_probe(0, dev);
	if (err) {
		printk(KERN_ERR "PWM-Speaker initialization failed.\n");
		return err;
	}
#ifdef CONFIG_DEBUG_PAGEALLOC
	/* Well, CONFIG_DEBUG_PAGEALLOC makes the sound horrible. Lets alert */
	printk(KERN_WARNING "PWMSP: CONFIG_DEBUG_PAGEALLOC is enabled, "
	       "which may make the sound noisy.\n");
#endif

	return 0;
}

static void __devexit alsa_card_pwmsp_exit(struct snd_pwmsp *chip)
{
	snd_card_free(chip->card);
}

static int __devinit pwmsp_probe(struct platform_device *dev)
{
	int err;

	/*err = pwmspkr_input_init(&pwmsp_chip.input_dev, &dev->dev);
	   if (err < 0)
	   return err; */

	err = alsa_card_pwmsp_init(&dev->dev);
	/*if (err < 0) {
	   pwmspkr_input_remove(pwmsp_chip.input_dev);
	   return err;
	   } */

	platform_set_drvdata(dev, &pwmsp_chip);
	return 0;
}

static int __devexit pwmsp_remove(struct platform_device *dev)
{
	struct snd_pwmsp *chip = platform_get_drvdata(dev);
	alsa_card_pwmsp_exit(chip);
//      pwmspkr_input_remove(chip->input_dev);
	platform_set_drvdata(dev, NULL);
	return 0;
}

static void pwmsp_stop_beep(struct snd_pwmsp *chip)
{
	pwmsp_sync_stop(chip);
	//pwmspkr_stop_sound();
}

#ifdef CONFIG_PM
static int pwmsp_suspend(struct platform_device *dev, pm_message_t state)
{
	struct snd_pwmsp *chip = platform_get_drvdata(dev);
	pwmsp_stop_beep(chip);
	snd_pcm_suspend_all(chip->pcm);
	return 0;
}
#else
#define pwmsp_suspend NULL
#endif /* CONFIG_PM */

static void pwmsp_shutdown(struct platform_device *dev)
{
	struct snd_pwmsp *chip = platform_get_drvdata(dev);
	pwmsp_stop_beep(chip);
}

static struct platform_driver pwmsp_platform_driver = {
	.driver = {
		   .name = "pwmspkr",
		   .owner = THIS_MODULE,
		   },
	.probe = pwmsp_probe,
	.remove = __devexit_p(pwmsp_remove),
	.suspend = pwmsp_suspend,
	.shutdown = pwmsp_shutdown,
};

static int __init pwmsp_init(void)
{
	printk(KERN_ALERT "189 \n");
	if (!enable)
		return -ENODEV;
	return platform_driver_register(&pwmsp_platform_driver);
	printk(KERN_ALERT "193 \n");
}

static void __exit pwmsp_exit(void)
{
	platform_driver_unregister(&pwmsp_platform_driver);
}

module_init(pwmsp_init);
module_exit(pwmsp_exit);
