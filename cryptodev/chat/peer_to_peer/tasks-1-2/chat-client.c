/**
 * chat-client.c
 * TCP/IP chat-app client for
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
#include <netdb.h>

#include <poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <string.h>

#include "chat-app-commons.h"

int cli_sockfd;


void _sigint_handler(int s) {
    if (shutdown(cli_sockfd, SHUT_WR) < 0) {
        perror("shutdown");
        exit(1);
    }
#ifdef CRYPT_ENABLE
    end_crypto_sess();
#endif /* CRYPT_ENABLE */

    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

int main(int argc, char *argv[]) {

    int port;
    char *hostname;
    struct hostent *hp;
    struct sockaddr_in cli_addr;
    nfds_t poll_amount = 2;
    struct pollfd to_watch[poll_amount];

    signal(SIGINT, _sigint_handler);

#ifdef CRYPT_ENABLE
    begin_crypto_sess();
#endif /* CRYPT_ENABLE */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s hostname port\n", argv[0]);
        exit(1);
    }
    hostname = argv[1];
    port = atoi(argv[2]);

    if ((cli_sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    fprintf(stderr, "Created TCP socket\n");

    if ( !(hp = gethostbyname(hostname))) {
        fprintf(stderr, "DNS lookup failed for host %s\n", hostname);
        exit(1);
    }

    cli_addr.sin_family = AF_INET;
    cli_addr.sin_port = htons(port);
    memcpy(&cli_addr.sin_addr.s_addr, hp->h_addr, sizeof(struct in_addr));
    fprintf(stderr, "Connecting to remote host... "); fflush(stderr);
    if (connect(cli_sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0) {
        perror("connect");
        exit(1);
    }
    fprintf(stderr, "Connected.\n");
    to_watch[0].fd = cli_sockfd;
    to_watch[0].events = POLLIN;
    to_watch[1].fd = 0;
    to_watch[1].events = POLLIN;
    if (pollooper(to_watch, poll_amount))
        exit(1);

    fprintf(stderr, "\nDone.\n");
    return 0;   
}