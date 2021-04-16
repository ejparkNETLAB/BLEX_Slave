#!/bin/bash
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/basic/blinky
./1.sh
./2.sh
./3.sh
./4.sh
rm -rf build/
west build -b nrf52840dk_nrf52840 samples/bluetooth/throughputrealupdate
./1.sh
./2.sh
