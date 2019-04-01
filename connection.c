#include "connection.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SOCKET connectsock(const char *hostname, uint16_t port)
{
	struct addrinfo hints, *res = NULL, *cur;
	SOCKET sock = INVALID_SOCKET;
    char _port[10];

    if (snprintf(_port, sizeof _port, "%hu", port) < 0)
        goto err;
    _port[(sizeof _port) -1] = '\0';

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

	if (getaddrinfo(hostname, _port, &hints, &res) != 0)
		goto err;

	for (cur = res; cur; cur = cur->ai_next) {
		sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (sock == INVALID_SOCKET)
			continue;

		if (connect(sock, cur->ai_addr,
                    cur->ai_addrlen) != -1)
			break;

		close(sock);
	}

err:
	freeaddrinfo(res);
	return sock;
}

/* vim: set ts=4 sw=4 tw=0 noet expandtab: */
