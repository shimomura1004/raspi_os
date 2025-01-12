FROM ubuntu:22.04
RUN ln -sf /usr/share/zoneinfo/Asia/Tokyo /etc/localtime \
&& apt update \
&& apt install -y --no-install-recommends \
   build-essential \
   gcc-aarch64-linux-gnu \
   libc6-dev-arm64-cross \
   gdb-multiarch \
   qemu-system-aarch64 \
   mtools \
&& apt -y clean \
&& rm -rf /var/lib/apt/lists/*