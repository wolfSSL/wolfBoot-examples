/* ble_svc_gatt.h
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
#ifndef H_BLE_SVC_GATT_
#define H_BLE_SVC_GATT_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;

#define BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16     0x2a05

void ble_svc_gatt_changed(uint16_t start_handle, uint16_t end_handle);
void ble_svc_gatt_init(void);

#ifdef __cplusplus
}
#endif

#endif
