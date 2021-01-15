/* gatt_srv.c
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
#include <stdint.h>

#include "assert.h"
#include "event/timeout.h"
#include "nimble_riot.h"
#include "net/bluetil/ad.h"
#include "periph/gpio.h"
#include "timex.h"
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "board.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"


static const char *_device_name = "nRF52_UPDATE";
static const char *_manufacturer_name = "wolfSSL";
static const char *_model_number = "PCA10056";
static const char *_serial_number = "dab1b30a11-ed218-x1";
static const char *_hw_ver = "1";

static event_queue_t _eq;
static event_t _update_evt;
static event_timeout_t _update_timeout_evt;

static uint16_t _conn_handle;
static uint16_t _reboot_val_handle;

static void reboot(void)
{
#   define SCB_AIRCR         (*((volatile uint32_t *)(0xE000ED0C)))
#   define AIRCR_VECTKEY     (0x05FA0000)
#   define SYSRESET          (1 << 2)
    SCB_AIRCR = AIRCR_VECTKEY | SYSRESET;
}


static int _devinfo_handler(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);

static int _fwupdate_handler(
        uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);

static int _fwupdate_info_handler(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);

static int _bas_handler(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg);

static void _start_advertising(void);


/* UUID = 38f28386-3070-4f3b-ba38-27507e991760 */
static const ble_uuid128_t gatt_svr_svc_fwupdate = BLE_UUID128_INIT(
        0x60, 0x17, 0x99, 0x7e, 0x50, 0x27, 0x38, 0xba, 
        0x3b, 0x4f, 0x70, 0x30, 0x86, 0x83, 0xf2, 0x38
        );

/* UUID = 38f28386-3070-4f3b-ba38-27507e991762 */
static const ble_uuid128_t gatt_svr_chr_fwupdate = BLE_UUID128_INIT(
        0x62, 0x17, 0x99, 0x7e, 0x50, 0x27, 0x38, 0xba, 
        0x3b, 0x4f, 0x70, 0x30, 0x86, 0x83, 0xf2, 0x38
        );

/* UUID = 38f28386-3070-4f3b-ba38-27507e991764 */
static const ble_uuid128_t gatt_svr_chr_fwupdate_info = BLE_UUID128_INIT(
        0x64, 0x17, 0x99, 0x7e, 0x50, 0x27, 0x38, 0xba, 
        0x3b, 0x4f, 0x70, 0x30, 0x86, 0x83, 0xf2, 0x38
        );

/* UUID = 38f28386-3070-4f3b-ba38-27507e991766 */
static const ble_uuid128_t gatt_svr_chr_fwupdate_info_notify = BLE_UUID128_INIT(
        0x66, 0x17, 0x99, 0x7e, 0x50, 0x27, 0x38, 0xba, 
        0x3b, 0x4f, 0x70, 0x30, 0x86, 0x83, 0xf2, 0x38
        );

/* GATT service definitions */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* Firmware update controls */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (ble_uuid_t*) &gatt_svr_svc_fwupdate.u,
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /* Characteristic: FWUPDATE  */
            .uuid = (ble_uuid_t*) &gatt_svr_chr_fwupdate.u,
            .access_cb = _fwupdate_handler,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            /* Characteristic: Firmware info */
            .uuid = (ble_uuid_t*) &gatt_svr_chr_fwupdate_info.u,
            .access_cb = _fwupdate_info_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /* Characteristic: Reboot notification */
            .uuid = (ble_uuid_t*) &gatt_svr_chr_fwupdate_info_notify.u,
            .access_cb = _fwupdate_info_handler,
            .val_handle = &_reboot_val_handle,
            .flags = BLE_GATT_CHR_F_NOTIFY,
        }, {
            0, /* no more characteristics in this service */
        }, }
    },
    {
        /* Device Information Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_GATT_SVC_DEVINFO),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_MANUFACTURER_NAME),
            .access_cb = _devinfo_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_MODEL_NUMBER_STR),
            .access_cb = _devinfo_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_SERIAL_NUMBER_STR),
            .access_cb = _devinfo_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_FW_REV_STR),
            .access_cb = _devinfo_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_HW_REV_STR),
            .access_cb = _devinfo_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, /* no more characteristics in this service */
        }, }
    },
    {
        /* Battery Level Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_GATT_SVC_BAS),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(BLE_GATT_CHAR_BATTERY_LEVEL),
            .access_cb = _bas_handler,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, /* no more characteristics in this service */
        }, }
    },
    {
        0, /* no more services */
    },
};

static int _fwupdate_info_handler(
        uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg)
{

    int rc = 0;
    uint16_t om_len;
    ble_uuid_t* read_uuid = (ble_uuid_t*) &gatt_svr_chr_fwupdate_info.u;
    static uint8_t ch;
    uint32_t ver = wolfBoot_current_firmware_version();
    (void) conn_handle;
    (void) attr_handle;
    (void) arg;
    if (ble_uuid_cmp(ctxt->chr->uuid, read_uuid) == 0) {
        puts("access to characteristic 'fwupdate_info'");
        rc = os_mbuf_append(ctxt->om, &ver, sizeof(uint32_t));
        return rc;
    }
    puts("unhandled uuid!");
    return 1;
}

#define FWUP_CHUNK_SIZE 128
#define FWUP_PKT_SIZE (FWUP_CHUNK_SIZE + sizeof(uint32_t))

static uint32_t fwup_cur_off = 0;
static uint32_t fwup_size = 0;
static uint8_t fwup_page_buffer[WOLFBOOT_SECTOR_SIZE];
static uint8_t fwup_pkt_buffer[FWUP_PKT_SIZE];
static uint32_t fwup_pkt_len = 0;

static void parse_update(void)
{
    uint32_t *sz;
    uint32_t *seq;
    const uint32_t zero = 0;
    if (fwup_size > 0)
        printf("Update: %lu / %lu \n", fwup_cur_off, fwup_size);
    else
        printf("Update: first packet\n");
    if (fwup_cur_off == 0) {
        if (memcmp(&zero, fwup_pkt_buffer, 4) != 0) {
            printf("wrong packet recvd: %lu\n", *(uint32_t *)(fwup_pkt_buffer));
            return;
        }
        if (memcmp(fwup_pkt_buffer + 4, "WOLF", 4) != 0) {
            puts("wrong packet hdr");
            return;
        }
        sz = (uint32_t *)(fwup_pkt_buffer + 8);
        if ((*sz < 256) || (*sz > WOLFBOOT_PARTITION_SIZE)) {
            printf("Wrong firmware size %lu\n", *sz);
            return;
        }
        fwup_size = *sz + IMAGE_HEADER_SIZE;
        printf("Total firmware len: %lu\n", fwup_size);
        hal_flash_lock();
        hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
        hal_flash_unlock();
        memset(fwup_page_buffer, 0xFF, WOLFBOOT_SECTOR_SIZE);
    }
    if (fwup_size > 0) {
        uint32_t in_page_off = fwup_cur_off % WOLFBOOT_SECTOR_SIZE;
        seq = (uint32_t *)(fwup_pkt_buffer);
        if (*seq != fwup_cur_off) {
            printf("Wrong seq %lu expecting %lu \n", *seq, fwup_cur_off);
            return;
        }
        memcpy(fwup_page_buffer + in_page_off, fwup_pkt_buffer + 4, fwup_pkt_len - 4);
        fwup_cur_off += fwup_pkt_len - 4;
        if (((fwup_cur_off % WOLFBOOT_SECTOR_SIZE) == 0) || (fwup_cur_off >= fwup_size)) {
            hal_flash_unlock();
            if (fwup_cur_off >= fwup_size) {
                uint32_t last_chunk_size = fwup_cur_off % WOLFBOOT_SECTOR_SIZE;
                uint32_t last_chunk_addr = fwup_cur_off - last_chunk_size;
                if (last_chunk_size > 0) {
                    hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + last_chunk_addr, fwup_page_buffer, last_chunk_size);
                }
            } else {
                hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + (fwup_cur_off - WOLFBOOT_SECTOR_SIZE), fwup_page_buffer, WOLFBOOT_SECTOR_SIZE);
            }
            hal_flash_lock();
            memset(fwup_page_buffer, 0xFF, WOLFBOOT_SECTOR_SIZE);
            if (fwup_cur_off + fwup_pkt_len >= fwup_size) {
                printf("Update complete (%lu/%lu).\n", fwup_cur_off, fwup_size);
                wolfBoot_update_trigger();
                reboot();
                while(1) 
                    ;
            }
        }
    }
    return;
}


static int _fwupdate_handler(
        uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg)
{

    int rc = 0;
    uint16_t om_len;
    ble_uuid_t* write_uuid = (ble_uuid_t*) &gatt_svr_chr_fwupdate.u;
    (void) conn_handle;
    (void) attr_handle;
    (void) arg;

    if (ble_uuid_cmp(ctxt->chr->uuid, write_uuid) == 0) {
        switch (ctxt->op) {
            case BLE_GATT_ACCESS_OP_READ_CHR:
                rc = os_mbuf_append(ctxt->om, &fwup_cur_off, sizeof(uint32_t));
                fwup_pkt_len = 0;
                //printf("ACK offset: %lx\n", fwup_cur_off);
                break;
            case BLE_GATT_ACCESS_OP_WRITE_CHR:
                om_len = OS_MBUF_PKTLEN(ctxt->om);
                if (om_len <= FWUP_PKT_SIZE) {
                    rc = ble_hs_mbuf_to_flat(ctxt->om, fwup_pkt_buffer, FWUP_PKT_SIZE, &om_len);
                    fwup_pkt_len = om_len;
                    parse_update();
                } else {
                    printf("wrong OM LEN: %d !\n", om_len);
                }
                break;
            case BLE_GATT_ACCESS_OP_READ_DSC:
                puts("read from descriptor");
                break;

            case BLE_GATT_ACCESS_OP_WRITE_DSC:
                puts("write to descriptor");
                break;

            default:
                puts("unhandled operation!");
                rc = 1;
                break;
        }
        return rc;
    }

    puts("unhandled uuid!");
    return 1;
}

static int _devinfo_handler(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    char version[20] = "";
    const char *str;

    switch (ble_uuid_u16(ctxt->chr->uuid)) {
        case BLE_GATT_CHAR_MANUFACTURER_NAME:
            puts("[READ] device information service: manufacturer name value");
            str = _manufacturer_name;
            break;
        case BLE_GATT_CHAR_MODEL_NUMBER_STR:
            puts("[READ] device information service: model number value");
            str = _model_number;
            break;
        case BLE_GATT_CHAR_SERIAL_NUMBER_STR:
            puts("[READ] device information service: serial number value");
            str = _serial_number;
            break;
        case BLE_GATT_CHAR_FW_REV_STR:
            puts("[READ] device information service: firmware revision value");
            snprintf(version, 20, "%08x", (unsigned int)wolfBoot_current_firmware_version());
            str = version;
            break;
        case BLE_GATT_CHAR_SW_REV_STR:
            puts("[READ] device information service: software revision value");
            snprintf(version, 20, "%08x", (unsigned int)wolfBoot_update_firmware_version());
            str = version;
            break;
        case BLE_GATT_CHAR_HW_REV_STR:
            puts("[READ] device information service: hardware revision value");
            str = _hw_ver;
            break;
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }

    int res = os_mbuf_append(ctxt->om, str, strlen(str));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int _bas_handler(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    puts("[READ] battery level service: battery level value");

    uint8_t level = 50;  /* this battery will never drain :-) */
    int res = os_mbuf_append(ctxt->om, &level, sizeof(level));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            printf("Ev: connect, Status: %d\n", event->connect.status); 
            if (event->connect.status) {
                _start_advertising();
                LED2_OFF;
                return 0;
            } 
            LED2_ON;
            _conn_handle = event->connect.conn_handle;
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            _start_advertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == _reboot_val_handle) {
                if (event->subscribe.cur_notify == 1) {
                }
                else {
                }
            }
            break;
    }

    return 0;
}

static void _start_advertising(void)
{
    struct ble_gap_adv_params advp;
    int res;

    memset(&advp, 0, sizeof advp);
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    advp.itvl_min  = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    advp.itvl_max  = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    res = ble_gap_adv_start(nimble_riot_own_addr_type, NULL, BLE_HS_FOREVER,
                            &advp, gap_event_cb, NULL);
    assert(res == 0);
    (void)res;
}

static void notify_boot(void)
{
    struct os_mbuf *om;
    unsigned int version;

    version = (unsigned int) wolfBoot_current_firmware_version();
    printf("[GATT] Sending boot notification.\n");
    /* send data notification to GATT client */
    om = ble_hs_mbuf_from_flat(&version, sizeof(unsigned int));
    if (!om) {
        printf("[GATT] Error incapsulating version data in frame \n");
        return;
    }
    ble_gattc_notify_custom(_conn_handle, _reboot_val_handle, om);
}

void *gatt_srv(void *arg)
{
    puts("NimBLE GATT server starting");

    int res = 0;
    msg_t msg;
    (void)res;


    notify_boot();
    wolfBoot_success();

    /* verify and add our custom services */
    res = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(res == 0);
    res = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(res == 0);

    /* set the device name */
    ble_svc_gap_device_name_set(_device_name);
    /* reload the GATT server to link our added services */
    ble_gatts_start();

    /* configure and set the advertising data */
    uint8_t buf[BLE_HS_ADV_MAX_SZ];
    bluetil_ad_t ad;
    bluetil_ad_init_with_flags(&ad, buf, sizeof(buf), BLUETIL_AD_FLAGS_DEFAULT);
    bluetil_ad_add_name(&ad, _device_name);
    ble_gap_adv_set_data(ad.buf, ad.pos);

    /* start to advertise this node */
    _start_advertising();

    while (1) {
        if (msg_receive(&msg) >= 0) {
            (void)msg;
        }
    }
    return 0;
}
