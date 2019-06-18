/* stm32g0.c
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

#include <stdint.h>
#include <image.h>


/* STM32 G0 register configuration */

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
//#define DMB() __asm volatile ("dmb")  //TODO

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

#define FLASH_SR_CFGBSY                       (1 << 18) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_BSY1                         (1 << 16) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_OPTVERR                      (1 << 15) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_RDERR                        (1 << 14) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_FASTERR                      (1 << 9)  //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_MISERR                       (1 << 8)  //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_PGSERR                       (1 << 7)  //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_SIZERR                       (1 << 6)  //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_PGAERR                       (1 << 5) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_WRPERR                       (1 << 4) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_PROGERR                      (1 << 3) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_OPERR                        (1 << 1) //RM0444 - 3.7.4 - FLASH_SR
#define FLASH_SR_EOP                          (1 << 0) //RM0444 - 3.7.4 - FLASH_SR

#define FLASH_CR_LOCK                         (1 << 31) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_OPTLOCK                      (1 << 30) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_SECPROT                      (1 << 28) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_OBL_LAUNCH                   (1 << 27) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_RDERRIE                      (1 << 26) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_ERRIE                        (1 << 25) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_EOPIE                        (1 << 24) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_FSTPG                        (1 << 18) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_OPTSTRT                      (1 << 17) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_STRT                         (1 << 16) //RM0444 - 3.7.5 - FLASH_CR

#define FLASH_CR_MER1                         (1 << 2) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_PER                          (1 << 1) //RM0444 - 3.7.5 - FLASH_CR
#define FLASH_CR_PG                           (1 << 0) //RM0444 - 3.7.5 - FLASH_CR

#define FLASH_CR_PNB_SHIFT                     3     //RM0444 - 3.7.5 - FLASH_CR - PNB bits 8:3
#define FLASH_CR_PNB_MASK                      0x3f   //RM0444 - 3.7.5 - FLASH_CR  - PNB bits 8:3 - 6 bits

#define FLASH_KEY1                            (0x45670123)
#define FLASH_KEY2                            (0xCDEF89AB)


/* FLASH Geometry */
#define FLASH_SECTOR_0  0x0000000 /* 2 Kb   */
#define FLASH_SECTOR_1  0x0000800 /* 2 Kb   */
#define FLASH_SECTOR_2  0x0001000 /* 2 Kb   */
#define FLASH_SECTOR_3  0x0001800 /* 2 Kb   */
#define FLASH_SECTOR_4  0x0002000 /* 2 Kb   */
#define FLASH_SECTOR_5  0x0002800 /* 2 Kb   */
#define FLASH_SECTOR_6  0x0003000 /* 2 Kb   */
#define FLASH_SECTOR_7  0x0003800 /* 2 Kb   */
#define FLASH_SECTOR_8  0x0004000 /* 2 Kb   */
#define FLASH_SECTOR_9  0x0004800 /* 2 Kb   */
#define FLASH_SECTOR_10 0x0005000 /* 2 Kb   */
#define FLASH_SECTOR_11  0x0005800 /* 2 Kb   */
#define FLASH_SECTOR_12  0x0006000 /* 2 Kb   */
#define FLASH_SECTOR_13  0x0006800 /* 2 Kb   */
#define FLASH_SECTOR_14  0x0007000 /* 2 Kb   */
#define FLASH_SECTOR_15  0x0007800 /* 2 Kb   */
#define FLASH_SECTOR_16  0x0008000 /* 2 Kb   */
#define FLASH_SECTOR_17  0x0008800 /* 2 Kb   */
#define FLASH_SECTOR_18  0x0009000 /* 2 Kb   */
#define FLASH_SECTOR_19  0x0009800 /* 2 Kb   */
#define FLASH_SECTOR_20  0x000A000 /* 2 Kb   */
#define FLASH_SECTOR_21  0x000A800 /* 2 Kb   */
#define FLASH_SECTOR_22  0x000B000 /* 2 Kb   */
#define FLASH_SECTOR_23  0x000B800 /* 2 Kb   */
#define FLASH_SECTOR_24  0x000C000 /* 2 Kb   */
#define FLASH_SECTOR_25  0x000C800 /* 2 Kb   */
#define FLASH_SECTOR_26  0x000D000 /* 2 Kb   */
#define FLASH_SECTOR_27  0x000D800 /* 2 Kb   */
#define FLASH_SECTOR_28  0x000E000 /* 2 Kb   */
#define FLASH_SECTOR_29  0x000E800 /* 2 Kb   */
#define FLASH_SECTOR_30  0x000F000 /* 2 Kb   */
#define FLASH_SECTOR_31  0x000F800 /* 2 Kb   */
#define FLASH_SECTOR_32  0x0010000 /* 2 Kb   */
#define FLASH_SECTOR_33  0x0010800 /* 2 Kb   */
#define FLASH_SECTOR_34  0x0011000 /* 2 Kb   */
#define FLASH_SECTOR_35  0x0011800 /* 2 Kb   */
#define FLASH_SECTOR_36  0x0012000 /* 2 Kb   */
#define FLASH_SECTOR_37  0x0012800 /* 2 Kb   */
#define FLASH_SECTOR_38  0x0013000 /* 2 Kb   */
#define FLASH_SECTOR_39  0x0013800 /* 2 Kb   */
#define FLASH_SECTOR_40  0x0014000 /* 2 Kb   */
#define FLASH_SECTOR_41  0x0014800 /* 2 Kb   */
#define FLASH_SECTOR_42  0x0015000 /* 2 Kb   */
#define FLASH_SECTOR_43  0x0015800 /* 2 Kb   */
#define FLASH_SECTOR_44  0x0016000 /* 2 Kb   */
#define FLASH_SECTOR_45  0x0016800 /* 2 Kb   */
#define FLASH_SECTOR_46  0x0017000 /* 2 Kb   */
#define FLASH_SECTOR_47  0x0017800 /* 2 Kb   */
#define FLASH_SECTOR_48  0x0018000 /* 2 Kb   */
#define FLASH_SECTOR_49  0x0018800 /* 2 Kb   */
#define FLASH_SECTOR_50  0x0019000 /* 2 Kb   */
#define FLASH_SECTOR_51  0x0019800 /* 2 Kb   */
#define FLASH_SECTOR_52  0x001A000 /* 2 Kb   */
#define FLASH_SECTOR_53  0x001A800 /* 2 Kb   */
#define FLASH_SECTOR_54  0x001B000 /* 2 Kb   */
#define FLASH_SECTOR_55  0x001B800 /* 2 Kb   */
#define FLASH_SECTOR_56  0x001C000 /* 2 Kb   */
#define FLASH_SECTOR_57  0x001C800 /* 2 Kb   */
#define FLASH_SECTOR_58  0x001D000 /* 2 Kb   */
#define FLASH_SECTOR_59  0x001D800 /* 2 Kb   */
#define FLASH_SECTOR_60  0x001E000 /* 2 Kb   */
#define FLASH_SECTOR_61  0x001E800 /* 2 Kb   */
#define FLASH_SECTOR_62  0x001F000 /* 2 Kb   */
#define FLASH_SECTOR_63  0x001F800 /* 2 Kb   */
#define FLASH_TOP        0x0200000

#define FLASH_SECTORS 64
const uint32_t flash_sector[FLASH_SECTORS + 1] = {
    FLASH_SECTOR_0,
    FLASH_SECTOR_1,
    FLASH_SECTOR_2,
    FLASH_SECTOR_3,
    FLASH_SECTOR_4,
    FLASH_SECTOR_5,
    FLASH_SECTOR_6,
    FLASH_SECTOR_7,
    FLASH_SECTOR_8,
    FLASH_SECTOR_9,
    FLASH_SECTOR_10,
    FLASH_SECTOR_11,
    FLASH_SECTOR_12,
    FLASH_SECTOR_13,
    FLASH_SECTOR_14,
    FLASH_SECTOR_15,
    FLASH_SECTOR_16,
    FLASH_SECTOR_17,
    FLASH_SECTOR_18,
    FLASH_SECTOR_19,
    FLASH_SECTOR_20,
    FLASH_SECTOR_21,
    FLASH_SECTOR_22,
    FLASH_SECTOR_23,
    FLASH_SECTOR_24,
    FLASH_SECTOR_25,
    FLASH_SECTOR_26,
    FLASH_SECTOR_27,
    FLASH_SECTOR_28,
    FLASH_SECTOR_29,
    FLASH_SECTOR_30,
    FLASH_SECTOR_31,
    FLASH_SECTOR_32,
    FLASH_SECTOR_33,
    FLASH_SECTOR_34,
    FLASH_SECTOR_35,
    FLASH_SECTOR_36,
    FLASH_SECTOR_37,
    FLASH_SECTOR_38,
    FLASH_SECTOR_39,
    FLASH_SECTOR_40,
    FLASH_SECTOR_41,
    FLASH_SECTOR_42,
    FLASH_SECTOR_43,
    FLASH_SECTOR_44,
    FLASH_SECTOR_45,
    FLASH_SECTOR_46,
    FLASH_SECTOR_47,
    FLASH_SECTOR_48,
    FLASH_SECTOR_49,
    FLASH_SECTOR_50,
    FLASH_SECTOR_51,
    FLASH_SECTOR_52,
    FLASH_SECTOR_53,
    FLASH_SECTOR_54,
    FLASH_SECTOR_55,
    FLASH_SECTOR_56,
    FLASH_SECTOR_57,
    FLASH_SECTOR_58,
    FLASH_SECTOR_59,
    FLASH_SECTOR_60,
    FLASH_SECTOR_61,
    FLASH_SECTOR_62,
    FLASH_SECTOR_63,
    FLASH_TOP
};

static void RAMFUNCTION flash_set_waitstates(int waitstates) //ok
{
    FLASH_ACR |=  waitstates | FLASH_ACR_RESET_INST_CACHE | FLASH_ACR_ENABLE_INST_CACHE;
}

static RAMFUNCTION void flash_wait_complete(void) //ok
{
    while ((FLASH_SR & FLASH_SR_BSY1) == FLASH_SR_BSY1)
        ;
}
/*
static void mass_erase(void)
{
    FLASH_CR |= FLASH_CR_MER;
    FLASH_CR |= FLASH_CR_STRT;
    flash_wait_complete();
    FLASH_CR &= ~FLASH_CR_MER;
}
*/

static void RAMFUNCTION flash_clear_errors(void)
{
      FLASH_SR |=   (FLASH_SR_OPERR   | FLASH_SR_PROGERR | FLASH_SR_WRPERR | \
                                         FLASH_SR_PGAERR  | FLASH_SR_SIZERR  | FLASH_SR_PGSERR | \
                                         FLASH_SR_MISERR  | FLASH_SR_FASTERR | \
                                         FLASH_SR_OPTVERR /*| FLASH_SR_ECCC    | FLASH_SR_ECCD*/);

      /*TODO : add ECC error handling*/
}

static void RAMFUNCTION flash_erase_page(uint32_t page)
{
  /* RM0444 - 3.3.7  To erase a page (2 Kbytes), follow the procedure below*/

  /*1. Check that no Flash memory operation is ongoing by checking the BSY1 bit of the FLASH status register (FLASH_SR).*/
  flash_wait_complete();

  /*2. Check and clear all error programming flags due to a previous programming. If not,  PGSERR is set.*/
  flash_clear_errors();

  /*3. Set the PER bit and select the page to erase (PNB) in the FLASH control register  (FLASH_CR).*/
  FLASH_CR |= FLASH_CR_PER;

  uint32_t reg = FLASH_CR & (~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT));
  FLASH_CR = reg | (page & FLASH_CR_PNB_MASK) << FLASH_CR_PNB_SHIFT;

  /*4. Set the STRT bit of the FLASH control register (FLASH_CR).*/
  FLASH_CR |= FLASH_CR_STRT;

  /*5. Wait until the BSY1 bit of the FLASH status register (FLASH_SR) is cleared.*/
  flash_wait_complete();

  /*Reset*/
  FLASH_CR &= ~FLASH_CR_PER;
  FLASH_CR &= ~(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT);

}




int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{

  int i;
  uint32_t len_64;
  uint64_t data_64;


  /*1. Check that no Main Flash memory operation is ongoing by checking the BSY1 bit of the FLASH status register (FLASH_SR).*/
  flash_wait_complete();

  /*2. Check and clear all error programming flags due to a previous programming. If not, PGSERR is set.*/
  flash_clear_errors();

  /*It is only possible to program a double word (2 x 32-bit data) */
  len_64 = len >> 3;

  for (i = 0; i < len_64 ; i++) {

    /*3. Set the PG bit of the FLASH control register (FLASH_CR).*/
    FLASH_CR |= FLASH_CR_PG;

    /*4. Perform the data write operation at the desired memory address, inside Main memory block or OTP area. Only double word (64 bits) can be programmed.*/
    data_64 = (*((uint64_t*)data));

    /* Program first word */
    *(uint32_t *)address = (uint32_t)data_64;

    /* Barrier to ensure programming is performed in 2 steps, in right order(independently of compiler optimization behavior) */
     DMB();

    /* Program second word */
    *(uint32_t *)(address + 4U) = (uint32_t)(data_64 >> 32U);

    /*5. Wait until the BSY1 bit of the FLASH status register (FLASH_SR) is cleared.*/
    flash_wait_complete();

    /*6. Check that EOP flag of the FLASH status register (FLASH_SR) is set (programming operation succeeded), and clear it by software.*/
    while ((FLASH_SR & FLASH_SR_EOP) == FLASH_SR_EOP);
    FLASH_SR &= ~FLASH_SR_EOP;

    /*7. Clear the PG bit of the FLASH control register (FLASH_CR) if there no more programming request anymore.*/
    FLASH_CR &= ~FLASH_CR_PG;
  }

  return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
    FLASH_KEYR = FLASH_KEY1;
    FLASH_KEYR = FLASH_KEY2;
}

void RAMFUNCTION hal_flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int start = -1, end = -1;
    uint32_t end_address;
    int i;
    if (len == 0)
        return -1;
    end_address = address + len - 1;

    if (address < flash_sector[0] || end_address > FLASH_TOP)
        return -1;
    for (i = 0; i < FLASH_SECTORS; i++)
    {
        if ((address >= flash_sector[i]) && (address < flash_sector[i + 1])) {
            start = i;
        }
        if ((end_address >= flash_sector[i]) && (end_address < flash_sector[i + 1])) {
            end = i;
        }
        if (start > 0 && end > 0)
            break;
    }
    if (start < 0 || end < 0)
        return -1;
    for (i = start; i <= end; i++)
        flash_erase_page(i);
    return 0;
}


/*This implementation will setup HSI RC 16 MHz as SYSCLCK source and turn of PLLCLK*/
static void clock_pll_off(void)
{
    uint32_t reg32;

   /* Select HSI as SYSCLK source. */
    reg32 = RCC_CFGR;
    reg32 &= ~((1 << 1) | (1 << 0));
    RCC_CFGR = (reg32 | RCC_CFGR_SW_HSI);
    DMB();

    /* Turn off PLL */
    RCC_CR &= ~RCC_CR_PLLON;
    DMB();
}

/*This implementation will setup HSI RC 16 MHz as PLL Source Mux, PLLCLK as System Clock Source*/
static void clock_pll_on(int powersave)
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

    flash_set_waitstates(flash_waitstates);

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
    reg32 |= ((pllm - 1) << 4);
    reg32 |= plln << 8;
    reg32 |= ((pllp - 1) << 17);
    reg32 |= ((pllq - 1) << 25);
    reg32 |= ((pllr - 1) << 29);
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

void hal_init(void)
{
    clock_pll_on(0);
}


/*GPIOA5*/
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

void led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    RCC_IOPENR |= RCC_IOPENR_GPIOAEN;
    reg = GPIOA_MODE & ~(0x03 << (pin * 2));
    GPIOA_MODE = reg | (1 << (pin * 2)); // general purpose output mode
    reg = GPIOA_PUPD & ~(0x03 << (pin * 2));
    GPIOA_PUPD = reg | (1 << (pin * 2)); // pull-up
    GPIOA_BSRR |= (1 << pin); // set pin
}

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

void hal_prepare_boot(void)
{
#ifdef SPI_FLASH
    spi_release();
#endif
    led_on();
    gpiotoggle(5);
    clock_pll_off();
}

