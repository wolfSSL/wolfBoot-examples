/* system.c
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
#ifdef PLATFORM_stm32f4
#include <stdint.h>
#include "system.h"

/*** FLASH ***/
#define FLASH_BASE (0x40023C00)
#define FLASH_ACR  (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_ACR_ENABLE_DATA_CACHE (1 << 10)
#define FLASH_ACR_ENABLE_INST_CACHE (1 << 9)

/*** RCC ***/

#define RCC_BASE (0x40023800)
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))

#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 1)
#define RCC_CR_HSION                (1 << 0)

#define RCC_CFGR_SW_HSI             0x0
#define RCC_CFGR_SW_HSE             0x1
#define RCC_CFGR_SW_PLL             0x2


#define RCC_PLLCFGR_PLLSRC          (1 << 22)

#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2    8
#define RCC_PRESCALER_DIV_4    9



#define PLLM 8
#define PLLN 336
#define PLLP 2
#define PLLQ 7
#define PLLR 0

void flash_set_waitstates(void)
{
    FLASH_ACR |= 5 | FLASH_ACR_ENABLE_DATA_CACHE | FLASH_ACR_ENABLE_INST_CACHE;
}


void clock_config(void)
{
    uint32_t reg32;
    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select HSI as SYSCLK source. */

    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Enable external high-speed oscillator 8MHz. */
    RCC_CR |= RCC_CR_HSEON;
    DMB();
    while ((RCC_CR & RCC_CR_HSERDY) == 0) {};

    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF0);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_NONE << 4));
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x1C00);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_2 << 10));
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x07 << 13);
    RCC_CFGR = (reg32 | (RCC_PRESCALER_DIV_4 << 13));
    DMB();

    /* Set PLL config */
    reg32 = RCC_PLLCFGR;
    reg32 &= ~(PLL_FULL_MASK);
    RCC_PLLCFGR = reg32 | RCC_PLLCFGR_PLLSRC | PLLM |
        (PLLN << 6) | (((PLLP >> 1) - 1) << 16) |
        (PLLQ << 24);
    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_CR |= RCC_CR_PLLON;
    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* Disable internal high-speed oscillator. */
    RCC_CR &= ~RCC_CR_HSION;
}

#endif /** PLATFORM_stm32f4 **/


#ifdef PLATFORM_stm32g0
#include <stdint.h>
#include "system.h"

/*** RCC ***/

#define RCC_BASE (0x40021000) /*PERIPH_BASE + 0x00020000UL + 0x00001000UL */  //RM0444 - 5.4.1
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))  //RM0444 - 5.4.1
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x0C))  //RM0444 - 5.4.4
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x08))  //RM0444 - 5.4.3


#define RCC_CR_PLLRDY               (1 << 25)
#define RCC_CR_PLLON                (1 << 24)
#define RCC_CR_HSERDY               (1 << 17)
#define RCC_CR_HSEON                (1 << 16)
#define RCC_CR_HSIRDY               (1 << 10)
#define RCC_CR_HSION                (1 << 8)

#define RCC_CFGR_SW_HSI             0x0
#define RCC_CFGR_SW_HSE             0x1
#define RCC_CFGR_SW_PLL             0x2


#define RCC_PLLCFGR_PLLR_EN       (1 << 28) //RM0444 - 5.4.3

#define RCC_PLLCFGR_PLLSRC_NONE   0
#define RCC_PLLCFGR_PLLSRC_HSI16  2
#define RCC_PLLCFGR_PLLSRC_HSE    3




/*** APB PRESCALER ***/ // TODO - confirm
#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2    8
#define RCC_PRESCALER_DIV_4    9
#define PLL_FULL_MASK (0x7F037FFF) // TODO

/*** FLASH ***/
#define APB1_CLOCK_ER           (*(volatile uint32_t *)(0x4002103C)) //RM0444 - 5.4.14 - RCC_APBENR1
#define APB1_CLOCK_RST          (*(volatile uint32_t *)(0x4002102C)) //RM0444 - 5.4.10 - RCC_APBRSTR1
#define TIM2_APB1_CLOCK_ER_VAL     (1 << 0)
#define PWR_APB1_CLOCK_ER_VAL   (1 << 28)

#define APB2_CLOCK_ER           (*(volatile uint32_t *)(0x40021040))  //RM0444 - 5.4.15 - RCC_APBENR2
#define APB2_CLOCK_RST          (*(volatile uint32_t *)(0x40021030))  //RM0444 - 5.4.11 - RCC_APBRSTR2
#define SYSCFG_APB2_CLOCK_ER    (1 << 0) //RM0444 - 5.4.15 - RCC_APBENR2 - SYSCFGEN

#define FLASH_BASE          (0x40022000)  /*FLASH_R_BASE = 0x40000000UL + 0x00020000UL + 0x00002000UL */
#define FLASH_ACR           (*(volatile uint32_t *)(FLASH_BASE + 0x00)) //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_KEYR          (*(volatile uint32_t *)(FLASH_BASE + 0x08)) //RM0444 - 3.7.2 - FLASH_KEYR
#define FLASH_SR            (*(volatile uint32_t *)(FLASH_BASE + 0x10)) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_CR            (*(volatile uint32_t *)(FLASH_BASE + 0x14)) //RM0444 - 3.7.5 - FLASH_CR

/* Register values */
#define FLASH_ACR_DBG_SWEN                    (1 << 18) //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_ACR_EMPTY                       (1 << 16) //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_ACR_RESET_INST_CACHE            (1 << 11) //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_ACR_ENABLE_INST_CACHE           (1 << 9)  //RM0444 - 3.7.1 - FLASH_ACR
#define FLASH_ACR_ENABLE_PRFT                 (1 << 8) //RM0444 - 3.7.1 - FLASH_ACR


#define RCC_PRESCALER_DIV_NONE 0
#define RCC_PRESCALER_DIV_2    8
#define RCC_PRESCALER_DIV_4    9



#define PLLM 4
#define PLLN 70
#define PLLP 10
#define PLLQ 5
#define PLLR 5

void flash_set_waitstates(void)
{
    FLASH_ACR |=  2 | FLASH_ACR_RESET_INST_CACHE | FLASH_ACR_ENABLE_INST_CACHE;
}


void clock_config(void)
{
    uint32_t reg32;
    uint32_t cpu_freq, plln, pllm, pllq, pllp, pllr, hpre, ppre, flash_waitstates;

    /* Enable Power controller - APB1 */
    APB1_CLOCK_ER |= PWR_APB1_CLOCK_ER_VAL;

    /* Select clock parameters (CPU Speed = 56MHz) */
    cpu_freq = 56000000;
    pllm = 4;
    plln = 70;
    pllp = 10;
    pllq = 5;
    pllr = 5;
    hpre = RCC_PRESCALER_DIV_NONE;
    ppre = RCC_PRESCALER_DIV_NONE;
    flash_waitstates = 2; // FLASH_LATENCY_2 > FLASH_ACR_LATENCY_1 = 0x2UL

    flash_set_waitstates();

    /* Enable internal high-speed oscillator. */
    RCC_CR |= RCC_CR_HSION;
    DMB();
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};

    /* Select PLLCLK as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();


    /*
     * Set prescalers for AHB, ADC, ABP1, ABP2.
     */
    reg32 = RCC_CFGR;
    reg32 &= ~(0xF0); //don't change bits [0-3] that were previously set
    RCC_CFGR = (reg32 | (hpre << 8)); //RM0444 - 5.4.3 - RCC_CFGR
    DMB();
    reg32 = RCC_CFGR;
    reg32 &= ~(0x1C00); //don't change bits [0-14]
    RCC_CFGR = (reg32 | (ppre << 12));  //RM0444 - 5.4.3 - RCC_CFGR
    DMB();

    /* Set PLL config */
    reg32 = RCC_PLLCFGR;
    reg32 &= ~(PLL_FULL_MASK); // TODO??
    reg32 |= RCC_PLLCFGR_PLLSRC_HSI16;
    reg32 |= ((PLLM - 1) << 4);
    reg32 |= PLLN << 8;
    reg32 |= ((PLLP - 1) << 17);
    reg32 |= ((PLLQ - 1) << 25);
    reg32 |= ((PLLR - 1) << 29);
    RCC_PLLCFGR = reg32;

    DMB();
    /* Enable PLL oscillator and wait for it to stabilize. */
    RCC_PLLCFGR |= RCC_PLLCFGR_PLLR_EN;
    RCC_CR |= RCC_CR_PLLON;

    DMB();
    while ((RCC_CR & RCC_CR_PLLRDY) == 0) {};

    /* Select PLL as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_PLL);
    DMB();

    /* Wait for PLL clock to be selected. */
    while ((RCC_CFGR & ((1 << 1) | (1 << 0))) != RCC_CFGR_SW_PLL) {};

    /* SYSCFG, COMP and VREFBUF clock enable */
    APB2_CLOCK_ER|=SYSCFG_APB2_CLOCK_ER;
}

#endif /** PLATFORM_stm32f4 **/
