/*
 * crypto-chat-common.h
 *
 * Crypto TCP/IP chat app using sockets
 *
 */

#ifndef _CRYPTO_CHAT_COMMON_H
#define _CRYPTO_CHAT_COMMON_H

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <crypto/cryptodev.h>

/* Compile-time options */
#define TCP_PORT 35001
#define TCP_BACKLOG 10
#define SERVER_NAME "snf-878950.vm.okeanos.grnet.gr"
#define MAX_USERS 10

#define MESSAGE_SIZE 256
#define BLOCK_SIZE 16
#define KEY_SIZE 16 /* AES128 */
#define KEY "AB4EE18D78FA006"
#define IV "AGHASLERFLWF348"

/* Linked list node for users using chat */
struct Client
{
    char name[64];
    int socketDescriptor;
    struct Client *next;
};

struct Client *headClient = NULL;
sem_t clientsLock;

/* Add Client */
void addClient(char *name, int sd)
{
    sem_wait(&clientsLock);
    struct Client *newClient = (struct Client *)malloc(sizeof(struct Client));

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

    struct Client *tmp = headClient;
    struct Client *prev = NULL;

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
    struct Client *tmp = headClient;
    while (tmp != NULL)
    {
        fprintf(stderr, "Username: %s, sd: %d\n", tmp->name, tmp->socketDescriptor);
        tmp = tmp->next;
    }
    fprintf(stderr, "End\n");
    sem_post(&clientsLock);
}

/* Check if string is a username */
int isUser(char *name)
{
    if (name == NULL)
        return 0;

    sem_wait(&clientsLock);

    struct Client *tmp = headClient;
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

/* Insist until all of the data has been written */
ssize_t insist_write(int fd, const void *buf, size_t cnt)
{
    ssize_t ret;
    size_t orig_cnt = cnt;

    while (cnt > 0)
    {
        ret = write(fd, buf, cnt);
        if (ret < 0)
            return ret;
        buf += ret;
        cnt -= ret;
    }

    return orig_cnt;
}

/* Insist until all of the data has been read */
ssize_t insist_read(int fd, void *buf, size_t cnt)
{
    ssize_t ret;
    size_t orig_cnt = cnt;

    while (cnt > 0)
    {
        ret = read(fd, buf, cnt);
        if (ret < 0)
            return ret;
        buf += ret;
        cnt -= ret;
    }

    return orig_cnt;
}

#endif /* _CRYPTO_CHAT_COMMON_H */