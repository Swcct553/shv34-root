# kernel — SHV34 内核编译与符号提取

SHV34 没有公开的 `System.map`，官方只放出 OSS 源码包。本目录记录如何用
**与设备 `/proc/version` 完全同版**的工具链编译内核、提取两个攻击目标符号，
并用 dmesg 崩溃栈锚点 + vdso 数据区双重校验地址可信。

最终目标符号（已验证）：

| 符号 | 地址 |
|---|---|
| `selinux_enforcing` | `0xffffffc001c7d9ac` |
| `modprobe_path` | `0xffffffc001a37b60` |
| `_text`（Image 基址） | `0xffffffc000080000` |

## 1. 素材

- **OSS 源码**：夏普官方 OSS 页面（sharp-oss.com，搜 SHV34 或 AQUOS），
  解包后内核源码树使用 `deckard_al40_defconfig`。
- **交叉编译器**：NDK r10e 里的 gcc（`aarch64-linux-android-4.9`，
  版本 4.9.x 20140827），务必和设备 `/proc/version` 显示的 gcc 版本一致，
  否则结构体布局可能漂移。
- **构建环境**：Windows 下用 MSYS2（bash + make 4.x）；macOS/Linux 直接用。
  在 NTFS 上编译会有大小写合并坑，见第 4 节。

```bash
adb shell cat /proc/version
# Linux version 3.18.20-perf (…gcc version 4.9.x 20140827 (prerelease) …)
```

## 2. 编译

参考 [build_kernel.sh](build_kernel.sh)（MSYS2/Windows 版，照搬自实战脚本）：

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
     CC=aarch64-linux-android-gcc \
     HOSTCC="gcc -fcommon" \
     HOSTCFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer -std=gnu89 -fcommon" \
     KCFLAGS="-mno-android -I sound/soc/msm -I drivers/net/ethernet/msm -I drivers/platform/msm/ipa/ipa_v2 -I drivers/sharp/shtps/sy3000 -I drivers/soc/qcom -I drivers/usb/gadget" \
     -j1
```

要点：

- 先 `make ARCH=arm64 deckard_al40_defconfig`。
- `-fcommon`：gcc 新版 host 编译器需要，否则 `multiple definition` 报错。
- `-mno-android`：NDK gcc 默认 `-mandroid` 会改链接行为。
- `-j1`：夏普源码有头文件竞争，并行构建会产生随机错误（曾实测 19 个假错误）。
  3.18 内核不大，串行 20~40 分钟可接受。
- 产物：`vmlinux`（带符号）→ `System.map` 自动生成。

## 3. 提取符号

```bash
grep -E " (selinux_enforcing|modprobe_path)$" System.map
```

## 4. NTFS（Windows）大小写合并坑——逐个确认过的修复

NTFS 默认大小写不敏感，`git`/解包时 `Xt_DSCP.h` 与 `xt_dscp.h` 会合并成同一个
文件、内容互相覆盖。实战遇到的 4 处：

| 文件 | 问题 | 修复 |
|---|---|---|
| `include/uapi/linux/netfilter/xt_DSCP.h` / `xt_dscp.h` | 两套定义互相覆盖 | 手工合并两套 enum/macro 到一份 |
| `ipt_ttl.h` / `ipt_TTL.h`、`ip6t_HL.h` / `ip6t_hl.h` | 同上 | 手工合并 |
| `net/netfilter/xt_hl.c` | 与 `xt_HL.c` 合并 | 重命名其一（如 `xt_hl_match.c`）+ 改 Makefile |
| `net/ipv4/netfilter/ipt_MASQUERADE.c` 等 | 少数大小写对 | 逐个看编译错误处理 |

出现"找不到宏/枚举"类错误时，第一怀疑对象就是大小写合并。

## 5. 校验——地址可信的证据链

盲抄编译产物地址有风险（源码版本差异、配置差异都会挪动符号）。两级校验：

### 5.1 dmesg 符号锚点

设备任意一次内核告警的 dmesg 崩溃栈会打印几十条：

```
[<ffffffc000207668>] dump_backtrace+0x0/0x270
[<ffffffc00025f824>] pm_qos_add_request+0x3c/0x250
...
```

把设备 dmesg 里的 `[<addr>] sym+off/size` 全部抄下来（实战收集了 34 条，
样例见 [anchors.shv34.txt](anchors.shv34.txt)），与本地 `System.map` 逐条比对：

- 地址完全一致 + 偏移落在函数大小内 → 符号可信；
- 实战结果：34 条锚点中 10 条与本地编译产物**精确一致**（同地址同符号），
  其余因内联/编译器微差落在相邻函数，整体证明代码段布局正确。

### 5.2 vdso 数据区逐字节比对

从设备内存读 vdso 数据页（用任意读原语或已有 dump），与本地编译的
`vdso.so` 数据段逐字节比对。实战结果：完全一致 → 数据段布局可信
（`selinux_enforcing` 和 `modprobe_path` 都在数据段/`.data` 区）。

### 5.3 终极自证

用 boot 镜像本身验证：从 boot 分区解出 gzip 压缩的 ARM64 `Image`，
其中 `/sbin/modprobe` 字符串位于 `kernel + 0x19b7b60`，
加上基址 `0xffffffc000080000` = `0xffffffc001a37b60`，
与 System.map 的 `modprobe_path` **完全一致**。这是不依赖任何推断的实证。

## 6. boot 镜像解包速查

```bash
# boot.img：kernel 紧跟 header page（对齐到 page_size，SHV34 是 2048）
dd if=boot.img of=kernel.gz bs=1 skip=2048
file kernel.gz            # gzip 数据
gzip -d kernel.gz         # → ARM64 Image（约 28.9MB，基址 0xffffffc000080000）
strings Image | grep sbin/modprobe
```
