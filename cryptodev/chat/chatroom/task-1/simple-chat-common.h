/*
 * simple-chat-common.h
 *
 * Simple TCP/IP chat app using sockets
 *
 */

#ifndef _SIMPLE_CHAT_COMMON_H
#define _SIMPLE_CHAT_COMMON_H

/* Compile-time options */
#define TCP_PORT    35001
#define TCP_BACKLOG 10
#define SERVER_NAME "snf-878950.vm.okeanos.grnet.gr"
#define MAX_USERS   10

/* Insist until all of the data has been written */
ssize_t insist_write(int fd, const void *buf, size_t cnt)
{
	ssize_t ret;
	size_t orig_cnt = cnt;
	
	while (cnt > 0) {
	        ret = write(fd, buf, cnt);
	        if (ret < 0)
	                return ret;
	        buf += ret;
	        cnt -= ret;
	}

	return orig_cnt;
}

#endif /* _SIMPLE_CHAT_COMMON_H */