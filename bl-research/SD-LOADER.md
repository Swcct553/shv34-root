# SHV34 SBL1「SD_LOADER」救援通道逆向

> 状态：**软件侧已完全打通镜像格式、校验算法、分区布局和触发命令**；
> 实机引导验证与 payload 注入尚未执行。本文给出可复现的全部依据。

## 1. 为什么走这条路

线一已证明：Linux 运行时，`boot`/`aboot`/`xbl`/`devinfo`/GPT 等关键分区
被写保护（`bl-research/README.md`），CMD29/CMD28、原始 CMD24 均无法写入。
但官方售后必须能刷这些分区，说明保护在 **SBL1 上电阶段之前** 尚未施加。
SBL1 里正好有一条夏普私有的救援引导分支。

## 2. xbl.bin 的地址映射

`xbl.bin`(p19 dump) 是一个 AArch64 ELF，20 个 program header：

| PH | file off | VA | size |
|---|---|---|---|
| 2 | `0x3000` | `0x6208000` | `0x5fdac` ← SBL1 主代码 |
| 10 | `0x75e70` | `0x85f00000` | `0x157c8` |
| 13 | `0x8b7c0` | `0x85e80000` | `0x2c7ce` ← SD loader 加载器 |

映射公式 `file = PH2.base + (VA - 0x6208000)`：

| VA | 用途 |
|---|---|
| `0x6217768` | `check_boot_mode()` |
| `0x6208830` | PLT 跳板 → `0x85f02a40`（读 boot reason） |
| `0x85f40050` | boot reason 结构：`+1` valid、`+4` reason |
| `0x85e98538` | SD loader 主加载函数 |
| `0x85e98604` | 魔数 + 校验和验证 |
| `0x85e986e0` | **字节累加和函数** |
| `0x85e899d8` | 打开 SDCC 设备（索引 0..3） |

`bl-research/sbl_dis.py` 可反汇编任意 VA（capstone 5.x）。

## 3. 进入条件（已确认，无需内核任意写）

`0x6217768` 反汇编：

```c
x0 = param;  [0x626c888] = x0;
if (get_boot_reason(&sp[8]) != 0) return;
bl 0x6225670;
x8 = reason;
if (x8 <= 0x16 && ((1ULL << x8) & 0x630000)) {   /* reason 16,17,21,22 */
    print("SD_LOADER: mode enter");               /* 0x6209cef */
    w0 = 0; bl 0x6223b48; bl 0x6216258;           /* → SD loader */
} else if (reason == 0x13 || reason == 0x14) { ... }
  else if (reason == 0x12) { print("sh_boot_dload_check:sbl_dload_entry() call"); bl 0x6217830; }
```

`0x630000 = 0x63 << 16`，`0x63 = 0b01100011` ⇒ **reason ∈ {16, 17, 21, 22}（十进制）
= {0x10, 0x11, 0x15, 0x16}**。这与内核 `include/linux/qpnp/power-on.h` 的
PMIC PON reason 常量完全对应：

```
0x10 PON_RESTART_REASON_EMERGENCY_NODLOAD
0x11 PON_RESTART_REASON_WHILE_LINUX_RUNNING
0x15 PON_RESTART_REASON_REBOOT_FROM_SHRLOG_HW_WTD_MODE
0x16 PON_RESTART_REASON_REBOOT_FROM_SHRLOG_MODEM_CRASH
```

`0x85f02900` 是 PMIC boot factor → SBL1 reason 的查表函数，最后
`str w19, [0x85f40050+4]` 写入 reason；`0x85f029c8` 置 `valid=1`。

**触发命令**（`drivers/power/reset/msm-poweroff.c`，`download_mode=0` 时）：

```sh
adb reboot emergency        # -> qpnp_pon_set_restart_reason(0x10)
# 或 adb reboot surfaceflinger
```

另有两个可用的原始写通道（备用）：

```sh
adb reboot oem-10           # __raw_writel(0x6f656d00 | (code & 0xff), IMEM)
adb reboot downloader       # sh_set_dload_mode(1)
```

IMEM restart reason 物理地址 = `0x066BF000 + 0x65C = 0x066BF65C`
（设备树 `/proc/device-tree/soc/qcom,msm-imem@66bf000/restart_reason@65c`）。
`/dev/mem` 在此机不存在，需要时可用内核任意写走线性映射
`0xffffffc0066bf65c`。

## 4. 镜像加载与校验（**已破解**）

`0x85e98538` 的流程：

```c
h   = open_sdcc(0, 0);                       // 0x85e899d8
read(h, 0, 0xD0000000, 34);                 // MBR + GPT header + entries
check "EFI PART";                            // 0x85e98408
parse_gpt(buf, &part, "sdl");                // 0x85e98438, 名字 UTF-16LE "sdl"
read(h, part.start_lba, 0xD0000000, part.sectors);   // 整块分区
memcmp(buf, "SDLOADER_IMG", 12);             // 0x85e9e0c4
if (checksum(buf, 508) != *(u32*)(buf+0x1fc)) return 6;
if (checksum(buf+0x200, part.sectors*512-512) != *(u32*)(buf+0x30)) return 7;
copy(buf+0x200, 0xC0400000, part.sectors*512-512);  // ELF 加载基址
... -> entry 0xC0402000
```

**校验就是逐字节累加和，不是 SHA256，更不是 RSA。** 校验和写在镜像内部，
重算即可自制镜像。

### 镜像格式（`sdl` = p23 / `sdldup` = p24 的 dump）

| 偏移 | 内容 |
|---|---|
| `0x00` | `SDLOADER_IMG\0\0\0\0` + `R1.03` 版本串 |
| `0x30` | 第二段累加和（4B） |
| `0x1FC` | 前 508 字节累加和（4B） |
| `0x200` | AArch64 ELF，entry `0xC0402000`，4 个 PH |
| `0x7FDAA` | 有效数据末端（其后补零） |

实测 `sdl_23.bin`：

```
sum(d[0:0x1fc])        = 0x781      == [0x1fc] ✓
sum(d[0x200:0x7fdaa])  = 0x17e136b == [0x30]  ✓
```

### 关键结论

> **自制/修改 SDLOADER_IMG 不需要任何私钥**：改 ELF 代码 → 重算两个
> 累加和 → 即可被 SBL1 加载并在 **SBL1 级权限**执行。

## 5. SDUPDATER（`sdl_23.bin` 的内容）

它是夏普售后下载器，不是空白 loader。字符串：

```
USB RCV / RCV COMMAND SUM / RCV COMMAND SETTING / RCV BINDATA NG
RCV GPT MAIN NG / RCV GPT BACKUP NG / RCV GPT DIFFERENT
CHECKSUM / CHECKSUM OTHER / SHDIAG WRITE / SHDIAG READ
HROFMT.BIN OPEN NG / NO FILE HRO.BIN / VERSION.IMG READ NG
SDFAT NO SDCARD / SDFAT INIT / SDFAT FILE READ
SDCC OPEN / ERASE / READ / WRITE / MODE_CHANGE
INPUT PASSWORD NG / NO CONNECT CHARGER / PRLOADER ERR 01..14
-----BEGIN / -----END / RSA PUBLIC KEY  (rsa_lib.c, 0x42220 起 256B 模数)
```

即：它走 **USB 主机协议**（可能需要密码），从 SD 卡 FAT 读 `HRO.BIN` 等
固件文件，并用 **RSA-2048 校验固件**。所以「伪造官方固件包」不可行；
但「自制 SDLOADER_IMG 直接注入 payload」不受 RSA 影响。

## 6. 待判与下一步

1. **设备索引**：`0x85e899d8(0,0)` 的索引 0 对应 `/hdev/sdc1`。
   若 = eMMC，则 SBL1 从 eMMC 的 `sdl`(p23) 加载固定镜像；
   若 = 外置 SD 卡，则我们放在卡上的自制镜像生效。**只能实机判别。**
   （eMMC p23/p24 已实测 BLOCKED，改不了。）
2. **触发验证**：`adb reboot emergency` + 观察是否进入 SD_LOADER。
   失败时设备应回退正常启动；若卡住，长按电源键强制关机即可恢复。
3. **payload**：若确认从外置 SD 加载自制镜像，则在 ELF 里注入
   SBL1 阶段代码，利用镜像自带的 `SDCC WRITE` 写 devinfo
   （`ANDROID-BOOT!` + `[0x10]=1`，见 `bl-research/README.md`）。
4. 备用：9008/EDL 逐个只读试 `bkerler/Loaders` 的 Sharp/MSM8996 loader。

## 7. 已生成的可复现产物

| 文件 | 说明 |
|---|---|
| `sbl_dis.py` | 按 VA 反汇编 xbl.bin（capstone） |
| `make_sdcard.py` | 生成 SD 卡前 1058 扇区（protective MBR + GPT + `sdl` 分区 + 合法镜像） |
| `sdcard_head.bin` | 生成结果，541696 B；GPT CRC 已自检通过 |
| `dis_*.txt` | 各关键函数反汇编 |

`make_sdcard.py` 只生成文件，**不会写任何磁盘**。写卡会覆盖 SD 卡
起始 ~542 KiB（可逆，重新格式化即可）。

## 8. 风险提示

- 触发 SD_LOADER 会让设备**不进 Android**，且临时 root 丢失；
  重启回来后需 `ROOTME.sh` 重新拿 root。
- 若自制镜像校验失败，SBL1 返回错误码 6/7/8，通常回退正常启动。
- 不要对真实分区发 CMD28（见主 README 的风险提示）。
- 到「注入 payload」这一步才需要写 AArch64 代码；前面所有步骤
  都不改动设备任何分区。
