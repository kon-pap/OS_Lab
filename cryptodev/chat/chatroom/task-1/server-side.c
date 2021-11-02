  /*
* server-side.c
* Server-side chat app using sockets
*
*/

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "simple-chat-common.h"

pthread_t tid[MAX_USERS];

/* Linked list node for users using chat */
struct Client { 
    char name[64];
    int socketDescriptor;
    struct Client* next; 
};

struct Client* headClient = NULL;
sem_t clientsLock;

/* Add Client */
void addClient(char *name, int sd) 
{ 
    sem_wait(&clientsLock);
    struct Client* newClient = (struct Client*) malloc(sizeof(struct Client)); 

    strcpy(newClient->name, name);
    newClient->socketDescriptor = sd;
    newClient->next = headClient;

    headClient = newClient;
    sem_post(&clientsLock);
}

/* Remove Client */
void removeClient(int sd)
{
    sem_wait(&clientsLock);
    
    struct Client* tmp = headClient;
    struct Client* prev = NULL;

    if (headClient->socketDescriptor == sd)
    {
        headClient = tmp->next;
        free(tmp);
        sem_post(&clientsLock);
        return;
    }

    while (tmp != NULL && tmp->socketDescriptor != sd)
    {
        prev = tmp;
        tmp = tmp->next;
    }

    if (tmp == NULL)
    {
        sem_post(&clientsLock);
        return;
    }
    
    prev->next = tmp->next;

    free(tmp);

    sem_post(&clientsLock);
}

/* Print Clients */
void printClients() 
{
    sem_wait(&clientsLock);
    fprintf(stderr, "Clients:\n");
    struct Client* tmp = headClient;
    while (tmp != NULL)  
    { 
        fprintf(stderr, "Username: %s, sd: %d\n", tmp->name, tmp->socketDescriptor);
        tmp = tmp->next; 
    }
    fprintf(stderr, "End\n");
    sem_post(&clientsLock);
}

/* Send message to all the clients except the one that send it */
void sendToClients(int senderSocketDescriptor, char *msg, char* name) 
{
    char message[1067], msgTemp[1000];
    int retval, bytes;
    fd_set wfds;

    strcpy(msgTemp, msg);

    sem_wait(&clientsLock);

    struct Client* tmp = headClient;
    while (tmp != NULL)  
    { 
        if (tmp->socketDescriptor != senderSocketDescriptor)
        {
            FD_ZERO(&wfds);
            FD_SET(tmp->socketDescriptor, &wfds);

            retval = select(tmp->socketDescriptor+1, NULL, &wfds, NULL, NULL);
            if (retval < 0)
            {
                perror("select");
                return;
            }
            else
            {
                bytes = snprintf(message, sizeof(message), "%s: %s\n", name, msgTemp);
                if (write(tmp->socketDescriptor, message, bytes) != bytes) {
                    perror("write");
                    continue;
                }
            }
        }
        tmp = tmp->next; 
    }

    sem_post(&clientsLock);
}

/* Check if string is a username */
int isUser(char *name)
{
    if (name == NULL)
        return 0;
    
    sem_wait(&clientsLock);

    struct Client* tmp = headClient;
    while (tmp != NULL)  
    { 
        if (strcmp(tmp->name, name) == 0)
        {
            sem_post(&clientsLock);
            return 1;
        }
        tmp = tmp->next; 
    }
    sem_post(&clientsLock);
    return 0;
}

/* Function that handles client-server communication */
void* clientSession(void *vargp)
{
    int clientSocketDescriptor = *(int *)vargp;
    int retval;
    char message[1000], tempMsg[1000], name[64];
    const char delim[2] = ":";
    fd_set rfds;
    ssize_t bytesRead, bytesWritten;

    /* Ask client for username */
    bytesWritten = snprintf(message, sizeof(message), "Please enter your username: ");

    if (insist_write(clientSocketDescriptor, message, bytesWritten) != bytesWritten)
    {
        perror("write");
        exit(1);
    }
    
    /* Wait for client username */
    memset(message, 0, sizeof(message));
    bytesRead = read(clientSocketDescriptor, message, sizeof(message));
    if (bytesRead <= 0)
    {
        if (bytesRead < 0)
            perror("read from remote peer failed (username)");
        else
            fprintf(stderr, "Peer went away (username)\n");
        goto close;
    }
    strcpy(name, message);

    /* Add Client to linked list */
    addClient(name, clientSocketDescriptor);

    snprintf(message, sizeof(message), "%s has entered the chatroom!", name);
    fprintf(stderr, "%s\n", message);
    sendToClients(clientSocketDescriptor, message, "admin");

    while(1)
    {
        /* Wait for client to write something */
        memset(message, 0, sizeof(message));
        FD_ZERO(&rfds);
        FD_SET(clientSocketDescriptor, &rfds);

        retval = select(clientSocketDescriptor+1, &rfds, NULL, NULL, NULL);
        if (retval < 0)
        {
            perror("select");
            goto remove;
        }
        else
        {
            bytesRead = read(clientSocketDescriptor, message, sizeof(message));

            if (bytesRead <= 0)
            {
                if (bytesRead < 0)
                    perror("read from remote peer failed");

                fprintf(stderr, "%s left chatroom\n", name);
                goto remove;
            }
            strcpy(tempMsg, message);
            if (isUser(strtok(tempMsg, delim)))
                continue;

            sendToClients(clientSocketDescriptor, message, name);
        }
    }

    /* Remove Client from linked list */
    remove:
    snprintf(message, sizeof(message), "%s has left the chatroom!", name);
    fprintf(stderr, "%s\n", message);
    sendToClients(clientSocketDescriptor, message, "admin");
    removeClient(clientSocketDescriptor);

    /* Close client socket */
    close:
    if (close(clientSocketDescriptor) < 0)
		perror("close");

    return NULL;
}

int main()
{
    int serverSocketDescriptor, newSocketDescriptor, id;
    socklen_t addressLength;
    struct sockaddr_in serverAddress, clientAddress;

    sem_init(&clientsLock, 1, 1);

    /* Make sure a broken connection doesn't kill us */
	signal(SIGPIPE, SIG_IGN);

    /* Initialize socket */
    if ((serverSocketDescriptor = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

    /* Bind to a well-known port */
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(TCP_PORT);
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(serverSocketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("bind");
		exit(1);
	}

    /* Listen for incoming connections */
	if (listen(serverSocketDescriptor, TCP_BACKLOG) < 0) {
		perror("listen");
		exit(1);
	}

    /* Loop forever, accept()ing connections */
    id = 0;
    while(1)
    {
        fprintf(stderr, "Waiting for new clients...\n");

        /* Accept an incoming connection */
		addressLength = sizeof(struct sockaddr_in);
		if ((newSocketDescriptor = accept(serverSocketDescriptor, (struct sockaddr *)&clientAddress, &addressLength)) < 0) {
			perror("accept");
			exit(1);
		}

        /* Create new threads to handle client-server communication */
        char addressString[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &clientAddress.sin_addr, addressString, sizeof(addressString))) {
            perror("could not format IP address");
            continue;
        }
        fprintf(stderr, "Incoming connection from %s:%d\n",
                addressString, ntohs(clientAddress.sin_port));

        if (pthread_create(&tid[id], NULL, clientSession, &newSocketDescriptor) != 0)
        {
            perror("thread_create");
            exit(1);
        }
        else
            id++;      
    }

    return 1;
}