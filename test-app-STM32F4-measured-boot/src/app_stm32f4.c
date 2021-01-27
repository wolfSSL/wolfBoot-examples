/* app_stm32f4.c
 *
 * Test bare-metal application for reporting measured boot value over UART
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system.h"
#include "timer.h"
#include "led.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "spi_flash.h"
#include "spi_drv.h"
#include "spi_tpm.h"

#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_wrap.h"
static WOLFTPM2_DEV wolftpm_dev;

#define UART1 (0x40011000)
#define UART2 (0x40014400)

#ifndef APP_UART
#define APP_UART UART1
#endif
#define UART_SR       (*(volatile uint32_t *)(APP_UART))
#define UART_DR       (*(volatile uint32_t *)(APP_UART + 0x04))
#define UART_BRR      (*(volatile uint32_t *)(APP_UART + 0x08))
#define UART_CR1      (*(volatile uint32_t *)(APP_UART + 0x0c))
#define UART_CR2      (*(volatile uint32_t *)(APP_UART + 0x10))

#define UART_CR1_UART_ENABLE    (1 << 13)
#define UART_CR1_SYMBOL_LEN     (1 << 12)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_SR_TX_EMPTY        (1 << 7)
#define UART_SR_RX_NOTEMPTY     (1 << 5)


#define CLOCK_SPEED (168000000)

#define APB2_CLOCK_ER       (*(volatile uint32_t *)(0x40023844))
#define UART1_APB2_CLOCK_ER (1 << 4)
#define UART2_APB2_CLOCK_ER (1 << 17)

#define AHB1_CLOCK_ER       (*(volatile uint32_t *)(0x40023830))
#define GPIOA_AHB1_CLOCK_ER (1 << 0)
#define GPIOB_AHB1_CLOCK_ER (1 << 1)

#ifndef GPIOA_BASE
#define GPIOA_BASE  0x48000000
#endif
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))
#ifndef GPIOB_BASE
#define GPIOB_BASE  0x48000400
#endif
#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

#define UART_PIN_AF 7
#define UART1_RX_PIN 7 /* PB7 */
#define UART1_TX_PIN 6 /* PB6 */
#define UART2_RX_PIN 3 /* PA3 */
#define UART2_TX_PIN 2 /* PA2 */

#define MSGSIZE 16
#define PAGESIZE (256)
static uint8_t page[PAGESIZE];
static const char ERR='!';
static const char START='*';
static const char UPDATE='U';
static const char ACK='#';
static uint8_t msg[MSGSIZE];
static const char startString[]="App started";
static const char TPMfailString[]="tpm_init failed";
static const char TPMpcrString[]="Measured Boot PCR is = ";
static const char HEX [16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

volatile uint32_t time_elapsed = 0; /* Used for PWM on LED */

void uart_write(const char c)
{
    uint32_t reg;
    do {
        reg = UART_SR;
    } while ((reg & UART_SR_TX_EMPTY) == 0);
    UART_DR = c;
}

void uart_write_hex(const char c)
{
    uart_write(HEX[(c >> 4) & 0x0F]);
    uart_write(HEX[c & 0x0F]);
}

static void uart_pins_setup(void)
{
    uint32_t reg;
#if APP_UART == UART1
    AHB1_CLOCK_ER |= GPIOB_AHB1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOB_MODE & ~ (0x03 << (UART1_RX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOB_MODE & ~ (0x03 << (UART1_TX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_TX_PIN * 2));

    /* Alternate function: use low pins (6 and 7) */
    reg = GPIOB_AFL & ~(0xf << ((UART1_TX_PIN) * 4));
    GPIOB_AFL = reg | (UART_PIN_AF << ((UART1_TX_PIN) * 4));
    reg = GPIOB_AFL & ~(0xf << ((UART1_RX_PIN) * 4));
    GPIOB_AFL = reg | (UART_PIN_AF << ((UART1_RX_PIN) * 4));
#elif APP_UART == UART2
    AHB1_CLOCK_ER |= GPIOA_AHB1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOA_MODE & ~ (0x03 << (UART2_RX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART2_RX_PIN * 2));
    reg = GPIOA_MODE & ~ (0x03 << (UART2_TX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART2_TX_PIN * 2));

    /* Alternate function: use low pins (6 and 7) */
    reg = GPIOA_AFL & ~(0xf << ((UART2_TX_PIN) * 4));
    GPIOA_AFL = reg | (UART_PIN_AF << ((UART2_TX_PIN) * 4));
    reg = GPIOA_AFL & ~(0xf << ((UART2_RX_PIN) * 4));
    GPIOA_AFL = reg | (UART_PIN_AF << ((UART2_RX_PIN) * 4));
#else
    #error Please configure the UART pins
#endif
}

int uart_setup(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF7 */
    uart_pins_setup();
    /* Turn on the device */
#if APP_UART == UART1
    APB2_CLOCK_ER |= UART1_APB2_CLOCK_ER;
#elif APP_UART == UART2
    APB2_CLOCK_ER |= UART2_APB2_CLOCK_ER;
#endif

    /* Configure for TX + RX */
    UART_CR1 |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE);

    /* Configure clock */
    UART_BRR =  CLOCK_SPEED / bitrate;

    /* Configure data bits */
    if (data == 8)
        UART_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
        case 'E':
            UART_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART_CR2 & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART_CR2 = reg & (2 << 12);
    else
        UART_CR2 = reg;

    /* Turn on uart */
    UART_CR1 |= UART_CR1_UART_ENABLE;

    return 0;
}

char uart_read(void)
{
    char c;
    volatile uint32_t reg;
    do {
        reg = UART_SR;
    } while ((reg & UART_SR_RX_NOTEMPTY) == 0);
    c = (char)(UART_DR & 0xff);
    return c;
}

static void ack(uint32_t _off)
{
    uint8_t *off = (uint8_t *)(&_off);
    int i;
    uart_write(ACK);
    for (i = 0; i < 4; i++) {
        uart_write(off[i]);
    }
}

static int check(uint8_t *pkt, int size)
{
    int i;
    uint16_t c = 0;
    uint16_t c_rx = *((uint16_t *)(pkt + 2));
    uint16_t *p = (uint16_t *)(pkt + 4);
    for (i = 0; i < ((size - 4) >> 1); i++)
        c += p[i];
    if (c == c_rx)
        return 0;
    return -1;
}

static int app_tpm2_IoCb(TPM2_CTX* ctx, const byte* txBuf, byte* rxBuf,
    word16 xferSz, void* userCtx)
{
    (void)userCtx;
    (void)ctx;
    word16 i;

    spi_cs_on(SPI_CS_TPM);

    XMEMSET(rxBuf, 0, xferSz);
    for (i = 0; i < xferSz; i++)
    {
        spi_write(txBuf[i]);
        rxBuf[i] = spi_read();
    }
    spi_cs_off(SPI_CS_TPM);

    /*
    printf("\r\nSPI TX: ");
    printbin(txBuf, xferSz);
    printf("SPI RX: ");
    printbin(rxBuf, xferSz);
    printf("\r\n");
    */

    return 0;
}

static int app_tpm2_init(void)
{
    int rc;
    WOLFTPM2_CAPS caps;

    spi_init(0,0);

    /* Init the TPM2 device */
    rc = wolfTPM2_Init(&wolftpm_dev, app_tpm2_IoCb, NULL);
    if (rc != 0)  {
        return rc;
    }

    /* Get device capabilities + options */
    rc = wolfTPM2_GetCapabilities(&wolftpm_dev, &caps);
    if (rc != 0)  {
        return rc;
    }

    return 0;
}

/* Reads out the TPM measurement created by wolfBoot */
static int read_measured_boot(uint8_t* digest)
{
    int rc;
    PCR_Read_In pcrReadCmd;
    PCR_Read_Out pcrReadResp;

    XMEMSET(&pcrReadCmd, 0, sizeof(pcrReadCmd));
    TPM2_SetupPCRSel(&pcrReadCmd.pcrSelectionIn, TPM_ALG_SHA256, WOLFBOOT_MEASURED_PCR_A);
    rc = TPM2_PCR_Read(&pcrReadCmd, &pcrReadResp);
    if (rc == TPM_RC_SUCCESS) {
        XMEMCPY(digest, pcrReadResp.pcrValues.digests[0].buffer,
                pcrReadResp.pcrValues.digests[0].size);
        rc = 0;
    }

    return rc;
}

void main(void)
{
    uint32_t tlen = 0;
    volatile uint32_t recv_seq;
    uint32_t r_total = 0;
    uint32_t tot_len = 0;
    uint32_t next_seq = 0;
    uint32_t version = 0;
    uint8_t *v_array = (uint8_t *)&version;
    uint8_t boot_measurement[WOLFBOOT_SHA_DIGEST_SIZE];
    int i;
    memset(page, 0xFF, PAGESIZE);
    boot_led_on();
    flash_set_waitstates();
    clock_config();
    led_pwm_setup();
    pwm_init(CPU_FREQ, 0);

    /* Dim the led by altering the PWM duty-cicle
     * in isr_tim2 (timer.c)
     *
     * Every 50ms, the duty cycle of the PWM connected
     * to the blue led increases/decreases making a pulse
     * effect.
     */
    timer_init(CPU_FREQ, 1, 50);
    uart_setup(115200, 8, 'N', 1);
    memset(page, 0xFF, PAGESIZE);
    asm volatile ("cpsie i");

    while(time_elapsed == 0)
        WFI();


    hal_flash_unlock();
    version = wolfBoot_current_firmware_version();
    if ((version & 0x01) == 0)
        wolfBoot_success();
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key("0123456789abcdef0123456789abcdef", 32);
#endif

    for(i=0; i < sizeof(startString); i++) {
        uart_write(startString[i++]);
    }

    uart_write(START);
    for (i = 3; i >= 0; i--) {
        uart_write(v_array[i]);
    }

    if(app_tpm2_init() != 0) {
        for(i=0; i < sizeof(TPMfailString); i++) {
            uart_write(TPMfailString[i]);
        }
    }

    if(read_measured_boot(boot_measurement) == 0) {
        for(i = 0; i < sizeof(TPMpcrString); i++) {
            uart_write(TPMpcrString[i]);
        }
        /* Print the digest of the measurement */
        for(i=0; i < sizeof(boot_measurement); i++) {
            uart_write_hex(boot_measurement[i]);
        }
        /* For better view on the UART terminal */
        uart_write('\n');
        uart_write('\r');
    }

    while (1) {
        r_total = 0;
        do {
            while(r_total < 2) {
                msg[r_total++] = uart_read();
                if ((r_total == 2) && ((msg[0] != 0xA5) || msg[1] != 0x5A)) {
                    r_total = 0;
                    continue;
                }
            }
            msg[r_total++] = uart_read();
            if ((tot_len == 0) && r_total == 2 + sizeof(uint32_t))
                break;
            if ((r_total > 8)  && (tot_len <= ((r_total - 8) + next_seq)))
                break;
        } while (r_total < MSGSIZE);
        if (tot_len == 0)  {
            tlen = msg[2] + (msg[3] << 8) + (msg[4] << 16) + (msg[5] << 24);
            if (tlen > WOLFBOOT_PARTITION_SIZE - 8) {
                uart_write(ERR);
                uart_write(ERR);
                uart_write(ERR);
                uart_write(ERR);
                uart_write(START);
                recv_seq = 0;
                tot_len = 0;
                continue;
            }
            tot_len = tlen;
            ack(0);
            continue;
        }
        if (check(msg, r_total) < 0) {
            ack(next_seq);
            continue;
        }
        recv_seq = msg[4] + (msg[5] << 8) + (msg[6] << 16) + (msg[7] << 24);
        if (recv_seq == next_seq)
        {
            int psize = r_total - 8;
            int page_idx = recv_seq % PAGESIZE;
            memcpy(&page[recv_seq % PAGESIZE], msg + 8, psize);
            page_idx += psize;
            if ((page_idx == PAGESIZE) || (next_seq + psize >= tot_len)) {
                uint32_t dst = (WOLFBOOT_PARTITION_UPDATE_ADDRESS + recv_seq + psize) - page_idx;
                if ((dst % WOLFBOOT_SECTOR_SIZE) == 0) {
                    hal_flash_erase(dst, WOLFBOOT_SECTOR_SIZE);
                }
                hal_flash_write(dst, page, PAGESIZE);
                memset(page, 0xFF, PAGESIZE);
            }
            next_seq += psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            /* Update complete */
            spi_flash_probe();
            wolfBoot_update_trigger();
            spi_release();
            hal_flash_lock();
            break;
        }
    }
    /* Wait for reboot */
    while(1)
        ;
}

