
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>

#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/route.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "net.h"

int net_getpeername(st_netfd_t fd,
                       struct sockaddr* name,
                       int* namelen) {
  socklen_t socklen;

  socklen = (socklen_t) *namelen;

  if (getpeername(st_netfd_fileno(fd), name, &socklen))
    return -errno;

  *namelen = (int) socklen;
  return 0;
}

int net_getsockname(const st_netfd_t fd,
                       struct sockaddr* name,
                       int* namelen) {
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

void getaddrinfo_work(void *data){
    char host[128];
    struct addrinfo hint;
    dns_parm_t *dns_parm=(dns_parm_t *)data;
    dns_parm->answer = NULL;
    memset (&hint, 0,sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    snprintf(host,128,"%s",dns_parm->host);
    getaddrinfo(host, NULL, &hint, &dns_parm->answer);
 }

struct addrinfo *st_getaddrinfo(char *url){
    dns_parm_t dns_parm;
    dns_parm.host=url;
    dns_parm.answer=NULL;
    getaddrinfo_work(&dns_parm);

    return dns_parm.answer;
}


uint8_t is_ip(const char* addr)
{

    struct sockaddr_in adr_inet; /* AF_INET */
    if (inet_pton(AF_INET, addr, &adr_inet.sin_addr) > 0)
    {
        return true;
    }
    return false;
}

st_netfd_t net_connect_addr(struct sockaddr *addr,st_utime_t to){
    st_netfd_t fd=NULL;
    int s=-1;
    int addrlen=0;

    if (addr->sa_family == AF_INET)
    {
        char ip[64];
        struct sockaddr_in *addrin=(struct sockaddr_in *)addr;
        s = socket(PF_INET, SOCK_STREAM, 0);
        addrlen = sizeof(struct sockaddr_in);
#ifdef __x86_x64__
        printf("connect to:%s %d %lu\n",inet_ntop(AF_INET, &addrin->sin_addr,ip, 64),ntohs(addrin->sin_port),to);
#else
        printf("connect to:%s %d %llu\n",inet_ntop(AF_INET, &addrin->sin_addr,ip, 64),ntohs(addrin->sin_port),to);
#endif
    }
    else
    {

        s = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
        addrlen = sizeof(struct sockaddr_in6);

    }
    if(s<0){
        return NULL;
    }
    fd = st_netfd_open_socket(s);
    if(fd == NULL){
        return NULL;
    }
    if(st_connect( fd, addr, addrlen, to)==-1){
        printf("st_connect error  :%s\n",strerror(errno));
        st_netfd_close(fd);
        return NULL;
    }
    return fd;
}

st_netfd_t net_connect(char *host,short port,st_utime_t to){
    st_netfd_t fd=NULL;
    struct addrinfo *answer=NULL,*cur=NULL;
    struct sockaddr_in addr4;
    if(is_ip(host)==true){
        inet_pton(AF_INET,host,&addr4.sin_addr);
        addr4.sin_family = AF_INET;
        addr4.sin_port   = htons (port);
        return net_connect_addr((struct sockaddr *)&addr4,to);
    }else{
        answer=st_getaddrinfo(host);
        if(answer==NULL){
            return NULL;
        }
        for(cur = answer; cur != NULL; cur = cur->ai_next)
        {
            if(cur->ai_addr->sa_family  ==AF_INET){
                ((struct sockaddr_in *)(cur->ai_addr))->sin_port = htons(port);
            }else{
                ((struct sockaddr_in6 *)(cur->ai_addr))->sin6_port = htons(port);
            }
            fd= net_connect_addr( cur->ai_addr,to);
            if(fd !=NULL){
                break;
            }
        }
    }

    if(answer)freeaddrinfo(answer);
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
