/* picotcp.c
 *
 * picotcp mutex interface for freeRTOS integration
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "portmacro.h"

#include "pico_stack.h"


void *pico_mutex_init(void)
{
    void *ret;
    ret = xSemaphoreCreateBinary();
    if (ret)
        xSemaphoreGive(ret);
    return ret;
}
void pico_mutex_deinit(void *mutex)
{
    vSemaphoreDelete(mutex);
}

void pico_mutex_lock(void *mutex)
{
    while(xSemaphoreTake(mutex, portMAX_DELAY) == pdFALSE)
        ;

}

int pico_mutex_lock_timeout(void *mutex, int timeout)
{
    int ret = xSemaphoreTake(mutex, timeout);
    if (ret)
        return 0; /* Success */
    else
        return -1; /* Timeout */
}

void pico_mutex_unlock(void *mutex)
{
    xSemaphoreGive(mutex);
}

void pico_mutex_unlock_ISR(void *mutex)
{
    long task_switch_is_needed = 0;
    xSemaphoreGiveFromISR(mutex, &task_switch_is_needed);
}

int rnd_custom_generate_block(uint8_t *b, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        b[i] = (uint8_t)pico_rand();
    }
    return 0;
}
