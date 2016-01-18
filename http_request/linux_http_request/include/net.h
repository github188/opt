
#ifndef __UITLS_NET_H__
#define __UITLS_NET_H__
#include "stt.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

typedef struct dns_parm{
    char *host;
    struct addrinfo *answer;
}dns_parm_t;
int net_getpeername(st_netfd_t fd,
                       struct sockaddr* name,
                       int* namelen);
st_netfd_t net_connect(char *host,short port,st_utime_t to);
int ip4_addr(const char* ip, int port, struct sockaddr_in* addr);
uint8_t is_ip(const char* addr);
struct addrinfo *st_getaddrinfo(char *url);
void getaddrinfo_work(void *data);
int ifc_get_addr(const char *name, in_addr_t *addr);
int ifc_get_default_route(char *ifname);
int ifc_get_hwaddr(const char *name, void *ptr);

void alm_hash(char *string, char *md5str);
#endif

