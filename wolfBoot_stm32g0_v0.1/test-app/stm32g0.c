/* main.c
 *
 * Test bare-metal blinking led application
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "system.h"
#include "timer.h"
#include "led.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"


#ifdef PLATFORM_stm32g0

#define RCC_IOPENR (*(volatile uint32_t *)(0x40021034)) // 40021034
#define RCC_IOPENR_GPIOAEN (1 << 0)

#define GPIOA_BASE 0x50000000
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPE (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x0c))
#define GPIOA_ODR   (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))
#define GPIOA_BRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x28))
#define LED_PIN (5)
#define LED_BOOT_PIN (5)
#define GPIO_OSPEED_100MHZ (0x03)

static void gpiotoggle(uint32_t pin)
{
	if ((GPIOA_ODR & (1 << pin)) != 0x00u)
	  {
	    GPIOA_BRR |= (1 << pin);
	  }
	  else
	  {
	    GPIOA_BSRR |= (1 << pin);
	  }
}


void main(void)
{
    uint32_t pin = 0;
    uint32_t i = 0;

    boot_led_on();
    gpiotoggle(5);
    flash_set_waitstates();
//    clock_config();

    while(1) {
        gpiotoggle(5);
        for (i = 0; i < 800000; i++)  // Wait a bit.
              asm volatile ("nop");
    }
}
#endif /*PLATFORM_stm32g0*/
