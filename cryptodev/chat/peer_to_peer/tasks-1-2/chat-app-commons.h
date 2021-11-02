/**
 * chat-app-commons.h
 * In header library containing most of the
 * common functions and operations.
 * 
 * Zografos Orfeas <zografos.orfeas@gmail.com>
 * Konstantinos Papaioannou <konpap99@hotmail.com>
 */

#ifndef _CHAT_APP_COMMONS_H
#define _CHAT_APP_COMMONS_H

#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define CHAT_TCP_PORT 38888
#define TCP_BACKLOG 5

#ifdef CRYPT_ENABLE

#include <crypto/cryptodev.h>

#define KEY_SIZE 16         /* AES128 key size in bytes */
#define BLOCK_SIZE 16       /* AES128 block size in bytes */
#define DATA_MAX_SIZE 256   /* max message acceptable to encrypt in bytes */ 

#define ENCRYPTION_KEY "_thisIsATestKey_"    /* bad practice, just for dev */
#define INIT_VECTOR "_thisIsATestVec_"       /* again, don't really do this.. */

char *key = ENCRYPTION_KEY;
char *vec = INIT_VECTOR;
int fd;

struct crypt_op cryp;
struct session_op sess;

int begin_crypto_sess() {
    fd = open("/dev/crypto", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/crypto)");
        exit(1); //semi-bad practice
    }
    memset(&sess, 0, sizeof(sess));
    memset(&cryp, 0, sizeof(cryp));
    sess.cipher = CRYPTO_AES_CBC;
    sess.keylen = KEY_SIZE;
    sess.key = key;

    cryp.iv = vec;

    if (ioctl(fd, CIOCGSESSION, &sess)) {
        perror("ioctl(CIOCGSESSION)");
        exit(1);
    }
    cryp.ses = sess.ses;

    return fd;
}

void end_crypto_sess() {
    if (ioctl(fd, CIOCFSESSION, &sess.ses)) {
        perror("ioctl(CIOCFSESSION)");
        exit(1);
    }
    if (close(fd) < 0) {
        perror("close(fd)");
        exit(1);
    }
}
void encrypt(unsigned char inbuf[], long size, unsigned char outbuf[]) {
    unsigned char temp[DATA_MAX_SIZE];
    cryp.len = (int)size;
    cryp.src = inbuf;
    cryp.dst = temp;
    cryp.op = COP_ENCRYPT;

    if (ioctl(fd, CIOCCRYPT, &cryp)) {
        perror("ioctl(CIOCCRYPT)");
        exit(1);
    }
    memcpy(outbuf, temp, size);
    return;
}

void decrypt(unsigned char inbuf[], long size, unsigned char outbuf[]) {
    unsigned char temp[DATA_MAX_SIZE];
    cryp.len = (int)size;
    cryp.src = inbuf;
    cryp.dst = temp;
    cryp.op = COP_DECRYPT;

    if (ioctl(fd, CIOCCRYPT, &cryp)) {
        perror("ioctl(CIOCCRYPT");
        exit(1);
    }
    memcpy(outbuf, temp, size);
    return;
}
#endif /* CRYPT_ENABLE */

ssize_t insist_write(int fd, const void *buf, size_t cnt){
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

int pollooper(struct pollfd *fds, nfds_t fds_size) {
    int retval;
    ssize_t n;
    char msg_buffer[256];
    for(;;) {
        retval = poll(fds, fds_size, -1);

        if (retval < 0) {
            perror("Failed to call poll()");
            return 1;
        }
        if (retval == 0) {
            fprintf(stderr, "poll() somehow timed out, oops\n");
            return 1;
        }
        // checking other.
        if (fds[0].revents == POLLIN) {
            /**
             * read from other, write to my stdout
             * https://www.softlab.ntua.gr/facilities/documentation/unix/unix-socket-faq/unix-socket-faq-2.html
             * breaks if read fails or other_side leaves
             * returns error (that's 1) if write fails
             */
            n = read(fds[0].fd, msg_buffer, sizeof(msg_buffer));
            if (n <= 0) {
                if (n < 0) 
                    perror("read from other failed");
                else
                    fprintf(stderr, "Peer went away\n");
                break;
            }
#ifdef CRYPT_ENABLE
            decrypt(msg_buffer, n + (BLOCK_SIZE-(n%BLOCK_SIZE)), msg_buffer);
#endif /* CRYPT_ENABLE */
            if (n < 256) msg_buffer[n] = '\0';
            fprintf(stdout, "Other: %s", msg_buffer);
            fflush(stdout);
        }
        if (fds[1].revents == POLLIN) {
            /**
             * read from my stdin, write to fds[0].fd
             * breaks if write fails.
             * return error (that's 1) if read fails
             */
            if(!fgets(msg_buffer, sizeof(msg_buffer)-1,stdin))
                return 1;
            n = strlen(msg_buffer);
#ifdef CRYPT_ENABLE
            for (long i = n; i < (n + (BLOCK_SIZE-(n%BLOCK_SIZE))); i++) {
                msg_buffer[i] = '\0';
            }
            n = n + (BLOCK_SIZE-(n%BLOCK_SIZE));
            encrypt(msg_buffer, n, msg_buffer);
#endif /* CRYPT_ENABLE */
            if (insist_write(fds[0].fd, msg_buffer, n) != n) {
                perror("write to other failed");
                break;
            }
        }
    }
    return 0;
}


#endif /* _CHAT_APP_COMMONS_H */
