﻿//
// Copyright (c) 2019-2022 yanggaofeng
//
#ifndef YANGSOCKETUTIL_H_
#define YANGSOCKETUTIL_H_
#include <stdint.h>
#include <yangutil/sys/YangVector.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>
#define socklen_t int
#define GetSockError()	WSAGetLastError()
#define SetSockError(e)	WSASetLastError(e)
#define setsockopt(a,b,c,d,e)	(setsockopt)(a,b,c,(const char *)d,(int)e)
#define Yang_No_Signal SO_NOSIGPIPE
#define yang_poll WSAPoll
#else
#include <netinet/in.h>
#include <errno.h>
#define yang_poll poll
#define GetSockError()	errno
#define Yang_No_Signal MSG_NOSIGNAL
#define SetSockError(e)	errno = e
#endif

#define yang_socket_t int
typedef struct sockaddr_in yang_socket_addr4;
typedef struct sockaddr_in6 yang_socket_addr6;


typedef struct {
    int32_t port;
    uint32_t mapAddress;
    YangIpFamilyType familyType;
    YangSocketProtocol protocol;

    uint8_t  address[16];
    yang_socket_addr4 addr4;
    yang_socket_addr6 addr6;

} YangIpAddress;

#ifdef __cplusplus
extern "C"{
#endif

void yang_addr_set(YangIpAddress* addr,char* ip,int32_t port,YangIpFamilyType familyType, YangSocketProtocol protocol);
void yang_addr_setAnyAddr(YangIpAddress* addr,int32_t port,YangIpFamilyType familyType, YangSocketProtocol protocol);

void yang_addr_setIPV4(YangIpAddress* addr,int32_t ip,int32_t port,YangSocketProtocol protocol);
void yang_addr_setIPV6(YangIpAddress* addr,uint8_t ip[16],int32_t port, YangSocketProtocol protocol);
uint32_t yang_addr_getIP(YangIpAddress* addr);
void yang_addr_getIPStr(YangIpAddress* addr,char* addrstr,int32_t strLen);
uint16_t yang_addr_getPort(YangIpAddress* addr);
uint16_t yang_addr_getSinPort(YangIpAddress* addr);
yang_socket_t yang_socket_create(YangIpFamilyType familyType, YangSocketProtocol protocol);
int32_t yang_socket_close(yang_socket_t fd);

int32_t yang_socket_listen(yang_socket_t fd,YangIpAddress* addr);

int32_t yang_socket_connect(yang_socket_t fd,YangIpAddress* remoteAddr);
int32_t yang_socket_recvfrom(yang_socket_t fd,char* buffer,int32_t bufferLen,YangIpAddress* addr);

int32_t yang_socket_accept(yang_socket_t fd,YangIpAddress* addr);

int32_t yang_socket_sendto(yang_socket_t fd,char* data,int32_t nb,YangIpAddress* remote_addr,int32_t flag);
int32_t yang_socket_send(yang_socket_t fd,char* data,int32_t nb,int32_t flag);
int32_t yang_socket_recv(yang_socket_t fd,char* data,int32_t nb,int32_t flag);


int32_t yang_getLocalInfo(char* p);
int32_t yang_getLocalInfoList(YangStringVector* p);
void yang_getIp( char* domain, char* ip);

#ifdef __cplusplus
}
#endif
#endif /* YANGSOCKETUTIL_H_ */
