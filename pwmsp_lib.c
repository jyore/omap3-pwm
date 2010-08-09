#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <sound/pcm.h>
#include <asm/io.h>
#include <linux/delay.h>
#include "pwmsp.h"

static int pwmsp_start_playing(struct snd_pwmsp *chip)
{
//	unsigned long ns;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	int duty_cycle,sleep_time,val;
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: start_playing called\n");
#endif
	if (atomic_read(&chip->active)) {
		printk(KERN_ERR "PWMSP: Timer already active\n");
		return -EIO;
	}
	atomic_set(&chip->active, 1);
	substream = chip->playback_substream;
	if (!substream)
		return 0;

	runtime = substream->runtime;
	sleep_time=SLEEP_TIME;
	/* assume it is mono! */
	while(chip->playback_ptr!= runtime->dma_bytes){
		val = runtime->dma_area[chip->playback_ptr ];
		duty_cycle= val*100/256 ;
		if (set_pwm_frequency(pwm_devs,BASE_CLOCK) == -1)
			return -EIO;

		if (set_duty_cycle(pwm_devs,duty_cycle) == -1)
			return -EIO;
		msleep(sleep_time);
		chip->playback_ptr+=sizeof(char);
	}

	if (pwm_off(pwm_devs) == -1)
		return -EIO;

	atomic_set(&chip->active, 0);
	return 0;
}

static int pwmsp_stop_playing(struct snd_pwmsp *chip)
{
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: stop_playing called\n");
#endif

	if (pwm_off(pwm_devs) == -1)
		return -EIO;

	atomic_set(&chip->active, 0);
	return 0;
}

/*
 * Force to stop and sync the stream
 */
void pwmsp_sync_stop(struct snd_pwmsp *chip)
{
	local_irq_disable();
	pwmsp_stop_playing(chip);
	local_irq_enable();
}

static int snd_pwmsp_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
#if PWMSP_DEBUG
	printk(KERN_INFO "PWMSP: close called\n");
#endif
	pwmsp_sync_stop(chip);
	chip->playback_substream = NULL;
	//close(chip->fd);
	return 0;
}

static int snd_pwmsp_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
	int err;
	pwmsp_sync_stop(chip);
	err = snd_pcm_lib_malloc_pages(substream,
				      params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	return 0;
}

static int snd_pwmsp_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: hw_free called\n");
#endif
	pwmsp_sync_stop(chip);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_pwmsp_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: prepare called, "
			"size=%zi psize=%zi f=%zi f1=%i\n",
			snd_pcm_lib_buffer_bytes(substream),
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream) /
			snd_pcm_lib_period_bytes(substream),
			substream->runtime->periods);
#endif
	pwmsp_sync_stop(chip);
	chip->playback_ptr = 0;
	chip->period_ptr = 0;
	chip->fmt_size =
		snd_pcm_format_physical_width(substream->runtime->format) >> 3;
	chip->is_signed = snd_pcm_format_signed(substream->runtime->format);
	return 0;
}

static int snd_pwmsp_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: trigger called\n");
#endif
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return pwmsp_start_playing(chip);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		pwmsp_stop_playing(chip);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t snd_pwmsp_playback_pointer(struct snd_pcm_substream
						   *substream)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
	unsigned int pos;
	spin_lock(&chip->substream_lock);
	pos = chip->playback_ptr;
	spin_unlock(&chip->substream_lock);
	return bytes_to_frames(substream->runtime, pos);
}

static struct snd_pcm_hardware snd_pwmsp_playback = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_HALF_DUPLEX |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = (SNDRV_PCM_FMTBIT_U8
	    ),
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = PWMSP_DEFAULT_SRATE,
	.rate_max = PWMSP_DEFAULT_SRATE,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = PWMSP_BUFFER_SIZE,
	.period_bytes_min = 64,
	.period_bytes_max = PWMSP_MAX_PERIOD_SIZE,
	.periods_min = 2,
	.periods_max = PWMSP_MAX_PERIODS,
	.fifo_size = 0,
};

static int snd_pwmsp_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pwmsp *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
#if PWMSP_DEBUG
	printk(KERN_INFO "pwmsp: open called\n");
#endif
//	chip->fd = open("/dev/pwm9", O_RDWR);
	if (atomic_read(&chip->active)) {
		printk(KERN_ERR "pwmsp: still active!!\n");
		return -EBUSY;
	}
	runtime->hw = snd_pwmsp_playback;
	chip->playback_substream = substream;
	return 0;
}

static struct snd_pcm_ops snd_pwmsp_playback_ops = {
	.open = snd_pwmsp_playback_open,
	.close = snd_pwmsp_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_pwmsp_playback_hw_params,
	.hw_free = snd_pwmsp_playback_hw_free,
	.prepare = snd_pwmsp_playback_prepare,
	.trigger = snd_pwmsp_trigger,
	.pointer = snd_pwmsp_playback_pointer,
};

int __devinit snd_pwmsp_new_pcm(struct snd_pwmsp *chip)
{
	int err;
	printk(KERN_ALERT"210 \n");
	err = snd_pcm_new(chip->card, "pwmspeaker", 0, 1, 0, &chip->pcm);
	if (err < 0)
		return err;
	printk(KERN_ALERT"214 \n");
	snd_pcm_set_ops(chip->pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_pwmsp_playback_ops);

	chip->pcm->private_data = chip;
	chip->pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	strcpy(chip->pcm->name, "pwmsp");
	printk(KERN_ALERT"ping \n");
	snd_pcm_lib_preallocate_pages_for_all(chip->pcm,
					      SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data
					      (GFP_KERNEL), PWMSP_BUFFER_SIZE,
					      PWMSP_BUFFER_SIZE);
	printk(KERN_ALERT"ping2 \n");
	return 0;
}
//module_init(snd_pwmsp_new_pcm);
MODULE_LICENSE("GPL");
EXPORT_SYMBOL(pwmsp_sync_stop);
EXPORT_SYMBOL(snd_pwmsp_new_pcm);
