/* main.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * wolfBoot fw-update example RIOT application, running on nRF52840.
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
 *
 */

#include <stdio.h>
#include "board.h"
#include "net/ipv6/addr.h"
#include "net/gnrc.h"
#include "net/gnrc/netif.h"
#include "shell.h"
#include "periph/flashpage.h"
#include "thread.h"
#include "periph/uart.h"
#include "periph/gpio.h"
#include "xtimer.h"
#include "ringbuffer.h"
#include "wolfboot/wolfboot.h"
#include "mutex.h"
#include "msg.h"

#ifndef SHELL_BUFSIZE
#define SHELL_BUFSIZE       (128U)
#endif

#define DISPATCHER_PRIO        (THREAD_PRIORITY_MAIN - 1)
#define BT_PRIO                (THREAD_PRIORITY_MAIN - 1)

#define MSGQ_SIZE (4)

#define GPIO_WAKEUP  GPIO_PIN(1,11)


// Globals
//
static mutex_t fwupdate_mutex = MUTEX_INIT;
static kernel_pid_t gatt_srv_pid;
static char gatt_srv_stack[THREAD_STACKSIZE_MAIN];
static msg_t gatt_srv_msgq[MSGQ_SIZE];
static xtimer_t quotes_msg_timer;

#define STDIO_UART_DEV       (UART_UNDEF)


static int cmd_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("GATT services for secure firmware updates\n");
    printf("You are running RIOT on %s.\n", RIOT_BOARD);
    printf("This board features a %s MCU.\n", RIOT_MCU);
    puts("Bootloader info: \n");
    printf(" - Running firmware version %08x\n", (unsigned)wolfBoot_current_firmware_version());
    printf(" - Update firmware version %08x\n", (unsigned)wolfBoot_update_firmware_version());
    puts("\n   UART INFO:");
    printf("     - Available devices:               %i\n", UART_NUMOF);
    if (STDIO_UART_DEV != UART_UNDEF) {
        printf(" - UART used for STDIO (the shell): UART_DEV(%i)\n\n", STDIO_UART_DEV);
    }
    return 0;
}

void *gatt_srv(void*);
static const shell_command_t shell_commands[] = {
    { "info", "device info", cmd_info },
    { NULL, NULL, NULL }
};

int main(void)
{
    /* initialize UART */
    char line_buf[SHELL_BUFSIZE];

    /* initialize GPIO WAKEUP */
    gpio_init(GPIO_WAKEUP, GPIO_OUT);
    gpio_set(GPIO_WAKEUP);

    /* Gatt Server */
    gatt_srv_pid = thread_create(gatt_srv_stack, sizeof(gatt_srv_stack),
            DISPATCHER_PRIO - 1, 0, gatt_srv, NULL, "BLE_gatt");

    /* run the shell */
    shell_run(shell_commands, line_buf, SHELL_BUFSIZE);
    return 0;
}


    
