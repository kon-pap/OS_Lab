/*
* client-side.c
* Client-side chat app using sockets
*
*/

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "simple-chat-common.h"

int bufLim;
char buf[1000];

/* Listener */
void* serverListener(void *vargp)
{
    int retval, bytesRead;
    int socketDescriptor = *(int *)vargp;
    char message[1067];
    fd_set rfds;
    while(1)
    {
        FD_ZERO(&rfds);
        FD_SET(socketDescriptor, &rfds);

        retval = select(socketDescriptor+1, &rfds, NULL, NULL, NULL);
        if (retval < 0)
        {
            perror("select");
            exit(1);
        }
        else
        {
            bytesRead = read(socketDescriptor, message, sizeof(message));

            if (bytesRead <= 0) {
                perror("read");
                exit(1);
            }

            if (write(1, message, bytesRead) != bytesRead) {
                perror("write");
                exit(1);
            }
        }
    }
    
    return NULL;
    
}

int main(int argc, char *argv[])
{
    char message[10000];
    int socketDescriptor, port, retval, bytesRead;
    fd_set rfds, wfds;
    pthread_t tid;
    struct hostent *hostPointer;
    struct sockaddr_in serverAddress;
    struct timeval timeInterval;

    if (argc != 2) {
		fprintf(stderr, "Usage: %s port\n", argv[0]);
		exit(1);
	}

    port = atoi(argv[1]);
    if (port < 1024 || port > 65535)
    {
        fprintf(stderr, "Port number is invalid. Please choose one in range 1024-65535\n");
		exit(1);
    }

    /* Create TCP/IP socket, used as main chat channel */
	if ((socketDescriptor = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	
	/* Look up server hostname on DNS */
	if ( !(hostPointer = gethostbyname(SERVER_NAME))) {
		fprintf(stderr, "DNS lookup failed for host %s\n", SERVER_NAME);
		exit(1);
	}

    /* Connect to remote TCP port */
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(TCP_PORT);
	memcpy(&serverAddress.sin_addr.s_addr, hostPointer->h_addr, sizeof(struct in_addr));
	if (connect(socketDescriptor, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0) {
		perror("connect");
		exit(1);
	}

	fprintf(stderr, "Welcome to the chatroom!\n");

    /* Start interacting */

    /* Wait for message asking for username */
    FD_ZERO(&rfds);
    FD_SET(socketDescriptor, &rfds);

    timeInterval.tv_sec = 5;
    timeInterval.tv_usec = 0;

    retval = select(socketDescriptor+1, &rfds, NULL, NULL, &timeInterval);
    if (retval < 0)
    {
        perror("select");
        exit(1);
    }
    else if (retval == 0)
    {
        printf("Server unresponsive: Please try again later!\n");
        exit(1);
    }
    else
    {
        bytesRead = read(socketDescriptor, message, sizeof(message));

        if (bytesRead <= 0) {
            perror("read");
            exit(1);
        }

        if (write(1, message, bytesRead) != bytesRead) {
            perror("write");
            exit(1);
        }
    }

    /* Wait user to type username */
    scanf("%64s", message);
    bytesRead = 0;
    while (message[bytesRead] != '\0')
        bytesRead++;

    /* Send username to server */
    FD_ZERO(&wfds);
    FD_SET(socketDescriptor, &wfds);

    retval = select(socketDescriptor+1, NULL, &wfds, NULL, NULL);
    if (retval < 0)
        perror("select");
    else
    {
        if (insist_write(socketDescriptor, message, bytesRead) != bytesRead) {
            perror("insist_write (username)");
            exit(1);
        }
    }

    /* Start chating */
    /* Incoming message listener */
    if (pthread_create(&tid, NULL, serverListener, &socketDescriptor) != 0)
    {
        perror("thread_create");
        exit(1);
    }

    /* Outgoing message listener */
    while(1)
    {
        /* Read user message */
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);

        retval = select(1, &rfds, NULL, NULL, NULL);
        if (retval < 0)
        {
            perror("select");
            exit(1);
        }
        else
        {
            bytesRead = read(0, message, sizeof(message));
            if (bytesRead <= 0) {
                perror("read");
                exit(1);
            } 
        }

        /* Send message to server */
        FD_ZERO(&wfds);
        FD_SET(socketDescriptor, &wfds);

        retval = select(socketDescriptor+1, NULL, &wfds, NULL, NULL);
        if (retval < 0)
            perror("select");
        else
        {
            if (bytesRead > 1) bytesRead--;
            if (insist_write(socketDescriptor, message, bytesRead) != bytesRead) {
                perror("insist_write");
                exit(1);
            }
        }
    }

	return 0;
}