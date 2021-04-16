#!/bin/bash
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/basic/blinky
./1.sh
./2.sh
./3.sh
./4.sh
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/bluetooth/throughput_220kbps/
./1.sh
sleep 60s
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/bluetooth/throughput_10kbps/
./2.sh
sleep 60s
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/bluetooth/throughput_220kbps/
./3.sh
sleep 60s
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/bluetooth/throughput_150kbps/
./4.sh
