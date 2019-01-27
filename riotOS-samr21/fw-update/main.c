/* main.c
 *
 * Copyright (C) 2019 wolfSSL Inc.
 *
 * wolfBoot fw-update example RIOT application, running on samR21.
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

#include <stdio.h>
#include "net/ipv6/addr.h"
#include "net/gnrc.h"
#include "net/gnrc/netif.h"
#include "target.h"
#include "shell.h"
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "periph/flashpage.h"

extern void wolfBoot_success(void);
#define MSGSIZE (4 + 4 + 8)
#define PAGESIZE (256)

uint8_t page[PAGESIZE];

static uint32_t next_seq;
static uint32_t tot_len = 0;
const char err[]="!!!!";

static void reboot(void)
{
#   define SCB_AIRCR         (*((volatile uint32_t *)(0xE000ED0C)))
#   define AIRCR_VECTKEY     (0x05FA0000)
#   define SYSRESET          (1 << 2)
    SCB_AIRCR = AIRCR_VECTKEY | SYSRESET;
}

static void ack(uint32_t _off)
{
    uint32_t off = _off;
    uint8_t ack_start = '#';
    
    write(STDOUT_FILENO, &ack_start, 1);
    write(STDOUT_FILENO, &off, 4);
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

static uint8_t msg[MSGSIZE];

static void update_loop(void)
{
    int r;
    uint32_t tlen = 0;
    volatile uint32_t recv_seq;
    uint32_t r_total = 0;
    memset(page, 0xFF, PAGESIZE);
    while (1) {
        r_total = 0;
        do {
            while(r_total < 2) {
                r = read(STDIN_FILENO, msg + r_total, 2 - r_total);
                if (r <= 0)
                    continue;
                r_total += r;
                if ((r_total == 2) && ((msg[0] != 0xA5) || msg[1] != 0x5A)) {
                    r_total = 0;
                    continue;
                }
            }
            r = read(STDIN_FILENO, msg + r_total, MSGSIZE - r_total);
            if (r <= 0)
                continue;
            r_total += r;
            if ((tot_len == 0) && r_total == 2 + sizeof(uint32_t))
                break;
            if ((r_total > 8)  && (tot_len <= ((r_total - 8) + next_seq)))
                break;
        } while (r_total < MSGSIZE);

        if (tot_len == 0)  {
            tlen = msg[2] + (msg[3] << 8) + (msg[4] << 16) + (msg[5] << 24);
            if (tlen > WOLFBOOT_PARTITION_SIZE - 8) {
                write(STDOUT_FILENO, err, 4);
                break;
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
                uint32_t dst = (WOLFBOOT_PARTITION_UPDATE_ADDRESS + recv_seq) / PAGESIZE;
                flashpage_write_and_verify(dst, page);
                memset(page, 0xFF, PAGESIZE);
            }
            next_seq += psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            /* Update complete */
            wolfBoot_update_trigger();
            reboot();
            while(1) 
                ;
        }
    }
}

int main(void)
{
    char x = '#';
    puts("OTA example running on RIOT");
    gnrc_netif_t *netif = NULL;
    while ((netif = gnrc_netif_iter(netif))) {
        ipv6_addr_t ipv6_addrs[GNRC_NETIF_IPV6_ADDRS_NUMOF];
        int res = gnrc_netapi_get(netif->pid, NETOPT_IPV6_ADDR, 0, ipv6_addrs,
                                  sizeof(ipv6_addrs));
        if (res < 0) {
            continue;
        }
        for (unsigned i = 0; i < (unsigned)(res / sizeof(ipv6_addr_t)); i++) {
            char ipv6_addr[IPV6_ADDR_MAX_STR_LEN];
            ipv6_addr_to_str(ipv6_addr, &ipv6_addrs[i], IPV6_ADDR_MAX_STR_LEN);
            printf("IPv6 Link local address: %s\n", ipv6_addr);
            ipv6_addrs[i].u8[0] = 0xFE;
            ipv6_addrs[i].u8[1] = 0xC0;
            ipv6_addrs[i].u8[2] = 0x00;
            ipv6_addrs[i].u8[3] = 0x0a;
            gnrc_netif_ipv6_addr_add(netif, &ipv6_addrs[i], 64, GNRC_NETIF_IPV6_ADDRS_FLAGS_STATE_VALID);
            ipv6_addr_to_str(ipv6_addr, &ipv6_addrs[i], IPV6_ADDR_MAX_STR_LEN);
            printf("IPv6 Site local address: %s\n", ipv6_addr);
        }
    }
    printf("You are running RIOT on %s.\n", RIOT_BOARD);
    printf("This board features a %s MCU.\n", RIOT_MCU);
    printf("Boot address: 0x%08x\n", WOLFBOOT_PARTITION_BOOT_ADDRESS);
    irq_disable();
    wolfBoot_success();
    irq_enable();
    write(STDOUT_FILENO, &x, 1);
    update_loop();
    return 0;
}


    
