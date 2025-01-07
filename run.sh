#!/bin/bash
#DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -kernel kernel8.img -dtb bcm2710-rpi-3-b.dtb -serial stdio -no-reboot -device usb-kbd -device usb-tablet -device usb-net,netdev=net0 -netdev user,id=net0,hostfwd=tcp::2222-:22 -gdb tcp::12345 -S

# -s means -gdb tcp::1234
# -S means wait for gdb
# add -dtb causes overlap error
#DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -serial stdio -s -S

# raspi3b には uart が2つあり、1つ目が PL011、2つ目が miniuart である
# miniuart のほうを標準出力に出すためには -serial を2回指定する必要がある
#DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -nographic -serial null -serial mon:stdio -s -S
#DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -nographic -serial null -serial mon:stdio -s $*

DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -nographic -serial null -serial mon:stdio -sd ./fs.img -s $*
