
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/route.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <pthread.h>

#include "net.h"

#include "common.h"

dns_t g_dns; /* global var */

int net_getpeername(st_netfd_t fd, struct sockaddr* name, int* namelen) {
  socklen_t socklen;
  socklen = (socklen_t) *namelen;
  if (getpeername(st_netfd_fileno(fd), name, &socklen))
    return -errno;
  *namelen = (int) socklen;
  return 0;
}

int net_getsockname(const st_netfd_t fd, struct sockaddr* name, int* namelen) {
  socklen_t socklen;
  /* sizeof(socklen_t) != sizeof(int) on some systems. */
  socklen = (socklen_t) *namelen;
  if (getsockname(st_netfd_fileno(fd), name, &socklen))
    return -errno;
  *namelen = (int) socklen;
  return 0;
}

int ip4_addr(const char* ip, int port, struct sockaddr_in* addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    return inet_pton(AF_INET, ip, &(addr->sin_addr.s_addr));
}

bool_t is_ip(const char* addr)
{
    struct sockaddr_in adr_inet; /* AF_INET */
    if (inet_pton(AF_INET, addr, &adr_inet.sin_addr) > 0)
    {
        return true;
    }
    return false;
}

int net_tcp_nodelay(int fd, int on)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)))
        return -errno;
    return 0;
}

int net_tcp_keepalive(int fd, int on, unsigned int delay)
{
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)))
        return -errno;

#ifdef TCP_KEEPIDLE
    if (on && setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &delay, sizeof(delay)))
    return -errno;
#endif

    /* Solaris/SmartOS, if you don't support keep-alive,
     * then don't advertise it in your system headers...
     */
    /* FIXME(bnoordhuis) That's possibly because sizeof(delay) should be 1. */
#if defined(TCP_KEEPALIVE) && !defined(__sun)
    if (on && setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &delay, sizeof(delay)))
    return -errno;
#endif

    return 0;
}

st_netfd_t net_connect_addr(struct sockaddr *addr,st_utime_t to){
    st_netfd_t fd=NULL;
    int s=-1;
    int addrlen=0;

    if (addr->sa_family == AF_INET)
    {
        s = socket(PF_INET, SOCK_STREAM, 0);
        addrlen = sizeof(struct sockaddr_in);
    }
    else
    {
        s = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
        addrlen = sizeof(struct sockaddr_in6);
    }
    if(s<0){
        return NULL;
    }
    fcntl(s, F_SETFD, FD_CLOEXEC); /* close socket when exec */
    fd = st_netfd_open_socket(s);
    if(fd == NULL){
        return NULL;
    }
    if(st_connect(fd, addr, addrlen, to)==-1){
        log_err("st_connect error:%s\n",strerror(errno));
        st_netfd_close(fd);
        return NULL;
    }
    return fd;
}

pthread_mutex_t pthread_lock_initializer()
{
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    return lock;
}

pthread_t thread_create(void * (*start)(void *arg), void *arg,int joinable, int stack_size)
{
    pthread_t thread;

    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setscope(&thread_attr, PTHREAD_SCOPE_SYSTEM);

    size_t stk_size = 8192;
    int ret = pthread_attr_getstacksize(&thread_attr, &stk_size);
    if(ret);/* without warning */
    if(stack_size > 0)
    {
        stk_size = stack_size;
        int page_size = getpagesize();
        stk_size = ((stk_size + page_size - 1) / page_size) * page_size;

        ret = pthread_attr_setstacksize(&thread_attr, stk_size);
    }

    if(pthread_create(&thread, NULL, start, arg)==0 && joinable==0){
        pthread_detach(thread);
    }

    pthread_attr_destroy(&thread_attr);
    return thread;
}

static void *dns_routine(void *arg)
{
    dns_t * pdns = (dns_t *)arg;
    prctl(PR_SET_NAME,"dns_routine");
    int valid = 1;
    while(valid)
    {
        pthread_mutex_lock(&pdns->lock);
        valid = pdns->valid;
        pthread_mutex_unlock(&pdns->lock);
        
        if(!valid)
            break;
        
        int i = 0;
        for(;i<8;i++)
        {
            pthread_mutex_lock(&pdns->lock);
            if(pdns->dns_list[i].used == 1 && pdns->dns_list[i].finished == 0)
            {
                struct addrinfo hint;
                memset (&hint, 0,sizeof(hint));
                hint.ai_family = AF_INET;
                hint.ai_socktype = SOCK_STREAM;
                hint.ai_protocol = IPPROTO_TCP;

                if(pdns->dns_list[i].answer) /* free the last one if exist */
                {
                    freeaddrinfo(pdns->dns_list[i].answer);
                    pdns->dns_list[i].answer = NULL;
                }
                getaddrinfo(pdns->dns_list[i].host, NULL, &hint, 
                    &pdns->dns_list[i].answer);
                pdns->dns_list[i].finished = 1;
            }
            pthread_mutex_unlock(&pdns->lock);
        }
        usleep(500*1000);
    }
    return NULL;
}

static void dns_client_init(dns_t *pdns)
{
    pdns->valid = 1;
    memset(pdns->dns_list, 0, sizeof(dns_parm_t)*8);
    pdns->lock = pthread_lock_initializer();
    pdns->dns_tid = thread_create(dns_routine, &g_dns, 1, THREAD_STACK_SIZE_K(128)); /* Joinable */
}

static void dns_client_stop(dns_t *pdns)
{
    void *value = NULL;
    pthread_mutex_lock(&pdns->lock);
    pdns->valid = 0;
    pthread_t tid = pdns->dns_tid;
    pthread_mutex_unlock(&pdns->lock);
    pthread_join(tid, &value);
}

static int push_dns_task(dns_t *pdns, const char *host)
{
    int i = 0;
    int index = 0;
    for(;i<8;i++)
    {
        pthread_mutex_lock(&pdns->lock);
        int cond = (pdns->dns_list[i].used == 0);
        pthread_mutex_unlock(&pdns->lock);
        if(cond)
        {
            pthread_mutex_lock(&pdns->lock);
            snprintf(pdns->dns_list[i].host, sizeof(pdns->dns_list[i].host), host);
            if(pdns->dns_list[i].answer)
            {
                freeaddrinfo(pdns->dns_list[i].answer);
                pdns->dns_list[i].answer = NULL;
            }
            pdns->dns_list[i].used = 1;
            pdns->dns_list[i].finished = 0;
            pthread_mutex_unlock(&pdns->lock);
            
            index = i;
            break;
        }
    }
    return index;
}

static struct addrinfo *get_dns_result(dns_t *pdns, int index, st_utime_t to)
{
    int i = 0;
    struct addrinfo * answer = NULL;
    for(;i<8;i++)
    {
        pthread_mutex_lock(&pdns->lock);
        int bingo = (pdns->dns_list[i].used == 1 && i == index);
        pthread_mutex_unlock(&pdns->lock);
        if(bingo)
        {
            st_utime_t start = st_time();
            while(1)
            {
                pthread_mutex_lock(&pdns->lock);
                int finished = pdns->dns_list[i].finished;
                pthread_mutex_unlock(&pdns->lock);
                st_utime_t timeout = abs(st_time() - start);
                if(finished == 0 && timeout < to)
                    usleep(500*100);
                else
                    break;
            }
            pthread_mutex_lock(&pdns->lock);
            answer = pdns->dns_list[i].answer;
            pthread_mutex_unlock(&pdns->lock);
            break;
        }
    }
    return answer;
}

static void free_dns(dns_t *pdns, int index)
{
    int i = 0;
    for(;i<8;i++)
    {
        pthread_mutex_lock(&pdns->lock);
        if(pdns->dns_list[i].used == 1 && i == index)
        {
            memset(pdns->dns_list[i].host, 0x0, sizeof(pdns->dns_list[i].host));
            if(pdns->dns_list[i].answer)
            {
                freeaddrinfo(pdns->dns_list[i].answer);
                pdns->dns_list[i].answer = NULL;
            }
            pdns->dns_list[i].used = 0;
            pdns->dns_list[i].finished = 0;
        }
        pthread_mutex_unlock(&pdns->lock);
    }
}

st_netfd_t st_getaddrinfo_connect(dns_parm_t *pdns_parm, short port, st_utime_t to)
{
    st_netfd_t fd = NULL;
    struct addrinfo *cur=NULL;
    
    int index = push_dns_task(&g_dns, pdns_parm->host);
    struct addrinfo *answer = get_dns_result(&g_dns, index, to);

    if(answer == NULL){
        log_err("dns %s fail\n", pdns_parm->host);
        free_dns(&g_dns, index);
        return NULL;
    }
    for(cur = answer; cur != NULL; cur = cur->ai_next)
    {
        if(cur->ai_addr->sa_family  ==AF_INET){
            ((struct sockaddr_in *)(cur->ai_addr))->sin_port = htons(port);
        }else{
            ((struct sockaddr_in6 *)(cur->ai_addr))->sin6_port = htons(port);
        }
        fd = net_connect_addr(cur->ai_addr,to);
        if(fd !=NULL){
            break;
        }
    }
    free_dns(&g_dns, index);
    return fd;
}

st_netfd_t net_connect(const char *host,short port,st_utime_t to){
    st_netfd_t fd=NULL;
    
    dns_parm_t dns_parm = { 0 };
    snprintf(dns_parm.host, sizeof(dns_parm.host), host);
    dns_parm.answer = NULL;
    
    struct sockaddr_in addr4 = { 0 };

    if(is_ip(host)==true){
        inet_pton(AF_INET,host,&addr4.sin_addr);
        addr4.sin_family = AF_INET;
        addr4.sin_port   = htons (port);
        return net_connect_addr((struct sockaddr *)&addr4,to);
    }else{
        return st_getaddrinfo_connect(&dns_parm, port, to);
    }

    return fd;
}

static void ifc_init_ifr(const char *name, struct ifreq *ifr)
{
    memset(ifr, 0, sizeof(struct ifreq));
    strncpy(ifr->ifr_name, name, IFNAMSIZ);
    ifr->ifr_name[IFNAMSIZ - 1] = 0;
}

int ifc_get_hwaddr(const char *name, void *ptr)
{
    int r;
    struct ifreq ifr;
    int ifc_ctl_sock=0;
    ifc_ctl_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(ifc_ctl_sock<0){
        return -1;
    }
    ifc_init_ifr(name, &ifr);

    r = ioctl(ifc_ctl_sock, SIOCGIFHWADDR, &ifr);
    close(ifc_ctl_sock);
    if(r < 0){
        return -1;
    }

    memcpy(ptr, &ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    return 0;
}

int ifc_get_addr(const char *name, in_addr_t *addr)
{
    struct ifreq ifr;
    int ret = 0;
    int ifc_ctl_sock=0;
    ifc_ctl_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(ifc_ctl_sock<0){
        return -1;
    }

    ifc_init_ifr(name, &ifr);
    if (addr != NULL) {
        ret = ioctl(ifc_ctl_sock, SIOCGIFADDR, &ifr);
        if (ret < 0) {
            *addr = 0;
        } else {
            *addr = ((struct sockaddr_in*) &ifr.ifr_addr)->sin_addr.s_addr;
        }
    }
    close(ifc_ctl_sock);
    return ret;
}

int ifc_get_default_route(char *ifname)
{
    char name[64];
    char line[256];
    in_addr_t dest, gway, mask;
    int flags, refcnt, use, metric, mtu, win, irtt;
    FILE *fp;
    fp = fopen("/proc/net/route", "r");
    if (fp == NULL){
        return -1;
    }
    /* Skip the header line */
    if (fscanf(fp, "%*[^\n]\n") < 0) {
        fclose(fp);
        return -1;
    }
    for (;;) {
        int nread;
        if(fgets(line,256,fp)==NULL){
            break;
        }
        nread = sscanf(line,"%63s%X%X%X%d%d%d%X%d%d%d\n",
            name, &dest, &gway, &flags, &refcnt, &use, &metric, &mask,
            &mtu, &win, &irtt);

        if (nread != 11) {
            break;
        }

        if ((flags & (RTF_UP|RTF_GATEWAY)) == (RTF_UP|RTF_GATEWAY)
                && dest == 0 && gway!=0) {
            strcpy(ifname,name);
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return -1;
}

netcard_t * ifc_get_cardinfo(netcard_t *cardinfo)
{
    if(cardinfo)
    {
        in_addr_t addr = { 0 };
        uint8_t hwaddr[6] = { 0 };
        if(ifc_get_default_route(cardinfo->ifname)){
            strcpy(cardinfo->ifname, "eth0");
        }

        ifc_get_hwaddr(cardinfo->ifname, hwaddr);
        
        sprintf(cardinfo->mac, "%02x%02x%02x%02x%02x%02x",
            hwaddr[0],hwaddr[1],hwaddr[2],hwaddr[3],hwaddr[4],hwaddr[5]);
        if(ifc_get_addr(cardinfo->ifname,&addr)==0){
            inet_ntop(AF_INET, &addr, cardinfo->local_ip, 64);
        }
    }
    return cardinfo;
}

uint64_t ntohl64(uint64_t host)
{
    uint64_t ret = 0;
    uint32_t high = 0;
    uint32_t low = 0;

    low = host & 0xFFFFFFFF;
    high = (host >> 32) & 0xFFFFFFFF;
    low = ntohl(low);
    high = ntohl(high);
    ret = low;
    ret <<= 32;
    ret |= high;

    return ret;
}

uint64_t hl64ton(uint64_t host)
{
    uint64_t ret = 0;
    uint32_t high = 0;
    uint32_t low = 0;

    low = host & 0xFFFFFFFF;
    high = (host >> 32) & 0xFFFFFFFF;
    low = htonl(low);
    high = htonl(high);
    ret = low;
    ret <<= 32;
    ret |= high;

    return ret;
}

void net_dns_start()
{
    dns_client_init(&g_dns);
}

void net_dns_stop()
{
    dns_client_stop(&g_dns);
}

