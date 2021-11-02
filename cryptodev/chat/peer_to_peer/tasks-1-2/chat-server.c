/**
 * chat-server.c
 * TCP/IP chat-app server for
 * one-to-one only communications
 * 
 * Zografos Orfeas <zografos.orfeas@gmail.com>
 * Konstantinos Papaioannou <konpap99@hotmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <string.h>

#include "chat-app-commons.h"



void _sigint_handler(int s) {
#ifdef CRYPT_ENABLE
    end_crypto_sess();
#endif /* CRYPT_ENABLE */
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

int main(int argc, char *argv[]) {
    
    int serv_sockfd, cli_sockfd, retval;
    nfds_t poll_amount = 2;
    struct pollfd to_watch[poll_amount];
    char  addrstr[INET_ADDRSTRLEN];
    struct sockaddr_in  serv_addr, cli_addr;
    socklen_t cli_len;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, _sigint_handler);

#ifdef CRYPT_ENABLE
    begin_crypto_sess();
#endif /* CRYPT_ENABLE */

    if ((serv_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Failed to create server socket");
        raise(SIGINT);
    }
    fprintf(stderr, "Server TCP/IP socket created\n");
    
    bzero((void *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(CHAT_TCP_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(serv_sockfd, (struct sockaddr * )&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Failed to bind server socket to address-port");
        raise(SIGINT);
    }
    fprintf(stderr, "Socket bound to port %d on local machine\n", CHAT_TCP_PORT);

    if (listen(serv_sockfd, TCP_BACKLOG) < 0) {
        perror("Failed to start listening for connections");
        raise(SIGINT);
    }

    for (;;) {
        fprintf(stderr, "Waiting for an incoming connection... \n");

        cli_len = sizeof(struct sockaddr_in);
        if ((cli_sockfd = accept(serv_sockfd, (struct sockaddr *) &cli_addr, &cli_len)) < 0 ) {
            perror("Failed to accept a connection");
            raise(SIGINT);
        }
        if (!inet_ntop(AF_INET, &cli_addr.sin_addr, addrstr, sizeof(addrstr))) {
            perror("Failed to format incoming IP address");
            raise(SIGINT);
        }
        fprintf(stderr, "Incoming connection from %s:%d\n",
            addrstr, ntohs(cli_addr.sin_port));

        /**
         * setup polling structs etc.
         */
        to_watch[0].fd = cli_sockfd;
        to_watch[0].events = POLLIN;
        to_watch[1].fd = 0;
        to_watch[1].events = POLLIN;
        if (pollooper(to_watch, poll_amount))
            raise(SIGINT);
        
        if (close(cli_sockfd) < 0)
            perror("close error'ed");
    }

    // dummy return, probably shouldn't happen.
    return 1;
}