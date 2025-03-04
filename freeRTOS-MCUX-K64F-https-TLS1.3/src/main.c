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
 
#include "pico_stack.h"
#include "pico_ipv4.h"
#include "pico_socket.h"
#include "pico_enet_kinetis.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK64F12.h"
#include "fsl_sysmpu.h"
#include "wolfssl/ssl.h"
#include "certs.h"
#include "semphr.h"
#include "wolfboot/wolfboot.h"

extern unsigned int _stored_data;
extern unsigned int _start_data;
extern unsigned int _end_data;
extern unsigned int _start_bss;
extern unsigned int _end_bss;
extern unsigned int _end_stack;
extern unsigned int _start_heap;

static volatile struct pico_socket *cli = NULL;
static SemaphoreHandle_t *picotcp_started;
static SemaphoreHandle_t *picotcp_rx_data;

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

int pico_send(void *ssl, char *buf, int len, void *ctx)
{
    struct pico_socket *s = (struct pico_socket *)ctx;
    int r;
    r = pico_socket_write(s, buf, len);
    if (r > 0)
        return r;
    else
        return WOLFSSL_CBIO_ERR_WANT_WRITE;
}

int pico_recv(void *ssl, char *buf, int len, void *ctx)
{
    struct pico_socket *s = (struct pico_socket *)ctx;
    int r;
    r = pico_socket_read(s, buf, len);
    if (r > 0)
        return r;
    else
        return WOLFSSL_CBIO_ERR_WANT_READ;
}

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

static const char http_html_transfer_complete[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-Length: 170\r\n\r\n<html><meta http-equiv='refresh' content='30'/><body><p>Firmware transfer successful.</p><p>Update verification in progress. Please wait 30 seconds...</p></body></html>\r\n";
static const char http_html_internal_error[] = "HTTP/1.1 500 Server Error\r\n";
static const char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-Length: ";


static char http_index_html[] = "<html><head><title>wolfSSL Firmware Update</title></head>"
    "<body><img src='https://www.wolfssl.com/wordpress/wp-content/uploads/2019/01/wolfssl_logo-branding-1.png' alt='wolfSSL logo'></img>&nbsp;&nbsp;&nbsp;&nbsp;"
    "<img src='https://blog.mozilla.org/security/files/2018/08/TLS1.3-Badge-Color-512px-252x236.png' height='118' width='126' alt='tls 1.3 badge'></img><br>"
    "<h1>wolfSSL firmware update page</h1></br>"
    "Current version: 0x00000000<br>"
    "<p><form enctype='multipart/form-data' action='/update.cgi' method='POST'>"
   "Firmware: <input type='FILE' name='file'/>"
   "<input type='submit' name='Update' value='Update' />"
   "</form></p>"
   "</body>\r\n"
   "</html>\r\n\r\n";

uint8_t fw_buffer[2048];

static void parse_update(WOLFSSL *ssl, char *inbuf, size_t size)
{
    uint32_t fw_off = 0;
    uint32_t buf_off = 0;
    uint32_t fw_siz = 0;
    int i = 0;
    int res;
    while (i < size) {
        if (strncmp(&inbuf[i], "\r\n\r\nWOLF", 8) == 0) {
            i+=4;
            fw_siz = inbuf[i + 4] + ((inbuf[i + 5]) << 8) + ((inbuf[i + 6]) << 16) + ((inbuf[i + 7]) << 24);

            break;
        }
        i++;
    }
    if ((fw_siz > WOLFBOOT_PARTITION_SIZE) || (fw_siz < WOLFBOOT_SECTOR_SIZE))
        goto internal_error;
    fw_siz += 0x100;

    if (i < size) {
        memcpy(fw_buffer, &inbuf[i], (size - i));
        buf_off = size - i;
    }
    hal_flash_unlock();
    while(fw_off < fw_siz) {
        res = wolfSSL_read(ssl, fw_buffer + buf_off, 2048 - buf_off);
        if (res > 0) {
            buf_off += res;
            if ((buf_off == 2048) || (fw_siz - fw_off <= buf_off)) {
                if ((fw_off % WOLFBOOT_SECTOR_SIZE) == 0)
                    hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS + fw_off, WOLFBOOT_SECTOR_SIZE);
                if (fw_off + buf_off > fw_siz) {
                    buf_off = fw_siz - fw_off;
                    memset(fw_buffer + buf_off, 0xFF, 2048 - buf_off);
                }
                hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + fw_off, fw_buffer, 2048);
                fw_off += buf_off;
                buf_off = 0;
            }
        }
        xSemaphoreTake(picotcp_rx_data, pdMS_TO_TICKS(100));
    }
    if (fw_off == fw_siz) {
        wolfSSL_write(ssl, http_html_transfer_complete, strlen(http_html_transfer_complete));
        wolfBoot_update_trigger();
        
        /* Wait one second, reboot */
        vTaskDelay(pdMS_TO_TICKS(1000));
        reboot();
        return;
    }

internal_error:
    wolfSSL_write(ssl, http_html_internal_error, strlen(http_html_internal_error));
    return;
}

static void set_version(uint8_t *buf, int off)
{
    uint32_t fw_version = wolfBoot_current_firmware_version();
    uint8_t *vp = (uint8_t *) (&fw_version);
    int i;

    for (i = 3; i >= 0; i--) {
        char *a = &buf[off++];
        char *b = &buf[off++];
        char v;
        v = (vp[i] & 0xF0) >> 4;
        *a = v;
        v = vp[i] & 0x0F;
        *b = v;
        if (*a < 9) 
            *a += '0';
        else
            *a += 'A';
        if (*b < 9)
            *b += '0';
        else
            *b += 'A';
    }
}

static void https_request(WOLFSSL *ssl, char *url, int size)
{
    char lens[8] = "000\r\n\r\n";
    int  response_len;
    char *p;
    if (size >= 5 &&
            url[0] == 'G' &&
            url[1] == 'E' &&
            url[2] == 'T' &&
            url[3] == ' ' &&
            url[4] == '/') {

        response_len = strlen(http_index_html);
        wolfSSL_write(ssl, http_html_hdr, strlen(http_html_hdr));
        lens[0] = response_len / 100 + '0';
        lens[1] = (response_len % 100) / 10 + '0';
        lens[2] = (response_len % 10) + '0';
        wolfSSL_write(ssl, lens, strlen(lens));
        set_version(http_index_html, 424);
        wolfSSL_write(ssl, http_index_html, response_len); 
    }
    if (size >= 6 &&
            url[0] == 'P' &&
            url[1] == 'O' &&
            url[2] == 'S' &&
            url[3] == 'T' &&
            url[4] == ' ') {
        parse_update(ssl, url, size);

    }
}


static char HttpReq[1024];

void MainTask(void *pv)
{
    int res;
    struct pico_socket *s;
    uint16_t port = short_be(443);
    WOLFSSL *ssl;
    struct pico_ip4 any;
    wolfSSL_Init();
    xSemaphoreTake(picotcp_started, portMAX_DELAY);
    any.addr = 0;

    s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &socket_cb);
    pico_socket_bind(s, &any, &port);
    pico_socket_listen(s, 1);

    cli = pico_socket_accept(s, NULL, NULL);

    WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
    if (wolfSSL_CTX_use_certificate_buffer( ctx, server_cert, server_cert_len, SSL_FILETYPE_ASN1) != SSL_SUCCESS)
        while(1);
    if (wolfSSL_CTX_use_PrivateKey_buffer( ctx, server_key, server_key_len, SSL_FILETYPE_ASN1) != SSL_SUCCESS)
        while(1);

    wolfSSL_CTX_SetIORecv(ctx, pico_recv);
    wolfSSL_CTX_SetIOSend(ctx, pico_send);

    while(!cli) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ssl = wolfSSL_new(ctx);
    if (!ssl)
        while(1)
            ;
    wolfBoot_success();
    do {
        wolfSSL_SetIOReadCtx(ssl, cli);
        wolfSSL_SetIOWriteCtx(ssl, cli);
        res = wolfSSL_accept(ssl);
        xSemaphoreTake(picotcp_rx_data, pdMS_TO_TICKS(100));
    } while (res != SSL_SUCCESS);
    wolfSSL_set_using_nonblock(ssl, 1);

    while(1) {
        res = wolfSSL_read(ssl, HttpReq, 1024);
        if (res > 0) 
            https_request(ssl, HttpReq, res);
        xSemaphoreTake(picotcp_rx_data, pdMS_TO_TICKS(100));
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
