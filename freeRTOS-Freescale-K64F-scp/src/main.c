/* main.c
 *
 * wolfSSL firmware update demo application
 * running on FRDM-K64F with freeRTOS, picoTCP, wolfSSL and wolfBoot
 *
 * This application demonstrates how to transfer a firmware update on
 * Kinetis K64F, using HTTPS/TLS1.3.
 *
 * Firmware update is performed in wolfBoot after the image is transfered
 * and the system reboots.
 *
 * Copyright (C) 2019 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include "user_settings.h"
#include "pico_stack.h"
#include "pico_ipv4.h"
#include "pico_socket.h"
#include "pico_enet_kinetis.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK64F12.h"
#include "fsl_sysmpu.h"
#include "wolfssh/ssh.h"
#include "wolfssh/wolfscp.h"
#include "wolfssl/ssl.h"
#include "wolfssl/certs_test.h"
#include "wolfssl/wolfcrypt/sha256.h"
#include "wolfssh/error.h"
#include "certs.h"
#include "semphr.h"
#include "wolfboot/wolfboot.h"
#include "target.h"


const char client_pubkey[] = "ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBNkI5JTP6D0lF42tbxX19cE87hztUS6FSDoGvPfiU0CgeNSbI+aFdKIzTP5CQEJSvm25qUzgDtH7oyaQROUnNvk= hansel\n";

static char ServerBanner[] = "wolfSSH Firmware update server\n";
extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;
extern unsigned int _end_stack;
extern unsigned int _start_heap;

static struct pico_socket *cli = NULL;
static SemaphoreHandle_t picotcp_started;
static SemaphoreHandle_t picotcp_rx_data;

static void reboot(void)
{
#   define SCB_AIRCR         (*((volatile uint32_t *)(0xE000ED0C)))
#   define AIRCR_VECTKEY     (0x05FA0000)
#   define SYSRESET          (1 << 2)
    SCB_AIRCR = AIRCR_VECTKEY | SYSRESET;
}


/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

void init_data_bss(void)
{
    register unsigned int *src, *dst;
    src = (unsigned int *) &_stored_data;
    dst = (unsigned int *) &_start_data;
    /* Copy the .data section from flash to RAM. */
    while (dst < (unsigned int *)&_end_data) {
        *dst = *src;
        dst++;
        src++;
    }

    /* Initialize the BSS section to 0 */
    dst = &_start_bss;
    while (dst < (unsigned int *)&_end_bss) {
        *dst = 0U;
        dst++;
    }

}

#include "FreeRTOS.h"
#include "task.h"

static __attribute__ ((used,section(".noinit.$SRAM_LOWER_Heap5"))) uint8_t heap_sram_lower[16*1024]; /* placed in in no_init section inside SRAM_LOWER */
static __attribute__ ((used,section(".noinit_Heap5"))) uint8_t heap_sram_upper[128*1024]; /* placed in in no_init section inside SRAM_UPPER */

static HeapRegion_t xHeapRegions[] =
{
  { &heap_sram_lower[0], sizeof(heap_sram_lower) },
  { &heap_sram_upper[0], sizeof(heap_sram_upper)},
  { NULL, 0 } //  << Terminates the array.
};


static void socket_cb(uint16_t ev, struct pico_socket *s)
{
    struct pico_ip4 client_addr;
    uint16_t client_port;
    if (ev & PICO_SOCK_EV_CONN) {
        cli = pico_socket_accept(s, &client_addr, &client_port);
        return;
    }
    if (ev & PICO_SOCK_EV_RD) {
        xSemaphoreGive(picotcp_rx_data);
    }

}

static INLINE void c32toa(word32 u32, byte* c)
{
    c[0] = (u32 >> 24) & 0xff;
    c[1] = (u32 >> 16) & 0xff;
    c[2] = (u32 >>  8) & 0xff;
    c[3] =  u32 & 0xff;
}

/* Map user names to passwords */
/* Use arrays for username and p. The password or public key can
 * be hashed and the hash stored here. Then I won't need the type. */
typedef struct PwMap {
    byte type;
    byte username[32];
    word32 usernameSz;
    byte p[WC_SHA256_DIGEST_SIZE];
    struct PwMap* next;
} PwMap;


typedef struct PwMapList {
    PwMap* head;
} PwMapList;

static PwMap* PwMapNew(PwMapList* list, byte type, const byte* username,
                       word32 usernameSz, const byte* p, word32 pSz)
{
    PwMap* map;

    map = (PwMap*)XMALLOC(sizeof(PwMap), 0, 0);
    if (map != NULL) {
        wc_Sha256 sha;
        byte flatSz[4];

        map->type = type;
        if (usernameSz >= sizeof(map->username))
            usernameSz = sizeof(map->username) - 1;
        XMEMCPY(map->username, username, usernameSz + 1);
        map->username[usernameSz] = 0;
        map->usernameSz = usernameSz;

        wc_InitSha256(&sha);
        c32toa(pSz, flatSz);
        wc_Sha256Update(&sha, flatSz, sizeof(flatSz));
        wc_Sha256Update(&sha, p, pSz);
        wc_Sha256Final(&sha, map->p);

        map->next = list->head;
        list->head = map;
    }

    return map;
}


static void PwMapListDelete(PwMapList* list)
{
    if (list != NULL) {
        PwMap* head = list->head;

        while (head != NULL) {
            PwMap* cur = head;
            head = head->next;
            memset(cur, 0, sizeof(PwMap));
            free(cur);
        }
    }
}


static int UserAuth(byte authType,
                      WS_UserAuthData* authData,
                      void* ctx)
{
    PwMapList* list;
    PwMap* map;
    byte authHash[WC_SHA256_DIGEST_SIZE];
    wc_Sha256 sha;
    byte flatSz[4];
    wc_InitSha256(&sha);

    if (ctx == NULL)
        return WOLFSSH_USERAUTH_FAILURE;

    if (authType != WOLFSSH_USERAUTH_PUBLICKEY)
        return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;

    if (authType == WOLFSSH_USERAUTH_PUBLICKEY) {
        c32toa(authData->sf.publicKey.publicKeySz, flatSz);
        wc_Sha256Update(&sha, flatSz, sizeof(flatSz));
        wc_Sha256Update(&sha,
                authData->sf.publicKey.publicKey,
                authData->sf.publicKey.publicKeySz);
    }
    wc_Sha256Final(&sha, authHash);
    list = (PwMapList*)ctx;
    map = list->head;
    while (map != NULL) {
        if (authData->usernameSz == map->usernameSz &&
            memcmp(authData->username, map->username, map->usernameSz) == 0) {

            if (authData->type == map->type) {
                if (memcmp(map->p, authHash, WC_SHA256_DIGEST_SIZE) == 0)
                    return WOLFSSH_USERAUTH_SUCCESS;
                else
                    return (WOLFSSH_USERAUTH_INVALID_PUBLICKEY);
            } else {
                return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
            }
        }
        map = map->next;
    }
    return WOLFSSH_USERAUTH_INVALID_USER;
}

#ifndef SSH_BUFFER_SIZE
    #define SSH_BUFFER_SIZE 4096
#endif
#define SCP_BUFFER_SIZE 4096

#define SCRATCH_BUFFER_SIZE 1200
static uint8_t scratch[SCRATCH_BUFFER_SIZE];
static uint8_t fileBuffer[SCP_BUFFER_SIZE];
static uint32_t fileBuffer_off = 0;

/*
typedef int (*WS_CallbackScpRecv)(WOLFSSH*, int, const char*, const char*,
                                  int, word64, word64, word32, byte*, word32,
                                  word32, void*);
typedef int (*WS_CallbackScpSend)(WOLFSSH*, int, const char*, char*, word32,
                                  word64*, word64*, int*, word32, word32*,
                                  byte*, word32, void*);
                                  */

static int update_send_data(WOLFSSH* ssh, int state, const char* basePath,
    const char* fileName, int fileMode, word64 mTime, word64 aTime,
    word32 fw_sz, byte* buf, word32 bufSz, word32 fileOffset,
    void* ctx)
{
    return -1;
}


static int update_recv_data(WOLFSSH* ssh, int state, const char* basePath,
    const char* fileName, int fileMode, word64 mTime, word64 aTime,
    word32 fw_sz, byte* buf, word32 bufSz, word32 file_off,
    void* ctx)
{
    uint8_t *fw_dst = (uint8_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    uint32_t sz;
    uint32_t buf_off = 0;

    if (fileName) {
        if ((file_off == 0) && (bufSz > 0)) {
            hal_flash_erase(fw_dst, WOLFBOOT_PARTITION_SIZE);
            XMEMSET(fileBuffer, 0xFF, SCP_BUFFER_SIZE);
            fileBuffer_off = 0;
        }
        if (fileBuffer_off > 0) {
            file_off -= fileBuffer_off;
            sz = SCP_BUFFER_SIZE - fileBuffer_off;
            if (sz > bufSz)
                sz = bufSz;
            XMEMCPY(fileBuffer + fileBuffer_off, buf, sz);
            buf_off += sz;
            fileBuffer_off += sz;
        }
        if (fileBuffer_off == SCP_BUFFER_SIZE) {
            hal_flash_write(fw_dst + file_off, fileBuffer, SCP_BUFFER_SIZE);
            fileBuffer_off = 0;
            XMEMSET(fileBuffer, 0xFF, SCP_BUFFER_SIZE);
            file_off += SCP_BUFFER_SIZE;
        }
        while (buf_off < bufSz) {
            sz = SCP_BUFFER_SIZE;
            if ((bufSz - buf_off) < sz) {
                sz = bufSz - buf_off;
                XMEMCPY(fileBuffer + fileBuffer_off, buf + buf_off, sz);
                fileBuffer_off += sz;
            } else {
                hal_flash_write(fw_dst + file_off, buf + buf_off, sz);
            }
            if ((fileBuffer_off > 0) && (file_off + fileBuffer_off >= fw_sz)) {
                hal_flash_write(fw_dst + file_off, fileBuffer, SCP_BUFFER_SIZE);
                fileBuffer_off = 0;
                XMEMSET(fileBuffer, 0xFF, SCP_BUFFER_SIZE);
            }
            buf_off += sz;
            file_off += sz;
        }
    }
    return WS_SCP_CONTINUE;
}

static int load_key(byte isEcc, byte* buf, word32 bufSz)
{
    word32 sz = 0;
    if (isEcc) {
        if (sizeof_ecc_key_der_256 > bufSz) {
            return 0;
        }
        WMEMCPY(buf, ecc_key_der_256, sizeof_ecc_key_der_256);
        sz = sizeof_ecc_key_der_256;
    }
    else {
        if (sizeof_rsa_key_der_2048 > bufSz) {
            return 0;
        }
        WMEMCPY(buf, rsa_key_der_2048, sizeof_rsa_key_der_2048);
        sz = sizeof_rsa_key_der_2048;
    }
    return sz;
}

static int LoadPublicKeyBuffer(byte* buf, word32 bufSz, PwMapList* list)
{
    char* str = (char*)buf;
    char* delimiter;
    byte* publicKey64;
    word32 publicKey64Sz;
    byte* username;
    word32 usernameSz;
    byte  publicKey[300];
    word32 publicKeySz;

    /* Each line of passwd.txt is in the format
     *     ssh-rsa AAAB3BASE64ENCODEDPUBLICKEYBLOB username\n
     * This function modifies the passed-in buffer. */
    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0)
        return 0;

    while (*str != 0) {
        /* Skip the public key type. This example will always be ssh-rsa. */
        delimiter = strchr(str, ' ');
        if (delimiter == NULL) {
            return -1;
        }
        str = delimiter + 1;
        delimiter = strchr(str, ' ');
        if (delimiter == NULL) {
            return -1;
        }
        publicKey64 = (byte*)str;
        *delimiter = 0;
        publicKey64Sz = (word32)(delimiter - str);
        str = delimiter + 1;
        delimiter = strchr(str, '\n');
        if (delimiter == NULL) {
            return -1;
        }
        username = (byte*)str;
        *delimiter = 0;
        usernameSz = (word32)(delimiter - str);
        str = delimiter + 1;
        publicKeySz = sizeof(publicKey);

        if (Base64_Decode(publicKey64, publicKey64Sz,
                          publicKey, &publicKeySz) != 0) {

            return -1;
        }

        if (PwMapNew(list, WOLFSSH_USERAUTH_PUBLICKEY,
                     username, usernameSz,
                     publicKey, publicKeySz) == NULL ) {

            return -1;
        }
    }

    return 0;
}

int wolfssh_socket_send(WOLFSSH *ssh, void *_data, word32 len, void *_ctx)
{
    struct pico_socket *cli = (struct pico_socket *)_ctx;
    uint8_t *data = _data;
    return pico_socket_write(cli, data, len);
}

int wolfssh_socket_recv(WOLFSSH *ssh, void *_data, word32 len, void *_ctx)
{
    struct pico_socket *cli = (struct pico_socket *)_ctx;
    uint8_t *data = _data;
    return pico_socket_read(cli, data, len);
}

/* Custom file header interface for SCP */
static char file_header[] = "C0700 0 UPDATE.BIN";
char *scp_get_file_hdr(WOLFSSH *ssh)
{
    return file_header;
}

void MainTask(void *pv)
{
    int res;
    struct pico_socket *s;
    uint16_t port = short_be(22);
    uint32_t bufSz;
    WOLFSSH *ssh;
    struct pico_ip4 any;
    wolfSSH_Init();
    PwMapList pubkeyMapList;
    xSemaphoreTake(picotcp_started, portMAX_DELAY);
    any.addr = 0;

    s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &socket_cb);
    pico_socket_bind(s, &any, &port);
    pico_socket_listen(s, 1);

    cli = pico_socket_accept(s, NULL, NULL);

    WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    memset(&pubkeyMapList, 0, sizeof(pubkeyMapList));
    wolfSSH_SetUserAuth(ctx, UserAuth);
    wolfSSH_CTX_SetBanner(ctx, ServerBanner);

    /* Load key */
    bufSz = load_key(1, scratch, SCRATCH_BUFFER_SIZE);
    if (bufSz == 0)
        while(1)
            ;
    if (wolfSSH_CTX_UsePrivateKey_buffer(ctx, scratch, bufSz,
                WOLFSSH_FORMAT_ASN1) < 0) {
        while(1)
            ;
    }
    /* Load client public keys */
    strcpy(scratch, client_pubkey);
    LoadPublicKeyBuffer(scratch, strlen(scratch), &pubkeyMapList);

    /* Install ctx send/recv I/O */
    wolfSSH_SetIORecv(ctx, wolfssh_socket_recv);
    wolfSSH_SetIOSend(ctx, wolfssh_socket_send);

    while(!cli) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ssh = wolfSSH_new(ctx);
    if (!ssh)
        while(1)
            ;
    /* Set SCP callbacks */
    wolfSSH_SetScpRecv(ctx, update_recv_data);
    wolfSSH_SetScpSend(ctx, update_send_data);

    /* Set auth CTX based on the MapList */
    wolfSSH_SetUserAuthCtx(ssh, &pubkeyMapList);

    wolfBoot_success();
    while (1) {
        wolfSSH_SetUserAuthCtx(ssh, &pubkeyMapList);
        wolfSSH_SetIOReadCtx(ssh, cli);
        wolfSSH_SetIOWriteCtx(ssh, cli);
        res = wolfSSH_accept(ssh);
        if (res == WS_SCP_COMPLETE) {
            //printf("scp file transfer completed\n");
            wolfBoot_update_trigger();
            /* Wait one second, reboot */
            vTaskDelay(pdMS_TO_TICKS(1000));
            reboot();
        } else if (res == WS_SFTP_COMPLETE) {
            //printf("SFTP is not supported.\n");
        }
        wolfSSH_stream_exit(ssh, 0);
        pico_socket_close(cli);
        wolfSSH_free(ssh);
    }
}

void PicoTask(void *pv) {
    struct pico_device *dev = NULL;
    struct pico_ip4 addr, mask, gw, any;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 5;

    pico_string_to_ipv4("192.168.178.211", &addr.addr);
    pico_string_to_ipv4("255.255.255.0", &mask.addr);
    pico_string_to_ipv4("192.168.178.1", &gw.addr);
    any.addr = 0;
    pico_stack_init();
    dev = pico_enet_create("en0");
    if (dev) {
       pico_ipv4_link_add(dev, addr, mask);
       pico_ipv4_route_add(any, any, gw, 1, NULL);
       LED_GREEN_ON();
    }
    xSemaphoreGive(picotcp_started);
    pico_stack_tick();

    xLastWakeTime = xTaskGetTickCount();
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        pico_stack_tick();
    }
}
/*
 * @brief   Application entry point.
 */
int main(void) {
  	/* Init board hardware. */
    BOARD_InitPins();
    BOARD_BootClockRUN();
    SYSMPU_Enable(SYSMPU, false);

    LED_GREEN_INIT(1);
    vPortDefineHeapRegions(xHeapRegions); // Pass the array into vPortDefineHeapRegions(). Must be called first!

    picotcp_started = xSemaphoreCreateBinary();
    picotcp_rx_data = xSemaphoreCreateBinary();

    if (xTaskCreate(
        PicoTask,  /* pointer to the task */
        "picoTCP", /* task name for kernel awareness debugging */
        400, /* task stack size */
        (void*)NULL, /* optional task startup argument */
        tskIDLE_PRIORITY,  /* initial priority */
        (xTaskHandle*)NULL /* optional task handle to create */
      ) != pdPASS) {
       for(;;){} /* error! probably out of memory */
    }
    if (xTaskCreate(
        MainTask,  /* pointer to the task */
        "Main", /* task name for kernel awareness debugging */
        1200, /* task stack size */
        (void*)NULL, /* optional task startup argument */
        tskIDLE_PRIORITY,  /* initial priority */
        (xTaskHandle*)NULL /* optional task handle to create */
      ) != pdPASS) {
       for(;;){} /* error! probably out of memory */
    }

    vTaskStartScheduler();
    while(1) {
    }
    return 0 ;
}

void SystemInit(void)
{
}

