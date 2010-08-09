#ifndef __PWMSP_H__
#define __PWMSP_H__
#define PWMSP_SOUND_VERSION 0x100	/* read 1.00 */
#define PWMSP_DEBUG 0
#define PWMSP_MAX_TREBLE 1
#define PWMSP_DEFAULT_TREBLE 0
#define PWMSP_DEFAULT_SRATE 128	// 128, 256, 512 ... 16384Kb/Sec how to decide??
#define PWMSP_MAX_PERIOD_SIZE	(64*1024)
#define PWMSP_MAX_PERIODS	512
#define PWMSP_BUFFER_SIZE	(128*1024)
#define DATA_BITS	256
#define BASE_CLOCK 	(DATA_BITS*8192)	//256*8khz
#define SLEEP_TIME	(DATA_BITS*1000/BASE_CLOCK)	//milliseconds
/*defines for ioctl()*/
#include "pwm.h"
struct snd_pwmsp {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct input_dev *input_dev;
	//int fd;
	unsigned short port, irq, dma;
	spinlock_t substream_lock;
	struct snd_pcm_substream *playback_substream;
	unsigned int fmt_size;
	unsigned int is_signed;
	size_t playback_ptr;
	size_t period_ptr;
	atomic_t active;
	int enable;
	int max_treble;
	int treble;
};

extern struct snd_pwmsp pwmsp_chip;
extern void pwmsp_sync_stop(struct snd_pwmsp *chip);
extern int snd_pwmsp_new_pcm(struct snd_pwmsp *chip);
extern struct pwm_dev *pwm_devs;
extern int set_pwm_frequency(struct pwm_dev *, int);
extern int set_duty_cycle(struct pwm_dev *, int);
extern int pwm_off(struct pwm_dev *);
#endif
