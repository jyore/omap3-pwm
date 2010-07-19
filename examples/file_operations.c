#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
//#define PWM_IOC_MAGIC  0x00 
 /**/
#define PWM_SET_DUTYCYCLE 1074003969
#define PWM_GET_DUTYCYCLE 1074003970
#define PWM_SET_FREQUENCY 1074003971
#define PWM_GET_FREQUENCY 1074003972
#define PWM_ON 1074003973
#define PWM_OFF 1074003974
#define PWM_SET_POLARITY 1074003975
main()
{
	int fd, status, k, f, sc;

	fd = open("/dev/pwm9", O_RDWR);
	scanf("%d", &k);
	scanf("%d", &f);
	scanf("%d", &sc);

	if (ioctl(fd, PWM_SET_FREQUENCY, f) == -1)
		printf("TIOCMGET failed: %s\n", strerror(errno));

	if (ioctl(fd, PWM_SET_DUTYCYCLE, k) == -1)
		printf("TIOCMGET failed: %s\n", strerror(errno));

	if (ioctl(fd, PWM_SET_POLARITY, sc) == -1)
		printf("TIOCMGET failed: %s\n", strerror(errno));
	printf("%d",ioctl(fd, PWM_GET_FREQUENCY, sc));
	printf("\n%d",ioctl(fd, PWM_GET_DUTYCYCLE, sc));

	close(fd);
}
