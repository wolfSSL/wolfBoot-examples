#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"
#include "semphr.h"
#include "pico_stack.h"



#define NO_64BIT
#define WOLFSSL_GENERAL_ALIGNMENT 4
//#define DEBUG_WOLFSSL
//#define WOLFSSL_LOG_PRINTF
//#define SINGLE_THREADED
#define WOLFSSL_USER_IO
#define CUSTOM_RAND_GENERATE pico_rand
#define CUSTOM_RAND_TYPE uint32_t 
#define CUSTOM_RAND_GENERATE_BLOCK rnd_custom_generate_block
#define XMALLOC_OVERRIDE
#define XMALLOC(s, h, type) pvPortMalloc((s))
#define XREALLOC(p, n, h, t) pvPortRealloc((p), (n))
#define XFREE(p, h, type)  vPortFree((p))
#define TIME_OVERRIDES
static inline long XTIME(long *x) { return xTaskGetTickCount() / configTICK_RATE_HZ;}
#define NO_ASN_TIME
#define WOLFSSL_USER_CURRTIME
#define NO_OLD_RNGNAME
#define SMALL_SESSION_CACHE
#define WOLFSSL_SMALL_STACK
#define TFM_ARM
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define NO_SIG_WRAPPER

/* ECC definitions */
#   define HAVE_ECC
#   define FP_ECC
#   define ECC_ALT_SIZE
#   define USE_FAST_MATH
#   undef FP_LUT
#   define FP_LUT 4
#   define WOLFSSL_HAVE_SP_ECC

/* SP math */
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE 32

/* Edwards */
#if 0
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define USE_FAST_MATH
#   define WOLFSSL_SHA512
#   define HAVE_CURVE25519
#   define WOLFSSL_SHA512

/* Chacha20-poly1305 */
#   define HAVE_CHACHA
#   define HAVE_POLY1305
#endif

/* RSA */
#define RSA_LOW_MEM
#define WC_RSA_BLINDING

/* TLS 1.3 */
#   define WOLFSSL_TLS13
#   define HAVE_TLS_EXTENSIONS
#   define HAVE_SUPPORTED_CURVES
#   define HAVE_HKDF
#   define HAVE_AEAD
#   define HAVE_AESGCM
#   define WC_RSA_PSS

/* SHA */
#define USE_SLOW_SHA
#define USE_SLOW_SHA2
//#define USE_SLOW_SHA512

/* Disabled ciphers */
#define NO_DES3
#define NO_MD4
#define NO_RABBIT
#define NO_RC4
#define NO_HC128
#define NO_HASH_DBRG


#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER


int pico_send(void *ssl, char *buf, int len, void *ctx);
int pico_recv(void *ssl, char *buf, int len, void *ctx);
#include <stdlib.h>
