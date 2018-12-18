#!/bin/bash

if (! test -f mac.txt); then
    echo "please create a file mac.txt with your device's BLE address"
    exit 1
fi

sudo modprobe bluetooth_6lowpan || true
echo "0" | sudo tee /sys/kernel/debug/bluetooth/6lowpan_enable
sleep .5
echo "1" | sudo tee /sys/kernel/debug/bluetooth/6lowpan_enable
echo "1" | sudo tee /proc/sys/net/ipv6/conf/all/forwarding
MAC=`cat mac.txt`
make
echo "connect $MAC 1" | sudo tee /sys/kernel/debug/bluetooth/6lowpan_control
while ( ! sudo ifconfig bt0 ); do
    sleep 1
done
sudo ifconfig bt0 add fd00:a::1/64
sleep 1
sudo service radvd restart
sudo tcpdump -i bt0 -n -w contiki.pcap &
./ota-server ../dtls-ota/dtls-ota-signed.bin
sudo killall tcpdump

