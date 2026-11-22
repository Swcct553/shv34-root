#!/bin/bash
# SHV34 内核编译脚本（Windows + MSYS2 实战版）
# 用法：在内核源码树根目录运行 ./build_kernel.sh
# 产物：vmlinux + System.map（符号提取见 README.md 第 3 节）
#
# 路径按你的环境修改这三行：
NDK10=/path/to/android-ndk-r10e/toolchains/aarch64-linux-android-4.9/prebuilt/windows-x86_64/bin
MSYS2_USR=/usr/bin
MINGW64=/mingw64/bin

export PATH="$MSYS2_USR:$NDK10:$MINGW64:$PATH"

make ARCH=arm64 deckard_al40_defconfig || exit 1

# -j1 是刻意的：夏普源码并行构建有头文件竞争，会产生随机假错误
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
     CC=aarch64-linux-android-gcc \
     HOSTCC="gcc -fcommon" \
     HOSTCFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer -std=gnu89 -fcommon" \
     KCFLAGS="-mno-android -I sound/soc/msm -I drivers/net/ethernet/msm -I drivers/platform/msm/ipa/ipa_v2 -I drivers/sharp/shtps/sy3000 -I drivers/soc/qcom -I drivers/usb/gadget" \
     -j1 2>&1 | tee build.log

echo "EXIT=$?"
grep -E "error:|Error [0-9]|undefined reference|No rule" build.log | grep -vE "warning|skipping" | head -20
ls -la vmlinux System.map 2>/dev/null
echo
echo "目标符号："
grep -E " (selinux_enforcing|modprobe_path)$" System.map 2>/dev/null
