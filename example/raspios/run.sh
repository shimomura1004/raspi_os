#!/bin/bash

# -kernel オプションを使うと el2 から開始できるが、
# イメージをロードするアドレスを指定できないのでフラットバイナリが使えない
#DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -kernel kernel8.img -nographic -serial null -serial mon:stdio -s $*

DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -nographic -serial null -serial mon:stdio -s $*
