#!/bin/bash -e

cp config.txt /media/robbe/MBED/config.txt
cp laser.bin /media/robbe/MBED/laser.bin
sync
echo "Deploy done"