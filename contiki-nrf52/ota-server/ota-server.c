/* ota-server.c
 *
 * based on dtls-server.c from wolfssl-examples repository.
 *
 * Copyright (C) 2006-2015 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *=============================================================================
 *
 * OTA Upgrade mechanism implemented using DTLS 1.2
 *
 */

#include <stdio.h>                  /* standard in/out procedures */
#include <stdlib.h>                 /* defines system calls */
#include <string.h>                 /* necessary for memset */
#include <netdb.h>
#include <sys/socket.h>             /* used for all socket calls */
#include <netinet/in.h>             /* used for sockaddr_in6 */
#include <arpa/inet.h>
#include <wolfssl/ssl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SERV_PORT   11111           /* define our server port number */
#define MSGLEN      512 + 4

int wolfSSL_6LoWPAN_Send(WOLFSSL* ssl, char *buf, int sz, void *ctx);

static int cleanup;                 /* To handle shutdown */
struct sockaddr_in6 servAddr;        /* our server's address */
struct sockaddr_in6 cliaddr;         /* the client's address */

struct ota_ack {
    uint32_t error;
    uint32_t offset;
};

int main(int argc, char** argv)
{
    /* cont short for "continue?", Loc short for "location" */
    int         cont = 0;
    char        caCertLoc[] = "./server-ecc.pem";
    char        servCertLoc[] = "./server-ecc.pem";
    char        servKeyLoc[] = "./ecc-key.pem";
    WOLFSSL_CTX* ctx;
    /* Variables for awaiting datagram */
    int           on = 1;
    int           res = 1;
    int           sent = 0;
    int           connfd = 0;
    int           listenfd = 0;   /* Initialize our socket */
    WOLFSSL*      ssl = NULL;
    socklen_t     cliLen;
    char          buff[MSGLEN]; 
    uint32_t      len, tot_len;
    int           ffd; /* Firmware file descriptor */
    struct stat   st;
    struct ota_ack ack;

    if (argc != 2) {
        printf("Usage: %s firmware_filename\n", argv[0]);
        exit(1);
    }

    ffd = open(argv[1], O_RDONLY);
    if (ffd < 0) {
        perror("opening file");
        exit(2);
    }

    res = fstat(ffd, &st);
    if (res != 0) {
        perror("fstat file");
        exit(2);
    }
    tot_len = st.st_size;

    /* "./config --enable-debug" and uncomment next line for debugging */
    wolfSSL_Debugging_ON();

    /* Initialize wolfSSL */
    wolfSSL_Init();

    /* Set ctx to DTLS 1.2 */
    if ((ctx = wolfSSL_CTX_new(wolfDTLSv1_2_server_method())) == NULL) {
        printf("wolfSSL_CTX_new error.\n");
        return 1;
    }
    wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
    wolfSSL_CTX_SetIOSend(ctx, wolfSSL_6LoWPAN_Send);

    /* Load CA certificates */
    if (wolfSSL_CTX_load_verify_locations(ctx,caCertLoc,0) !=
            SSL_SUCCESS) {
        printf("Error loading %s, please check the file.\n", caCertLoc);
        return 1;
    }
    /* Load server certificates */
    if (wolfSSL_CTX_use_certificate_file(ctx, servCertLoc, SSL_FILETYPE_PEM) != 
            SSL_SUCCESS) {
        printf("Error loading %s, please check the file.\n", servCertLoc);
        return 1;
    }
    /* Load server Keys */
    if (wolfSSL_CTX_use_PrivateKey_file(ctx, servKeyLoc,
                SSL_FILETYPE_PEM) != SSL_SUCCESS) {
        printf("Error loading %s, please check the file.\n", servKeyLoc);
        return 1;
    }

    /* Await Datagram */
    while (cleanup != 1) {

        /* Create a UDP/IP socket */
        if ((listenfd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0 ) {
            printf("Cannot create socket.\n");
            cleanup = 1;
        }
        printf("Socket allocated\n");

        /* clear servAddr each loop */
        memset((char *)&servAddr, 0, sizeof(servAddr));

        /* host-to-network-long conversion (htonl) */
        /* host-to-network-short conversion (htons) */
        servAddr.sin6_family      = AF_INET6;
        servAddr.sin6_port        = htons(SERV_PORT);

        /* Eliminate socket already in use error */
        res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, len);
        if (res < 0) {
            perror("Setsockopt SO_REUSEADDR failed");
            cleanup = 1;
            cont = 1;
        }

        /*Bind Socket*/
        if (bind(listenfd, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
            printf("Bind failed.\n");
            cleanup = 1;
            cont = 1;
        }

        printf("Awaiting client connection on port %d\n", SERV_PORT);

        cliLen = sizeof(cliaddr);
        connfd = (int)recvfrom(listenfd, (char *)&buff, sizeof(buff), MSG_PEEK,
                (struct sockaddr*)&cliaddr, &cliLen);

        if (connfd < 0) {
            printf("No clients in queue, enter idle state\n");
            continue;
        }
        else if (connfd > 0) {
            if (connect(listenfd, (const struct sockaddr *)&cliaddr,
                        sizeof(cliaddr)) != 0) {
                printf("Udp connect failed.\n");
                cleanup = 1;
                cont = 1;
            }
        } else {
            printf("Recvfrom failed.\n");
            cleanup = 1;
            cont = 1;
        }
        printf("Client connected!\n");

        /* Create the WOLFSSL Object */
        if ((ssl = wolfSSL_new(ctx)) == NULL) {
            printf("wolfSSL_new error.\n");
            cleanup = 1;
            cont = 1;
        }
        wolfSSL_dtls_set_timeout_init(ssl, 12);

        /* set the session ssl to client connection port */
        wolfSSL_set_fd(ssl, listenfd);

        wolfSSL_SetIOWriteCtx(ssl, &listenfd);

        if (wolfSSL_accept(ssl) != SSL_SUCCESS) {
            int e = wolfSSL_get_error(ssl, 0);
            printf("error = %d, %s\n", e, wolfSSL_ERR_reason_error_string(e));
            printf("SSL_accept failed.\n");
            continue;
        }

        len = 0;
        lseek(ffd, 0, SEEK_SET); 
        res = wolfSSL_write(ssl, &tot_len, sizeof(uint32_t));
        printf("Sent image file size (%d)\n", tot_len);
        while (len < tot_len) {
            res = wolfSSL_read(ssl, &ack, sizeof(ack));
            if (res < 0) {
                int readErr = wolfSSL_get_error(ssl, 0);
                if(readErr != SSL_ERROR_WANT_READ) {
                    printf("SSL_read failed. (ssl error %d)\n", readErr);
                    cleanup = 1;
                    break;
                }
            }
            if (ack.error != 0) {
                printf("Device sent error = %d\n", ack.error);
                cleanup = 1;
                break;
            }
            if (ack.offset != len) {
                printf("buf rewind %u\n", ack.offset);
                lseek(ffd, ack.offset, SEEK_SET); 
                len = ack.offset;
            }
            res = read(ffd, buff + sizeof(uint32_t), MSGLEN - sizeof(uint32_t));
            memcpy(buff, &len, sizeof(len));
            if (res < 0) {
                printf("EOF\r\n");
                cleanup = 1;
                break;
            }
            sent = wolfSSL_write(ssl, buff, res + sizeof(uint32_t));
            if (sent > 0)
                len += MSGLEN - sizeof(uint32_t);
            printf("Sent bytes: %d/%d                  \r", len, tot_len);
            fflush(stdout);
        }
        printf("\n\n");

        wolfSSL_set_fd(ssl, 0);
        wolfSSL_shutdown(ssl);
        wolfSSL_free(ssl);

        printf("Client left cont to idle state\n");
        cont = 0;
    }

    /* With the "continue" keywords, it is possible for the loop to exit *
     * without changing the value of cont                                */
    if (cleanup == 1) {
        cont = 1;
    }

    if (cont == 1) {
        wolfSSL_CTX_free(ctx);
        wolfSSL_Cleanup();
        close(ffd);
    }

    return 0;
}

/* Custom send callback for wolfSSL.
 *
 * This introduces a 10ms delay after the packet is sent, so that
 * there are no collision generated by Linux 6LoWPAN driver.
 *
 *  return : nb bytes sent, or error
 */
int wolfSSL_6LoWPAN_Send(WOLFSSL* ssl, char *buf, int sz, void *ctx)
{
    int sd = *(int*)ctx;
    int sent;
    sent = send(sd, buf, sz, 0);
    if (sent < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return WOLFSSL_CBIO_ERR_WANT_WRITE;
        }
        else if (errno == ECONNRESET) {
            return WOLFSSL_CBIO_ERR_CONN_RST;
        }
        else if (errno == EINTR) {
            return WOLFSSL_CBIO_ERR_ISR;
        }
        else if (errno == EPIPE) {
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        }
        else {
            return WOLFSSL_CBIO_ERR_GENERAL;
        }
    }
    usleep(10000);
    return sent;
}
