#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <poll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "gps_parser.h"

/**
 * Macro & global definition
 */

#define TTY_GPS_DEV         "/tmp/gps_fifo"
#define TTY_SIMU_DATA       "gps_data.txt"

#define TraceImport printf
#define TraceInfo printf
#define TraceErr printf

enum GPS_SYMBOL {
    GPS_CR = '\r',
    GPS_LF = '\n',
    GPS_CM = ',',
    GPS_ST = '$'
};

static gps_data_t gps_data;
static gps_buffer_t gps_buffer;

void *hm_mallocz(unsigned int size)
{
	return calloc(1, size);
}

void hm_free(void *ptr)
{
	free(ptr);
}

typedef void *(*thread_rountine)(void *);

void thread(thread_rountine t)
{
	pthread_t pid;
	pthread_create(&pid, NULL, t, NULL);
}

void *simulate_gps(void *arg)
{
	FILE *fp = fopen(TTY_SIMU_DATA, "r");
	if (!fp)
	{
		printf("open simulate gps data error\n");
		return NULL;
	}
	//unlink(TTY_GPS_DEV);
	mkfifo(TTY_GPS_DEV,0666);
	int fd = open(TTY_GPS_DEV, O_RDWR);
	for( ; ; )
	{
		char buffer[256] = {0};
		fseek(fp, 0, SEEK_SET);

		fgets(buffer, 256, fp);

		write(fd, buffer, sizeof(buffer));
		printf("write to fifo %s\n", buffer);
		//unlink(TTY_GPS_DEV);
		sleep(5);
	}
	fclose(fp);
	return NULL;
}

void data_init() {
    TraceImport("Initialize GPS data memory\n");
    memset(&gps_buffer, 0, sizeof(gps_buffer));

    memset(&gps_data, 0, sizeof(gps_data));
}

static size_t timeout_read(int fd, void *buf, size_t nbytes, unsigned int timout) {
    int nfds;
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = timout;
    tv.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    nfds = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (nfds <= 0) {
        if (nfds == 0)
            errno = ETIME;
        return (-1);
    }

    return (read(fd, buf, nbytes));
}

static size_t timeout_readn(int fd, void *buf, size_t nbytes, unsigned int timout) {
    size_t nleft;
    ssize_t nread;

    nleft = nbytes;

    while (nleft > 0) {
        if ((nread = timeout_read(fd, buf, nleft, timout)) < 0) {
            if (nleft == nbytes)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount read so far */
        } else if (nread == 0) {
            break; /* EOF */
        }
        nleft -= nread;
        buf += nread;
    }
    return (nbytes - nleft); /* return >= 0 */
}

void parse_buffer()
{
	char cmd[6] = {0};
	strncpy(cmd, gps_buffer.buffer, 6);
	if(!strcmp(cmd, "$GPGGA"))
	{
		printf("----get GPGGA data----\n");
	}
}

void data_simulation() {
    int fd = -1;
    int rlen = 0;
    //unlink(TTY_GPS_DEV);
	mkfifo(TTY_GPS_DEV,0666);
    fd = open(TTY_GPS_DEV, O_RDWR);
    if (fd < 0) {
        TraceErr("Open simulation GPS data file error\n");
        return;
    }

    for( ; ; ) {
        memset(gps_buffer.buffer, 0, TTY_GPS_DATA_LEN);

        rlen = timeout_readn(fd, gps_buffer.buffer, TTY_GPS_DATA_LEN, 1);
        if (rlen <= 0) {
        	TraceInfo("Read data error\n");
            continue;
        }
        else {
            TraceInfo("Read tty data length %d\n", rlen);
            printf("%s\n", gps_buffer.buffer);
            parse_buffer();
        }

        sleep(2);

    }
    //unlink(TTY_GPS_DEV);
    close(fd);
}

int main()
{
	printf("hello world!\n");


	thread(simulate_gps);

	data_simulation();

	return 0;
}
