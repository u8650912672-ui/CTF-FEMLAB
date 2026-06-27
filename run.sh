#!/bin/bash
DIR="$(dirname "$(realpath "$0")")"
if ! command -v qemu-system-x86_64 &>/dev/null; then
    echo "Install qemu: sudo pacman -S qemu-full  (or apt install qemu-system-x86)"
    exit 1
fi
qemu-system-x86_64 \
    -m 1024 \
    -kernel "$DIR/vmlinuz-linux" \
    -initrd "$DIR/initramfs-linux.img" \
    -drive file="$DIR/rootfs.img",format=raw,if=virtio \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net,netdev=net0 \
    -append "root=/dev/vda rw console=ttyS0" \
    -serial stdio
