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

ssize_t sendall(SOCKET sock, const void *buffer, size_t size)
{
    size_t sent;
    ssize_t r;

    /* FIXME we should actually check the length instead of simply casting from
     * size_t to ssize_t (or int), which have both the same width! */
	for (sent = 0; sent < size; sent += r) {
        r = send(sock, (void *) (((unsigned char *) buffer)+sent),
                (size-sent), MSG_NOSIGNAL);

		if (r < 0)
			return r;
	}

    return (ssize_t) sent;
}

ssize_t recvall(SOCKET sock, void *buffer, size_t size) {
    return recv(sock, buffer,
            size, MSG_WAITALL|MSG_NOSIGNAL);
}

SOCKET opensock(uint16_t port)
{
    SOCKET sock;
    socklen_t yes = 1;
    struct sockaddr_in server_sockaddr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return INVALID_SOCKET;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *) &yes, sizeof yes) != 0) 
        goto err;

#if HAVE_DECL_SO_NOSIGPIPE
    if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &yes, sizeof yes) != 0)
        goto err;
#endif

    memset(&server_sockaddr, 0, sizeof server_sockaddr);
    server_sockaddr.sin_family      = PF_INET;
    server_sockaddr.sin_port        = htons(port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *) &server_sockaddr,
                sizeof server_sockaddr) != 0)  {
        perror(NULL);
        goto err;
    }

    if (listen(sock, 0) != 0) {
        perror(NULL);
        goto err;
    }

    return sock;

err:
    close(sock);

    return INVALID_SOCKET;
}

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
