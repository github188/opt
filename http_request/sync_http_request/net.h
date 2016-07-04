
#ifndef __UITLS_NET_H__
#define __UITLS_NET_H__
#include "stt.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define bool_t int
#define true 1
#define false 0

typedef struct dns_parm{
    int used;
    int finished;
    char host[252];
    struct addrinfo *answer; 
} dns_parm_t;

typedef struct dns_s /* DNS module use unique pthread */
{
    int valid; /* DNS running */
    dns_parm_t dns_list[8]; /* supported dns parser list */
    pthread_t dns_tid;
    pthread_mutex_t lock;
} dns_t;

typedef struct netcard_s
{
    char local_ip[64];
    char mac[32];
    char ifname[32];
} netcard_t;

int net_getpeername(st_netfd_t fd, struct sockaddr* name, int* namelen);
int net_tcp_nodelay(int fd, int on);
int net_tcp_keepalive(int fd, int on, unsigned int delay);
st_netfd_t net_connect(const char *host,short port,st_utime_t to);
int ip4_addr(const char* ip, int port, struct sockaddr_in* addr);
bool_t is_ip(const char* addr);

int ifc_get_addr(const char *name, in_addr_t *addr);
int ifc_get_default_route(char *ifname);
int ifc_get_hwaddr(const char *name, void *ptr);

uint64_t ntohl64(uint64_t host);
uint64_t hl64ton(uint64_t host);
               
void net_dns_start();
void net_dns_stop();
#endif

