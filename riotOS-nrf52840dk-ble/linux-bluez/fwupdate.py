#!/usr/bin/env python3
# 
# fwupdate.py
# 
# Copyright (C) 2021 wolfSSL Inc.
# 
# wolfBoot fw-update example, Bluez client for Linux.
# 
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

import os
import sys
import dbus
try:
  from gi.repository import GObject
except ImportError:
  import gobject as GObject
import sys
import time

from dbus.mainloop.glib import DBusGMainLoop

import bluezutils
import struct
import keyboard

# Settings
FWUPDATE_TARGET="nRF52_UPDATE"
HCI="hci1"


bus = None
mainloop = None

BLUEZ_SERVICE_NAME = 'org.bluez'
DBUS_OM_IFACE =      'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE =    'org.freedesktop.DBus.Properties'

GATT_SERVICE_IFACE = 'org.bluez.GattService1'
GATT_CHRC_IFACE =    'org.bluez.GattCharacteristic1'

FWUPDATE_SVC_UUID =    '38f28386-3070-4f3b-ba38-27507e991760'

FWUPDATE_CHR_WRITE_UUID =    '38f28386-3070-4f3b-ba38-27507e991762'
FWUPDATE_CHR_READ_UUID = '38f28386-3070-4f3b-ba38-27507e991764'
FWUPDATE_BOOT_NOTIFY_UUID = '38f28386-3070-4f3b-ba38-27507e991766'
FWUPDATE_CHUNK_SIZE=128




# The objects that we interact with.
fwupdate_service = None
fwupdate_ctrl_chrc = None
fwupdate_read_chrc = None
fwupdate_notify_chrc = None
bt_write_in_progress = False
    
fwup_off = 0
fwup_filename = None
fwup_file = None
fwup_filesize = 0

def bt_write(wbuf):
    global bt_write_in_progress
    bt_write_in_progress = True
    fwupdate_ctrl_chrc[0].WriteValue(wbuf, {},  reply_handler=ctrl_write_cb,
                                      error_handler=generic_error_cb,
                                      dbus_interface=GATT_CHRC_IFACE)
    #print("SND")

def ctrl_write_cb():
    global bt_write_buffer
    #print("ACK")
    bt_write_in_progress = False
    fwupdate_ctrl_chrc[0].ReadValue({}, reply_handler=update_off_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)

def generic_error_cb(error):
    print('D-Bus call failed: ' + str(error))
    mainloop.quit()

def update_off_cb(value):
    global fwup_off
    global fwup_filename
    global fwup_file
    global fwup_filesize
    x = struct.unpack("I", bytes(value))[0]
    fwup_off = x
    print("Update offset: " + str(x))
    
    if (fwup_file is None):
        if (fwup_filename is None):
            print ("No update filename provided. Terminating...\n");
            sys.exit(0)
        fwup_filesize = os.path.getsize(fwup_filename)
        try:
            fwup_file = open(fwup_filename, "rb")
        except:
            print("Cannot open "+fwup_filename+". Exiting...")
            sys.exit(1)

    fwup_file.seek(fwup_off, 0)
    wbuf = struct.pack("I", fwup_off)
    wbuf += fwup_file.read(FWUPDATE_CHUNK_SIZE)
    bt_write(wbuf)

def read_info_cb(value):
    x = struct.unpack("I", bytes(value))[0]
    print('Current version: '+ str(x))

def fwupdate_read_start_notify_cb():
    print('notification enabled')


def fwupdate_read_changed_cb(iface, changed_props, invalidated_props):
    if iface != GATT_CHRC_IFACE:
        return

    if not len(changed_props):
        return

    value = changed_props.get('Value', None)
    if not value:
        return
    fwupdate_read_chrc[0].ReadValue({}, reply_handler=read_info_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)

def sensors_start_notify_cb():
    print("Sensors notifications: enabled.")


def start_client():
    fwupdate_read_chrc[0].ReadValue({}, reply_handler=read_info_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)

    fwupdate_read_prop_iface = dbus.Interface(fwupdate_notify_chrc[0], DBUS_PROP_IFACE)
    fwupdate_read_prop_iface.connect_to_signal("PropertiesChanged", fwupdate_read_changed_cb)

    fwupdate_notify_chrc[0].StartNotify(reply_handler=sensors_start_notify_cb,
                                      error_handler=generic_error_cb,
                                      dbus_interface=GATT_CHRC_IFACE)

    fwupdate_ctrl_chrc[0].ReadValue({}, reply_handler=update_off_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)

def process_chrc(chrc_path):
    chrc = bus.get_object(BLUEZ_SERVICE_NAME, chrc_path)
    chrc_props = chrc.GetAll(GATT_CHRC_IFACE,
                             dbus_interface=DBUS_PROP_IFACE)

    uuid = chrc_props['UUID']

    if uuid == FWUPDATE_CHR_WRITE_UUID:
        global fwupdate_ctrl_chrc
        fwupdate_ctrl_chrc = (chrc, chrc_props)
    elif uuid == FWUPDATE_CHR_READ_UUID:
        global fwupdate_read_chrc
        fwupdate_read_chrc = (chrc, chrc_props)
    elif uuid == FWUPDATE_BOOT_NOTIFY_UUID:
        global fwupdate_notify_chrc
        fwupdate_notify_chrc = (chrc, chrc_props)
    else:
        print('Unrecognized characteristic: ' + uuid)

    return True


def process_fwupdate_service(service_path, chrc_paths):
    service = bus.get_object(BLUEZ_SERVICE_NAME, service_path)
    service_props = service.GetAll(GATT_SERVICE_IFACE,
                                   dbus_interface=DBUS_PROP_IFACE)

    uuid = service_props['UUID']

    if uuid != FWUPDATE_SVC_UUID:
        return False

    print('fwupdate GATT service found: ' + service_path)

    global fwupdate_service
    fwupdate_service = (service, service_props, service_path)
    
    # Process the characteristics.
    for chrc_path in chrc_paths:
        process_chrc(chrc_path)

    return True


def interfaces_removed_cb(object_path, interfaces):
    if not fwupdate_service:
        return

    if object_path == fwupdate_service[2]:
        print('Service was removed')
        mainloop.quit()


def main():
    # Set up the main loop.
    DBusGMainLoop(set_as_default=True)
    global bus
    bus = dbus.SystemBus()
    global mainloop
    mainloop = GObject.MainLoop()
    address = None

    if (len(sys.argv) > 1):
        global fwup_filename
        fwup_filename = sys.argv[1]

    om = dbus.Interface(bus.get_object(BLUEZ_SERVICE_NAME, '/'), DBUS_OM_IFACE)
    om.connect_to_signal('InterfacesRemoved', interfaces_removed_cb)

    print('Getting objects...')
    objects = om.GetManagedObjects()
    chrcs = []

    # List devices found
    for path, interfaces in objects.items():
        device = interfaces.get("org.bluez.Device1")
        if (device is None):
            continue
        try:
            if (device["Name"] == FWUPDATE_TARGET):
                print("Found FWUPDATE TARGET!")
                address = device["Address"]
                break
        except:
            continue

    if address is None:
        print("device not found.")
        return

    device = bluezutils.find_device(address, HCI)
    if (device is None):
        print("Cannot 'find_device'")
    else:
        device.Connect()
        print("Connected")
            

    # List characteristics found
    for path, interfaces in objects.items():
        if GATT_CHRC_IFACE not in interfaces.keys():
            continue
        chrcs.append(path)

    # List sevices found
    for path, interfaces in objects.items():
        if GATT_SERVICE_IFACE not in interfaces.keys():
            continue

        chrc_paths = [d for d in chrcs if d.startswith(path + "/")]

        if process_fwupdate_service(path, chrc_paths):
            break

    if not fwupdate_service:
        print('No FWUPDATE found.')
        sys.exit(1)

    start_client()
    mainloop.run()


if __name__ == '__main__':
    main()
