/* 
 * picotcp driver for Freescale Kinetis boards
 *
 * Copyright (C) 2019 wolfSSL
 * Author: Daniele Lacamera <daniele@wolfssl.com>
 * License: GPL
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 *
 *
 * Part of this code previously licensed as:
 *
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2018 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <stdlib.h>
#include "board.h"
#include "fsl_enet.h"
#include "fsl_phy.h"
#include "pin_mux.h"
#include "fsl_common.h"
#include "board.h"
#include "pico_stack.h"
#include "pico_device.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "pico_enet_kinetis.h"
#include "fsl_enet.h"
#include "fsl_phy.h"

/* ENET base address */
#define EXAMPLE_ENET ENET
#define EXAMPLE_PHY 0x00U
#define CORE_CLK_FREQ CLOCK_GetFreq(kCLOCK_CoreSysClk)
#define ENET_RXBD_NUM (4)
#define ENET_TXBD_NUM (4)
#define ENET_RXBUFF_SIZE (ENET_FRAME_MAX_FRAMELEN)
#define ENET_TXBUFF_SIZE (ENET_FRAME_MAX_FRAMELEN)
#define ENET_DATA_LENGTH (1500)
#define ENET_TRANSMIT_DATA_NUM (20)
#ifndef APP_ENET_BUFF_ALIGNMENT
#define APP_ENET_BUFF_ALIGNMENT ENET_BUFF_ALIGNMENT
#endif

AT_NONCACHEABLE_SECTION_ALIGN(enet_rx_bd_struct_t g_rxBuffDescrip[ENET_RXBD_NUM], ENET_BUFF_ALIGNMENT);
AT_NONCACHEABLE_SECTION_ALIGN(enet_tx_bd_struct_t g_txBuffDescrip[ENET_TXBD_NUM], ENET_BUFF_ALIGNMENT);

SDK_ALIGN(uint8_t g_rxDataBuff[ENET_RXBD_NUM][SDK_SIZEALIGN(ENET_RXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT)],
          APP_ENET_BUFF_ALIGNMENT);
SDK_ALIGN(uint8_t g_txDataBuff[ENET_TXBD_NUM][SDK_SIZEALIGN(ENET_TXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT)],
          APP_ENET_BUFF_ALIGNMENT);

enet_handle_t g_handle;
static uint8_t g_frame[ENET_DATA_LENGTH + 14];

uint8_t g_macAddr[6] = {0xd4, 0xbe, 0xd9, 0x45, 0x22, 0x60};

struct pico_device_enet {
    struct pico_device dev;
    ENET_Type *base;
};

static void ENET_BuildBroadCastFrame(void)
{
    uint32_t count = 0;
    uint32_t length = ENET_DATA_LENGTH - 14;

    for (count = 0; count < 6U; count++)
    {
        g_frame[count] = 0xFFU;
    }
    memcpy(&g_frame[6], &g_macAddr[0], 6U);
    g_frame[12] = (length >> 8) & 0xFFU;
    g_frame[13] = length & 0xFFU;

    for (count = 0; count < length; count++)
    {
        g_frame[count + 14] = count % 0xFFU;
    }
}


static int enet_driver_init(struct pico_device_enet *enet)
{
    enet_config_t config;
    status_t status;
    bool link = false;
    uint32_t sysClock;
    phy_speed_t speed;
    phy_duplex_t duplex;

    /* prepare the buffer configuration. */
    enet_buffer_config_t buffConfig[] = {
        {
            ENET_RXBD_NUM,
            ENET_TXBD_NUM,
            SDK_SIZEALIGN(ENET_RXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT),
            SDK_SIZEALIGN(ENET_TXBUFF_SIZE, APP_ENET_BUFF_ALIGNMENT),
            &g_rxBuffDescrip[0],
            &g_txBuffDescrip[0],
            &g_rxDataBuff[0][0],
            &g_txDataBuff[0][0],
        }
    };

    /* Get default configuration. */
    /*
     * config.miiMode = kENET_RmiiMode;
     * config.miiSpeed = kENET_MiiSpeed100M;
     * config.miiDuplex = kENET_MiiFullDuplex;
     * config.rxMaxFrameLen = ENET_FRAME_MAX_FRAMELEN;
     */
    ENET_GetDefaultConfig(&config);

    /* Set SMI to get PHY link status. */
    sysClock = CORE_CLK_FREQ;
    status = PHY_Init(EXAMPLE_ENET, EXAMPLE_PHY, sysClock);
    while (status != kStatus_Success)
    {
        status = PHY_Init(EXAMPLE_ENET, EXAMPLE_PHY, sysClock);
    }

    PHY_GetLinkStatus(EXAMPLE_ENET, EXAMPLE_PHY, &link);
    if (link)
    {
        /* Get the actual PHY link speed. */
        PHY_GetLinkSpeedDuplex(EXAMPLE_ENET, EXAMPLE_PHY, &speed, &duplex);
        /* Change the MII speed and duplex for actual link status. */
        config.miiSpeed = (enet_mii_speed_t)speed;
        config.miiDuplex = (enet_mii_duplex_t)duplex;
    }

    ENET_Init(EXAMPLE_ENET, &g_handle, &config, &buffConfig[0], &g_macAddr[0], sysClock);
    ENET_ActiveRead(EXAMPLE_ENET);

    return 0;
}

void pico_enet_destroy(struct pico_device *enet)
{
    
}

static int enet_poll(struct pico_device *dev, int loop_score)
{
    struct pico_device_enet *enet = (struct pico_device_enet *)dev;
    uint32_t size;
    while(loop_score > 0) {
        if (ENET_GetRxFrameSize(&g_handle, &size) == kStatus_ENET_RxFrameEmpty)
            return loop_score;
        ENET_ReadFrame(enet->base, &g_handle, g_frame, size);
        pico_stack_recv(dev, g_frame, size);
        loop_score--;
    }
    return loop_score;
}

static int enet_send(struct pico_device *dev, void *buf, int len)
{
    struct pico_device_enet *enet = (struct pico_device_enet *)dev;
    if (ENET_SendFrame(enet->base, &g_handle, buf, len) != kStatus_ENET_TxFrameBusy)
        return len;
    return 0;
}

struct pico_device *pico_enet_create(char *name)
{
    struct pico_device_enet *enet = PICO_ZALLOC(sizeof(struct pico_device_enet));
    
    if( 0 != pico_device_init((struct pico_device *)enet, name, g_macAddr)) {
        dbg("Tap init failed.\n");
        pico_enet_destroy((struct pico_device *)enet);
        return NULL;
    }
    enet->base = ENET;
    enet->dev.send = enet_send;
    enet->dev.poll = enet_poll;
#ifdef PICO_SUPPORT_TICKLESS
    enet->dev.wfi = enet_WFI;
#endif
    enet->dev.destroy = pico_enet_destroy;
    enet_driver_init(enet);
    dbg("Device %s created.\n", enet->dev.name);
#ifdef PICO_SUPPORT_MULTICAST
    ENET_AcceptAllMulticast(enet->base);
#endif
    NVIC_EnableIRQ(ENET_Receive_IRQn);
    NVIC_EnableIRQ(ENET_Transmit_IRQn);
    return (struct pico_device *)enet;
}



