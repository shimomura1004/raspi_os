#!/bin/bash
DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -kernel kernel8.img -nographic -serial null -serial mon:stdio -s $*
