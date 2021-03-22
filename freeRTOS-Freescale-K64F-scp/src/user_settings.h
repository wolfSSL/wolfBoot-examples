#ifndef USER_SETTINGS_INCLUDED
#define USER_SETTINGS_INCLUDED 
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"
#include "pico_stack.h"
#include "pico_socket.h"
#include "pico_port.h"

/* wolfSSH section */
#define WOLFSSH_SCP
#define WOLFSSH_SCP_USER_CALLBACKS
#define WOLFSSH_USER_IO
#define SEND_FUNCTION pico_send
#define RECV_FUNCTION pico_recv
#include "wolfssh/error.h"

static inline int pico_send(void *ssl, char *buf, int len, void *ctx)
{
    struct pico_socket *s = (struct pico_socket *)ctx;
    int r;
    r = pico_socket_write(s, buf, len);
    if (r > 0)
        return r;
    else
        return WS_WANT_WRITE;
}

static inline int pico_recv(void *ssl, char *buf, int len, void *ctx)
{
    struct pico_socket *s = (struct pico_socket *)ctx;
    int r;
    r = pico_socket_read(s, buf, len);
    if (r > 0)
        return r;
    else
        return WS_WANT_READ;
}




#define NO_64BIT
#define WOLFSSL_GENERAL_ALIGNMENT 4
//#define DEBUG_WOLFSSL
//#define WOLFSSL_LOG_PRINTF
#define SINGLE_THREADED
#define WOLFSSL_USER_IO
#define CUSTOM_RAND_GENERATE pico_rand
#define CUSTOM_RAND_TYPE uint32_t 
#define CUSTOM_RAND_GENERATE_BLOCK rnd_custom_generate_block
#define XMALLOC_OVERRIDE
#define XMALLOC(s, h, type) pvPortMalloc((s))
#define XREALLOC(p, n, h, t) pvPortRealloc((p), (n))
#define XFREE(p, h, type)  vPortFree((p))
#define NO_WOLFSSH_CLIENT

#define TIME_OVERRIDES
static inline long XTIME(long *x) { return xTaskGetTickCount() / configTICK_RATE_HZ;}
#define WOLFSSH_NO_TIMESTAMP
#define NO_ASN_TIME
#define WOLFSSL_USER_CURRTIME
#define NO_OLD_RNGNAME
#define SMALL_SESSION_CACHE
#define WOLFSSL_SMALL_STACK
#define TFM_ARM
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT

#define NO_TERMIOS

/* ECC definitions */
#   define HAVE_ECC
#   define HAVE_ECC_SIGN
#   define HAVE_ECC_VERIFY
#   define FP_ECC
#   define ECC_ALT_SIZE
#   define USE_FAST_MATH
#   undef FP_LUT
#   define FP_LUT 4
#   define WOLFSSL_HAVE_SP_ECC
#   define USE_CERT_BUFFERS_256

/* SP math */
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE 32

/* RSA */
#define HAVE_RSA
#define USE_CERT_BUFFERS_2048
#define RSA_LOW_MEM
#define WC_RSA_BLINDING
#define WC_RSA_PSS

/* SHA */
#define USE_SLOW_SHA
#define USE_SLOW_SHA2
//#define USE_SLOW_SHA512

/* AES */
#define HAVE_AESGCM
#define HAVE_AESCCM
#define HAVE_AES_COUNTER
#define HAVE_AES_DIRECT


/* Disabled ciphers */
#define NO_DES3
#define NO_MD4
#define NO_RABBIT
#define NO_RC4
#define NO_HC128


#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER


int pico_send(void *ssl, char *buf, int len, void *ctx);
int pico_recv(void *ssl, char *buf, int len, void *ctx);
#include <stdlib.h>
#endif
