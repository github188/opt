#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include "stt.h"


#if EAGAIN != EWOULDBLOCK
#define _IO_NOT_READY_ERROR  ((errno == EAGAIN) || (errno == EWOULDBLOCK))
#else
#define _IO_NOT_READY_ERROR  (errno == EAGAIN)
#endif

#define _LOCAL_MAXIOV  16

#define COLOR_N         "\033[m"
#define COLOR_R         "\033[0;32;31m"
#define INFO_SHOW(fmt, args...) do{\
    printf(COLOR_R"[HMPU_CSDK INFOMATION]# "COLOR_N);\
    printf(fmt, ##args); \
}while(0)

int st_pthread_version(void)
{
    return 1;
}

int st_init(void)
{
    INFO_SHOW("thread module pthread\n");
    return 0;
}

int __st_name(char *name)
{
    return prctl(PR_SET_NAME,(name));
}

int st_free(void)
{
    INFO_SHOW("thread module pthread free\n");
    return 0;
}

int st_getfdlimit(void)
{
    return 1024;
}

int st_set_eventsys(int eventsys)
{
    return 0;
}

int st_get_eventsys(void)
{
    return 0;
}

const char *st_get_eventsys_name(void)
{
    return "epoll";
}


st_thread_t st_thread_self(void)
{
    return pthread_self();
}

void st_thread_exit(void *retval)
{
    return;
}

void st_thread_exit_init(void *retval)
{
    st_thread_exit(retval);
}

int st_thread_join(st_thread_t thread, void **retvalp)
{
    return pthread_join(thread, retvalp);
}

void st_thread_interrupt(st_thread_t thread)
{
    return ;
}

st_thread_t st_thread_create(void * (*start)(void *arg), void *arg,
                             int joinable, int stack_size)
{
    pthread_t thread;
    
    if(pthread_create(&thread, NULL, start, arg)==0 && joinable==0){
        pthread_detach(thread);
    }
    
    
    return thread;
}

int st_randomize_stacks(int on)
{
    return 0;
}

int st_set_utime_function(st_utime_t (*func)(void))
{
    return 0;
}

st_utime_t st_utime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec*1000000LL+tv.tv_usec);
}

st_utime_t st_utime_last_clock(void)
{
    return 0;
}

int st_timecache_set(int on)
{
    return 0;
}

time_t st_time(void)
{
    return 0;
}
int st_usleep(st_utime_t usecs)
{
    usleep(usecs);
    return 0;
}
int st_sleep(int secs)
{
    sleep(secs);
    return 0;
}
st_cond_t st_cond_new(void)
{
    return 0;
}
int st_cond_destroy(st_cond_t cvar)
{
    return 0;
}
int st_cond_timedwait(st_cond_t cvar, st_utime_t timeout)
{
    return 0;
}
int st_cond_wait(st_cond_t cvar)
{
    return 0;
}
int st_cond_signal(st_cond_t cvar)
{
    return 0;
}
int st_cond_broadcast(st_cond_t cvar)
{
    return 0;
}



st_mutex_t st_mutex_new(void)
{
    return 0;
}
int st_mutex_destroy(st_mutex_t lock)
{
    return 0;
}
int st_mutex_lock(st_mutex_t lock)
{
    return 0;
}
int st_mutex_unlock(st_mutex_t lock)
{
    return 0;
}
int st_mutex_trylock(st_mutex_t lock)
{
    return 0;
}

extern int st_key_create(int *keyp, void (*destructor)(void *))
{
    return 0;
}
extern int st_key_getlimit(void)
{
    return 0;
}
extern int st_thread_setspecific(int key, void *value)
{
    return 0;
}
extern void *st_thread_getspecific(int key)
{
    return 0;
}

struct _st_netfd
{
    int osfd;
};

st_netfd_t _st_netfd_new(int osfd, int nonblock, int is_socket)
{
    st_netfd_t fd;
    int flags = 1;


    fd = (st_netfd_t)calloc(1, sizeof(struct _st_netfd));
    if (!fd)
        return NULL;

    fd->osfd = osfd;

    if (nonblock)
    {
        /* Use just one system call */
        if (is_socket && ioctl(osfd, FIONBIO, &flags) != -1)
            return fd;
        /* Do it the Posix way */
        if ((flags = fcntl(osfd, F_GETFL, 0)) < 0 ||
                fcntl(osfd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            st_netfd_free(fd);
            return NULL;
        }
    }

    return fd;
}

st_netfd_t st_netfd_open(int osfd)
{
    return _st_netfd_new(osfd, 1, 0);
}


st_netfd_t st_netfd_open_socket(int osfd)
{
    return _st_netfd_new(osfd, 1, 1);
}

void st_netfd_free(st_netfd_t fd)
{
    free(fd);
}

int st_netfd_close(st_netfd_t fd)
{
    close(fd->osfd);
    st_netfd_free(fd);
    return 0;
}

int st_netfd_fileno(st_netfd_t fd)
{
    return fd->osfd;
}

void st_netfd_setspecific(st_netfd_t fd, void *value,
                          void (*destructor)(void *))
{
    return;
}
void *st_netfd_getspecific(st_netfd_t fd)
{
    return NULL;
}
int st_netfd_serialize_accept(st_netfd_t fd)
{
    return 0;
}

int st_netfd_poll(st_netfd_t fd, int how, st_utime_t timeout)
{
    struct pollfd pd;
    int n;

    pd.fd = fd->osfd;
    pd.events = (short) how;
    pd.revents = 0;

    if ((n = st_poll(&pd, 1, timeout)) < 0){
        return -1;
    }
    if (n == 0) {
        /* Timed out */
        errno = ETIME;
        return -1;
    }
    if (pd.revents & POLLNVAL) {
        errno = EBADF;
        return -1;
    }

    return 0;
}

int st_poll(struct pollfd *pds, int npds, st_utime_t timeout)
{
    if(timeout == ST_UTIME_NO_TIMEOUT)
    {
        return poll(pds, npds, -1);
    }
    return poll(pds, npds, timeout / 1000);
}

extern st_netfd_t st_accept(st_netfd_t fd, struct sockaddr *addr, int *addrlen,
                            st_utime_t timeout)
{

    int osfd, err;
    struct _st_netfd *newfd;

    while ((osfd = accept(fd->osfd, addr, (socklen_t *)addrlen)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return NULL;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return NULL;
    }

    /* On some platforms the new socket created by accept() inherits */
    /* the nonblocking attribute of the listening socket */

    newfd = _st_netfd_new(osfd, 1, 1);

    if (!newfd)
    {
        err = errno;
        close(osfd);
        errno = err;
    }

    return newfd;

}
extern int st_connect(st_netfd_t fd, const struct sockaddr *addr, int addrlen,
                      st_utime_t timeout)
{
    int n, err = 0;

    while (connect(fd->osfd, addr, addrlen) < 0)
    {
        if (errno != EINTR)
        {
            /*
             * On some platforms, if connect() is interrupted (errno == EINTR)
             * after the kernel binds the socket, a subsequent connect()
             * attempt will fail with errno == EADDRINUSE.  Ignore EADDRINUSE
             * iff connect() was previously interrupted.  See Rich Stevens'
             * "UNIX Network Programming," Vol. 1, 2nd edition, p. 413
             * ("Interrupted connect").
             */
            if (errno != EINPROGRESS && (errno != EADDRINUSE || err == 0))
                return -1;
            /* Wait until the socket becomes writable */
            if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
                return -1;
            /* Try to find out whether the connection setup succeeded or failed */
            n = sizeof(int);
            if (getsockopt(fd->osfd, SOL_SOCKET, SO_ERROR, (char *)&err,
                           (socklen_t *)&n) < 0)
                return -1;
            if (err)
            {
                errno = err;
                return -1;
            }
            break;
        }
        err = 1;
    }

    return 0;
}
ssize_t st_read(struct _st_netfd *fd, void *buf, size_t nbyte, st_utime_t timeout)
{
    ssize_t n;

    while ((n = read(fd->osfd, buf, nbyte)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }

    return n;
}


int st_read_resid(struct _st_netfd *fd, void *buf, size_t *resid,
                  st_utime_t timeout)
{
    struct iovec iov, *riov;
    int riov_size, rv;

    iov.iov_base = buf;
    iov.iov_len = *resid;
    riov = &iov;
    riov_size = 1;
    rv = st_readv_resid(fd, &riov, &riov_size, timeout);
    *resid = iov.iov_len;
    return rv;
}


ssize_t st_readv(struct _st_netfd *fd, const struct iovec *iov, int iov_size,
                 st_utime_t timeout)
{
    ssize_t n;

    while ((n = readv(fd->osfd, iov, iov_size)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }

    return n;
}

int st_readv_resid(struct _st_netfd *fd, struct iovec **iov, int *iov_size,
                   st_utime_t timeout)
{
    ssize_t n;

    while (*iov_size > 0)
    {
        if (*iov_size == 1)
            n = read(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
        else
            n = readv(fd->osfd, *iov, *iov_size);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (!_IO_NOT_READY_ERROR)
                return -1;
        }
        else if (n == 0){
            //return -1;
            break;
        }
        else
        {
            while ((size_t) n >= (*iov)->iov_len)
            {
                n -= (*iov)->iov_len;
                (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
                (*iov)->iov_len = 0;
                (*iov)++;
                (*iov_size)--;
                if (n == 0)
                    break;
            }
            if (*iov_size == 0)
                break;
            (*iov)->iov_base = (char *) (*iov)->iov_base + n;
            (*iov)->iov_len -= n;
        }
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }

    return 0;
}


ssize_t st_read_fully(struct _st_netfd *fd, void *buf, size_t nbyte,
                      st_utime_t timeout)
{
    size_t resid = nbyte;
    return st_read_resid(fd, buf, &resid, timeout) == 0 ?
           (ssize_t) (nbyte - resid) : -1;
}


int st_write_resid(struct _st_netfd *fd, const void *buf, size_t *resid,
                   st_utime_t timeout)
{
    struct iovec iov, *riov;
    int riov_size, rv;

    iov.iov_base = (void *) buf;        /* we promise not to modify buf */
    iov.iov_len = *resid;
    riov = &iov;
    riov_size = 1;
    rv = st_writev_resid(fd, &riov, &riov_size, timeout);
    *resid = iov.iov_len;
    return rv;
}


ssize_t st_write(struct _st_netfd *fd, const void *buf, size_t nbyte,
                 st_utime_t timeout)
{
    size_t resid = nbyte;
    return st_write_resid(fd, buf, &resid, timeout) == 0 ?
           (ssize_t) (nbyte - resid) : -1;
}


ssize_t st_writev(struct _st_netfd *fd, const struct iovec *iov, int iov_size,
                  st_utime_t timeout)
{
    ssize_t n, rv;
    size_t nleft, nbyte;
    int index, iov_cnt;
    struct iovec *tmp_iov;
    struct iovec local_iov[_LOCAL_MAXIOV];

    /* Calculate the total number of bytes to be sent */
    nbyte = 0;
    for (index = 0; index < iov_size; index++)
        nbyte += iov[index].iov_len;

    rv = (ssize_t)nbyte;
    nleft = nbyte;
    tmp_iov = (struct iovec *) iov; /* we promise not to modify iov */
    iov_cnt = iov_size;

    while (nleft > 0)
    {
        if (iov_cnt == 1)
        {
            if (st_write(fd, tmp_iov[0].iov_base, nleft, timeout) != (ssize_t) nleft)
                rv = -1;
            break;
        }
        n = writev(fd->osfd, tmp_iov, iov_cnt);
        //printf(">>>>>>>>>>>>>>>>>>>>=============%d\n",n);
        if (n < 0)
        {
            //printf(">>>>>>>>>>>>>>>>>>>>================writev error===============:%s\n",strerror(errno));
            if (errno == EINTR){
                //printf(">>>>>>>>>>>>>>>>>======EINTR=======writev error===============:%s\n",strerror(errno));
                continue;
            }
            if (!_IO_NOT_READY_ERROR)
            {
                //printf(">>>>>>>>>>>>>>>======_IO_NOT_READY_ERROR=======writev error===============:%s\n",strerror(errno));
                rv = -1;
                break;
            }
        }
        else
        {
            if ((size_t) n == nleft)
                break;
            nleft -= n;
            /* Find the next unwritten vector */
            n = (ssize_t)(nbyte - nleft);
            for (index = 0; (size_t) n >= iov[index].iov_len; index++)
                n -= iov[index].iov_len;

            if (tmp_iov == iov)
            {
                /* Must copy iov's around */
                if (iov_size - index <= _LOCAL_MAXIOV)
                {
                    tmp_iov = local_iov;
                }
                else
                {
                    tmp_iov = (struct iovec *)calloc(1, (iov_size - index) * sizeof(struct iovec));
                    if (tmp_iov == NULL)
                        return -1;
                }
            }

            /* Fill in the first partial read */
            tmp_iov[0].iov_base = &(((char *)iov[index].iov_base)[n]);
            tmp_iov[0].iov_len = iov[index].iov_len - n;
            index++;
            /* Copy the remaining vectors */
            for (iov_cnt = 1; index < iov_size; iov_cnt++, index++)
            {
                tmp_iov[iov_cnt].iov_base = iov[index].iov_base;
                tmp_iov[iov_cnt].iov_len = iov[index].iov_len;
            }
        }
        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
        {
            rv = -1;
            break;
        }
    }

    if (tmp_iov != iov && tmp_iov != local_iov)
        free(tmp_iov);

    return rv;
}


int st_writev_resid(struct _st_netfd *fd, struct iovec **iov, int *iov_size,
                    st_utime_t timeout)
{
    ssize_t n;

    while (*iov_size > 0)
    {
        if (*iov_size == 1)
            n = write(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
        else
            n = writev(fd->osfd, *iov, *iov_size);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (!_IO_NOT_READY_ERROR)
                return -1;
        }
        else
        {
            while ((size_t) n >= (*iov)->iov_len)
            {
                n -= (*iov)->iov_len;
                (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
                (*iov)->iov_len = 0;
                (*iov)++;
                (*iov_size)--;
                if (n == 0)
                    break;
            }
            if (*iov_size == 0)
                break;
            (*iov)->iov_base = (char *) (*iov)->iov_base + n;
            (*iov)->iov_len -= n;
        }
        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }

    return 0;
}


/*
 * Simple I/O functions for UDP.
 */
int st_recvfrom(struct _st_netfd *fd, void *buf, int len, struct sockaddr *from,
                int *fromlen, st_utime_t timeout)
{
    int n;

    while ((n = recvfrom(fd->osfd, buf, len, 0, from, (socklen_t *)fromlen))
            < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }

    return n;
}


int st_sendto(struct _st_netfd *fd, const void *msg, int len,
              const struct sockaddr *to, int tolen, st_utime_t timeout)
{
    int n;

    while ((n = sendto(fd->osfd, msg, len, 0, to, tolen)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }

    return n;
}


int st_recvmsg(struct _st_netfd *fd, struct msghdr *msg, int flags,
               st_utime_t timeout)
{
    int n;

    while ((n = recvmsg(fd->osfd, msg, flags)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }

    return n;
}


int st_sendmsg(struct _st_netfd *fd, const struct msghdr *msg, int flags,
               st_utime_t timeout)
{
    int n;

    while ((n = sendmsg(fd->osfd, msg, flags)) < 0)
    {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;
        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }

    return n;
}



/*
 * To open FIFOs or other special files.
 */
struct _st_netfd *st_open(const char *path, int oflags, mode_t mode)
{
    int osfd, err;
    struct _st_netfd *newfd;

    while ((osfd = open(path, oflags | O_NONBLOCK, mode)) < 0)
    {
        if (errno != EINTR)
            return NULL;
    }

    newfd = _st_netfd_new(osfd, 0, 0);
    if (!newfd)
    {
        err = errno;
        close(osfd);
        errno = err;
    }

    return newfd;
}
