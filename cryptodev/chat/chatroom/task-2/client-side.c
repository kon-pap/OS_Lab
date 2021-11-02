/*
* client-side.c
* Client-side crypto chat app using sockets
*
*/

#include "crypto-chat-common.h"

int fd;

/* Listener */
void *serverListener(void *vargp)
{
    int retval, bytesRead;
    int socketDescriptor = *(int *)vargp;
    unsigned char message[MESSAGE_SIZE], decMsg[MESSAGE_SIZE];
    fd_set rfds;
    struct crypt_op cryp;
    struct session_op sess;

    memset(&sess, 0, sizeof(sess));
    memset(&cryp, 0, sizeof(cryp));

    /* Session for AES128 */
    sess.cipher = CRYPTO_AES_CBC;
    sess.keylen = KEY_SIZE;
    sess.key = (void *)KEY;

    if (ioctl(fd, CIOCGSESSION, &sess))
    {
        perror("ioctl(CIOCGSESSION)");
        exit(1);
    }

    cryp.ses = sess.ses;
    cryp.iv = (void *)IV;

    while (1)
    {
        memset(&message, '\0', sizeof(message));
        memset(&decMsg, '\0', sizeof(decMsg));
        printf("cleared\n");
        FD_ZERO(&rfds);
        FD_SET(socketDescriptor, &rfds);

        retval = select(socketDescriptor + 1, &rfds, NULL, NULL, NULL);
        if (retval < 0)
        {
            perror("select");
            exit(1);
        }
        else
        {
            bytesRead = read(socketDescriptor, message, sizeof(message));

            if (bytesRead <= 0)
            {
                perror("read");
                exit(1);
            }

            /* Decrypt message */
            cryp.len = bytesRead;
            cryp.src = (void *)message;
            cryp.dst = decMsg;
            cryp.op = COP_DECRYPT;

            if (ioctl(fd, CIOCCRYPT, &cryp))
            {
                perror("ioctl(CIOCCRYPT)");
                exit(1);
            }

            if (insist_write(1, decMsg, bytesRead) != bytesRead)
            {
                perror("write");
                exit(1);
            }
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    unsigned char message[MESSAGE_SIZE], encMsg[MESSAGE_SIZE];
    int socketDescriptor, port, retval, bytesRead;
    fd_set rfds, wfds;
    pthread_t tid;
    struct hostent *hostPointer;
    struct sockaddr_in serverAddress;
    struct timeval timeInterval;
    struct crypt_op cryp;

    if (argc != 2)
    {
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
    if ((socketDescriptor = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    /* Look up server hostname on DNS */
    if (!(hostPointer = gethostbyname(SERVER_NAME)))
    {
        fprintf(stderr, "DNS lookup failed for host %s\n", SERVER_NAME);
        exit(1);
    }

    /* Connect to remote TCP port */
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(TCP_PORT);
    memcpy(&serverAddress.sin_addr.s_addr, hostPointer->h_addr, sizeof(struct in_addr));
    if (connect(socketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("connect");
        exit(1);
    }

    /* Open cryptodev */
    fd = open("/dev/crypto", O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        perror("open(/dev/crypto)");
        exit(1);
    }

    fprintf(stderr, "Welcome to the chatroom!\n");

    /* Start interacting */
    struct session_op sess;
    memset(&sess, 0, sizeof(sess));
    memset(&cryp, 0, sizeof(cryp));

    /* Session for AES128 */
    sess.cipher = CRYPTO_AES_CBC;
    sess.keylen = KEY_SIZE;
    sess.key = (void *)KEY;
    cryp.iv = (void *)IV;

    if (ioctl(fd, CIOCGSESSION, &sess))
    {
        perror("ioctl(CIOCGSESSION)");
        exit(1);
    }
    cryp.ses = sess.ses;

    /* Wait for message asking for username */
    FD_ZERO(&rfds);
    FD_SET(socketDescriptor, &rfds);

    timeInterval.tv_sec = 5;
    timeInterval.tv_usec = 0;

    retval = select(socketDescriptor + 1, &rfds, NULL, NULL, &timeInterval);
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
        memset(&message, '\0', sizeof(message));
        bytesRead = read(socketDescriptor, message, sizeof(message));

        if (bytesRead <= 0)
        {
            perror("read");
            exit(1);
        }

        if (write(1, message, bytesRead) != bytesRead)
        {
            perror("write");
            exit(1);
        }
    }

    /* Wait user to type username */
    memset(&message, '\0', sizeof(message));
    memset(&encMsg, '\0', sizeof(encMsg));
    scanf("%64s", message);
    bytesRead = 0;
    while (message[bytesRead] != '\0')
        bytesRead++;

    /* Send username to server */
    FD_ZERO(&wfds);
    FD_SET(socketDescriptor, &wfds);

    retval = select(socketDescriptor + 1, NULL, &wfds, NULL, NULL);
    if (retval < 0)
        perror("select");
    else
    {
        /*Encrypt username */
        cryp.len = bytesRead + (16 - (bytesRead % BLOCK_SIZE));
        cryp.src = (void *)message;
        cryp.dst = encMsg;
        cryp.op = COP_ENCRYPT;

        if (ioctl(fd, CIOCCRYPT, &cryp))
        {
            perror("ioctl(CIOCCRYPT)");
            exit(1);
        }

        if (insist_write(socketDescriptor, encMsg, cryp.len) != cryp.len)
        {
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
    while (1)
    {
        /* Read user message */
        memset(&message, '\0', sizeof(message));
        memset(&encMsg, '\0', sizeof(encMsg));
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
            if (bytesRead <= 0)
            {
                perror("read");
                exit(1);
            }
        }

        /* Send message to server */
        FD_ZERO(&wfds);
        FD_SET(socketDescriptor, &wfds);

        retval = select(socketDescriptor + 1, NULL, &wfds, NULL, NULL);
        if (retval < 0)
            perror("select");
        else
        {
            if (bytesRead > 1)
                bytesRead--;

            /* Encrypt message */
            cryp.len = bytesRead + (16 - (bytesRead % BLOCK_SIZE));
            cryp.src = (void *)message;
            cryp.dst = encMsg;
            cryp.op = COP_ENCRYPT;

            if (ioctl(fd, CIOCCRYPT, &cryp))
            {
                perror("ioctl(CIOCCRYPT)");
                exit(1);
            }

            if (insist_write(socketDescriptor, encMsg, cryp.len) != cryp.len)
            {
                perror("insist_write");
                exit(1);
            }
        }
    }
    /* Stop interacting */

    return 0;
}