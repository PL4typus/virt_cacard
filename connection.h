#ifndef _CONNECTION_H
#define _CONNECTION_H
#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>

#define SOCKET int
#define closesocket(x) close(x)
#define VPCDPORT 35963
#define INVALID_SOCKET -1

SOCKET opensock(uint16_t port);
SOCKET connectsock(const char *hostname, uint16_t port);

ssize_t sendall(SOCKET sock, const void *buffer, size_t size);
ssize_t recvall(SOCKET sock, void *buffer, size_t size);

#endif

/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
