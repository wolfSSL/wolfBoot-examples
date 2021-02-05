/* ble_svc_gap.h
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
#ifndef H_BLE_SVC_GAP_
#define H_BLE_SVC_GAP_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SVC_GAP_UUID16                                  0x1800
#define BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME                  0x2a00
#define BLE_SVC_GAP_CHR_UUID16_APPEARANCE                   0x2a01
#define BLE_SVC_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS      0x2a04
#define BLE_SVC_GAP_CHR_UUID16_CENTRAL_ADDRESS_RESOLUTION   0x2aa6

#define BLE_SVC_GAP_APPEARANCE_GEN_UNKNOWN                         0
#define BLE_SVC_GAP_APPEARANCE_GEN_COMPUTER                        128
#define BLE_SVC_GAP_APPEARANCE_CYC_SPEED_AND_CADENCE_SENSOR        1157

typedef void (ble_svc_gap_chr_changed_fn) (uint16_t uuid);

void ble_svc_gap_set_chr_changed_cb(ble_svc_gap_chr_changed_fn *cb);

const char *ble_svc_gap_device_name(void);
int ble_svc_gap_device_name_set(const char *name);
uint16_t ble_svc_gap_device_appearance(void);
int ble_svc_gap_device_appearance_set(uint16_t appearance);

void ble_svc_gap_init(void);

#ifdef __cplusplus
}
#endif

#endif
