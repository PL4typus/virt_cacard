#ifndef _CONNECTION_H
#define _CONNECTION_H
#include <unistd.h>

#include <stdint.h>
#include <stdlib.h>
#include <glib.h>
#define SOCKET int
#define closesocket(x) close(x)
#define VPCDPORT 35963
#define INVALID_SOCKET -1
#define VPCD_CTRL_LEN 	1
#define VPCD_CTRL_OFF   0
#define VPCD_CTRL_ON    1
#define VPCD_CTRL_RESET 2
#define VPCD_CTRL_ATR	4
#define APDUBufSize 270
#define MAX_ATR_LEN 40

SOCKET connectsock(const char *hostname, uint16_t port);

#endif
/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
