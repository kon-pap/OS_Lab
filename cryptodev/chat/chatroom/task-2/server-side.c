/*
* server-side.c
* Server-side crypto chat app using sockets
*
*/

#include "crypto-chat-common.h"

pthread_t tid[MAX_USERS];
int cfd;

/* Send message to all the clients except the one that send it */
void sendToClients(int senderSocketDescriptor, char *msg, char *name, struct crypt_op* cryp)
{
    char message[MESSAGE_SIZE], msgTemp[MESSAGE_SIZE], encMsg[MESSAGE_SIZE];
    int retval, bytes;
    fd_set wfds;

    memset(&message, '\0', sizeof(message));

    strcpy(msgTemp, msg);

    bytes = snprintf(message, sizeof(message), "%s: %s", name, msgTemp);

    /* Encrypt message */
    cryp->len = bytes + (16 - (bytes % BLOCK_SIZE));
    cryp->src = (void *)message;
    cryp->dst = (void *)encMsg;
    cryp->op = COP_ENCRYPT;

    if (ioctl(cfd, CIOCCRYPT, cryp))
    {
        perror("ioctl(CIOCCRYPT)");
        exit(1);
    }

    /*Send it*/
    sem_wait(&clientsLock);

    struct Client *tmp = headClient;
    while (tmp != NULL)
    {
        if (tmp->socketDescriptor != senderSocketDescriptor)
        {
            FD_ZERO(&wfds);
            FD_SET(tmp->socketDescriptor, &wfds);

            retval = select(tmp->socketDescriptor + 1, NULL, &wfds, NULL, NULL);
            if (retval < 0)
            {
                perror("select");
                return;
            }
            else
            {
                if (insist_write(tmp->socketDescriptor, encMsg, cryp->len) != cryp->len)
                {
                    perror("write");
                    continue;
                }
            }
        }
        tmp = tmp->next;
    }

    sem_post(&clientsLock);
}

/* Function that handles client-server communication */
void *clientSession(void *vargp)
{
    int clientSocketDescriptor = *(int *)vargp;
    int retval;
    char message[MESSAGE_SIZE], tempMsg[MESSAGE_SIZE], decMsg[MESSAGE_SIZE], name[64];
    const char delim[2] = ":";
    fd_set rfds;
    ssize_t bytesRead, bytesWritten;
    struct crypt_op cryp;

    /*Start session for AES218*/
    struct session_op sess;
    memset(&sess, 0, sizeof(sess));
    memset(&cryp, 0, sizeof(cryp));

    sess.cipher = CRYPTO_AES_CBC;
    sess.keylen = KEY_SIZE;
    sess.key = (void *)KEY;
    cryp.iv = (void *)IV;

    if (ioctl(cfd, CIOCGSESSION, &sess))
    {
        perror("ioctl(CIOCGSESSION)");
        exit(1);
    }
    cryp.ses = sess.ses;

    /* Ask client for username */
    bytesWritten = snprintf(message, sizeof(message), "Please enter your username: ");
    if (insist_write(clientSocketDescriptor, message, bytesWritten) != bytesWritten)
    {
        perror("write");
        exit(1);
    }

    /* Wait for client username */
    memset(&message, '\0', sizeof(message));
    bytesRead = read(clientSocketDescriptor, message, sizeof(message));
    if (bytesRead <= 0)
    {
        if (bytesRead < 0)
            perror("read from remote peer failed (username)");
        else
            fprintf(stderr, "Peer went away (username)\n");
        goto close;
    }

    /* Decrypt username */
    cryp.len = bytesRead;
    cryp.src = (void *)message;
    cryp.dst = (void *)decMsg;
    cryp.op = COP_DECRYPT;

    if (ioctl(cfd, CIOCCRYPT, &cryp))
    {
        perror("ioctl(CIOCCRYPT)");
        exit(1);
    }
    strcpy(name, decMsg);

    /* Add Client to linked list */
    addClient(name, clientSocketDescriptor);

    snprintf(decMsg, sizeof(decMsg), "%s has entered the chatroom!\n", name);

    sendToClients(clientSocketDescriptor, decMsg, "admin", &cryp);

    while (1)
    {
        // Wait for client to write something 
        memset(&message, '\0', sizeof(message));
        memset(&decMsg, '\0', sizeof(decMsg));
        FD_ZERO(&rfds);
        FD_SET(clientSocketDescriptor, &rfds);

        retval = select(clientSocketDescriptor + 1, &rfds, NULL, NULL, NULL);
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

            /* Decrypt username */
            cryp.len = bytesRead;
            cryp.src = (void *)message;
            cryp.dst = (void *)decMsg;
            cryp.op = COP_DECRYPT;

            if (ioctl(cfd, CIOCCRYPT, &cryp))
            {
                perror("ioctl(CIOCCRYPT)");
                exit(1);
            }

            strcpy(tempMsg, decMsg);
            if (isUser(strtok(tempMsg, delim)))
                continue;

            sendToClients(clientSocketDescriptor, decMsg, name, &cryp);
        }
    }

/* Remove Client from linked list */
remove:
    memset(&message, '\0', sizeof(message));
    snprintf(message, sizeof(message), "%s has left the chatroom!\n", name);

    sendToClients(clientSocketDescriptor, message, "admin", &cryp);
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
    if ((serverSocketDescriptor = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    /* Bind to a well-known port */
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(TCP_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(serverSocketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("bind");
        exit(1);
    }

    /* Listen for incoming connections */
    if (listen(serverSocketDescriptor, TCP_BACKLOG) < 0)
    {
        perror("listen");
        exit(1);
    }

    /* Open cryptodev */
    cfd = open("/dev/crypto", O_RDWR | O_CLOEXEC);
    if (cfd < 0)
    {
        perror("open(/dev/crypto)");
        exit(1);
    }

    /* Loop forever, accept()ing connections */
    id = 0;
    while (1)
    {
        fprintf(stderr, "Waiting for new clients...\n");

        /* Accept an incoming connection */
        addressLength = sizeof(struct sockaddr_in);
        if ((newSocketDescriptor = accept(serverSocketDescriptor, (struct sockaddr *)&clientAddress, &addressLength)) < 0)
        {
            perror("accept");
            exit(1);
        }

        /* Create new threads to handle client-server communication */
        char addressString[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &clientAddress.sin_addr, addressString, sizeof(addressString)))
        {
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