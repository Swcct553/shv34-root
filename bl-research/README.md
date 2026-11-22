# SHV34 解 BL 研究：原始 eMMC 命令通道与写保护实测

> 状态：**BL 仍未解锁**。本文记录一次完整的「线一」攻关，结论是 *运行时无法写入
> devinfo*，同时给出一个对同平台玩家非常有用的**全新增量工具链**。
> 所有实验均为可还原的标记写入；未刷写任何分区镜像，未改动 boot/system。

## 1. 目标与已知结构

- BL 锁状态 = `devinfo` 分区偏移 `0x10` 的 `is_unlocked` 字节。
- 反汇编 `aboot.bin`（32-bit ARM LK，加载基址 `0x91600000`）确认：
  - `0x9163a5a4` / `0x9163a830`：`memcmp(buf, "ANDROID-BOOT!", 13)`。
  - magic 不符时 aboot **自行写回 magic** 并清零 `[+0x10]` / `[+0x14]`，
    即结构可自愈；读路径**无签名校验**。
  - 实机 `devinfo` 前 4096 字节全 0，所以 `devinfo_unlocked.bin`（magic +
    `0x10=1`，其余保持 0）是合理的载荷，不会破坏其他字段。
- 分区几何（`/sys/block/mmcblk0/mmcblk0pXX/start|size`，单位 512B 扇区）：

| 分区 | p | start | size | 扇区 hex |
|---|---|---|---|---|
| boot | 30 | 428032 | 131072 (64MB) | `0x68800` |
| aboot | 33 | 821248 | 2048 (1MB) | `0xc8800` |
| devinfo | 39 | **11403264** | 32768 (16MB) | **`0xae0000`** |
| apdp | 40 | 11436032 | 32768 | `0xae8000` |
| misc | 50 | 12395520 | 2048 | `0xbd2400` |

> 注意：早期脚本里用的 `0xae0600` 是错的；`p39` 起点是 `0xae0000`。

## 2. 重大发现：`MMC_IOC_RPMB_CMD` 可绕过夏普的 ioctl 封锁

量产内核打开 `CONFIG_RW_PROTECT_EMMC_CUST_SH`，在 `drivers/mmc/card/block.c`
的 `mmc_blk_ioctl_cmd()` 里加了：

```c
if (card && mmc_card_mmc(card)) { ... return -EOPNOTSUPP; }   /* errno 95 */
```

所以 `MMC_IOC_CMD` 全部被拒（实测 `errno=95`），之前只能靠 CVE-2019-2215
任意写把 `card->type` 翻成 1（伪装 SD 卡）来绕过——代价是要跑完整的
exploit 链、定位 slab 中的 `card` 结构体，且 `type=1` 挂久了系统会崩。

**但 `mmc_blk_ioctl_rpmb_cmd()` 没有任何这个检查**，并且它把
`opcode / arg / flags / blksz / blocks / write_flag` 原样从用户态拷进
`struct mmc_command` 后调 `mmc_wait_for_req()`。也就是说这是一条
**内核自带的、不需要任何内核读写能力的任意 eMMC 命令通道**。

代价：该 ioctl 要求打开 `/dev/block/mmcblk0rpmb`，且开头会
`mmc_blk_part_switch()` 把卡切到 RPMB 分区。解决办法是在同一个 ioctl 的
第 0 条命令先发 **CMD6 切回用户区**：

```
CMD6 arg = (0x03<<24) | (179<<16) | (0<<8) | 1 = 0x03B30001
           (MMC_SWITCH_MODE_WRITE_BYTE, EXT_CSD_PART_CONFIG, value=0 user area)
flags   = 0x1d  (MMC_RSP_R1B | MMC_CMD_AC)
```

`MMC_IOC_RPMB_CMD` 一次最多 3 条命令，足够 `CMD6 + CMD24 + 校验`。

`bl-research/rpmb_cmd.c` 即为该通道的实现（NDK r21e 静态编译）：

```sh
aarch64-linux-android23-clang -O2 -static -w -o rpmb_cmd rpmb_cmd.c
adb push rpmb_cmd /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/rpmb_cmd
rootshell /data/local/tmp/rpmb_cmd r8              # CMD8 读 EXT_CSD
rootshell /data/local/tmp/rpmb_cmd wp  ae0000     # CMD30/CMD31（此机超时）
rootshell /data/local/tmp/rpmb_cmd r1  ae0001 out # CMD6+CMD17 读 512B
rootshell /data/local/tmp/rpmb_cmd w1  ae0001 in  # CMD6+CMD24 写 512B
rootshell /data/local/tmp/rpmb_cmd clr ae0000     # CMD6+CMD29 清 WP 组
rootshell /data/local/tmp/rpmb_cmd set ae0000     # CMD6+CMD28 设 WP 组
rootshell /data/local/tmp/rpmb_cmd probe ...      # CMD6+CMD24+CMD13 查状态位
```

验证：CMD8 返回 `ioctl ret=0`、R1=`0x900`，且 `EXT_CSD[192]=0x08`
(eMMC 5.1)、`[179]=0x3b` (RPMB 访问模式)、`[168]=0x20` 均合理 —— 是真实寄存器值。

## 3. 纠正两个早期错误

| 早期结论 | 实测 / 源码依据 |
|---|---|
| `EXT_CSD[33] = USER_WP = 0x01` → power-on 型保护 | **33 是 `CACHE_CTRL`**。`USER_WP = 171`，`BOOT_WP = 173`，`HC_WP_GRP_SIZE = 221`，`HC_ERASE_GRP_SIZE = 224`（设备 OSS 内核 `include/linux/mmc/mmc.h`）。实测 `[171] = 0x10` = `US_PERM_WP_DIS`，与写保护状态无关 |
| 「CMD30/31 data phase 超时」是卡/工具没配好 | 换 ioctl 通道、CMD6 切用户区、`blksz=512` 后依然 `errno=110`；CMD9(R2) 也超时。**这颗 host 对 WP/查询类命令普遍不配合**，不是配置问题 |

另外：`CONFIG_RW_PROTECT_EMMC_CUST_SH` 只拦 ioctl，**不拦块层写**；
`/sys/block/mmcblk0{,p39}/{ro,force_ro}` 全部为 0。

`USER_WP` 是「CMD28 该施加哪种保护」的**模式配置位**，不是任何 WP 组的
**状态位**；组状态要靠 CMD30(4B 位图) / CMD31(8B 类型) 读，而这两条在此机读不到。

## 4. 实验矩阵（全部可还原、sync + drop_caches 后回读）

先排除 page cache 假阳性：`dd` 写回相同字节会「报成功」，但加
`sync; echo 3 > /proc/sys/vm/drop_caches` 后才知道有没有真正落盘。

| 实验 | 结果 |
|---|---|
| 块层 `dd` 写 `devinfo` p39 sector1 / sector100 | **DROPPED**（`dd` 无报错、dmesg 无 I/O 错误，静默丢弃） |
| 块层 `dd` 写 `misc` p50 sector1（对照） | **PERSISTED** |
| 原始 CMD24 写 `devinfo` `0xae0001` / `0xae0000` | **DROPPED**，CMD24 响应 `0x900` 干净无 `WP_VIOLATION` |
| 原始 CMD24 写 `misc` `0xbd2401`（对照） | **PERSISTED**（`cmp` 完全一致） |
| CMD29 清 WP 组（8 种 arg 编码：分区起点、旧值、组号、字节地址…） | 全部接受（`0x800`）但**写入仍被丢弃** |
| CMD28 → CMD29 重新标记保护类型 | 无效 |
| CMD6 改 `USER_WP` = `0x01/0x04/0x05/0x00/0x18` 后 CMD29 | 全部无效；`CMD6` 本身可写（0x10→0x18 生效） |
| 写 opcode 24 / 25 / 26；CMD23 reliable-write | 全部无效 |
| 分区可写性扫描（`wpscan.sh`） | 见下 |

**保护范围图谱**（每个分区末扇区 + devinfo 内 5 个偏移，写标记→回读→还原）：

```
BLOCKED   : boot(p30), aboot(p33), abootdup(p34), devinfo(p39) 全 16MB
WRITABLE  : apdp(p40), blank00(p42), misc(p50), fotacache(p54)
```

`apdp` 紧邻 `devinfo` 却可写，说明保护是**按分区/按 WP 组精确施加**的，
不是整片用户区只读。真实 WP 组大小 = `HC_ERASE_GRP_SIZE[224]=8 × 512KiB
× HC_WP_GRP_SIZE[221]=1` = **4 MiB = 0x2000 扇区**；`0xae0000 / 0x2000 =
1392` 正好是组边界。

**扩展扫描（全部 bootloader / TrustZone 分区）**：

```
BLOCKED   : p6 mdtp, p9 xbl(SBL1), p13 tz, p15 hyp, p17 keymaster,
            p19 cmnlib, p21 cmnlib64, p25 devcfg, p30 boot,
            p33 aboot, p34 abootdup, p39 devinfo,
            GPT 区域 LBA 0 / 1 / 2 / 17
WRITABLE  : p38 dip, p40 apdp, p42 blank00, p50 misc, p54 fotacache
```

由此排除了两条看似可行的捷径：

- **patch SBL1**：`xbl`(p9) 同样被保护，改不了「上电时施加 WP」的代码。
- **改 GPT 把 `devinfo` 指向可写的 `dip`**：GPT 表区被保护，写不进去。
  （另外 `dip` 实为 MDTP 防盗分区 `validate_DIP_and_firmware` 用，与
  `device_info` 无关；aboot 里 `devinfo` 与 `Device is unlocked!
  Skipping verification...` 相邻，确认按分区名读 devinfo。）

## 5. 结论与下一步

所有标准运行时手段都已穷尽且无效，最符合的机制是：

> **SBL1 在上电阶段对 bootloader 关键分区所在 WP 组施加 power-on 写保护。**
> 依据：CMD29 对 power-on 型无效（只有 temporary 可清，permanent 更是无解）；
> CMD28 无法覆盖已保护组；`US_PERM_WP_DIS=1` 说明厂商禁用了 permanent
> 保护功能（否则产线自己也无法刷机）；保护精确覆盖 boot/aboot/devinfo；
> 官方 9008/firehose 必须能写这些分区，所以保护一定在 **SBL1 阶段之前**可解。

因此运行时（Linux 起来之后）无法解 BL，**正确方向是引导阶段写入**：

1. **SD Loader 线**：SBL1 有 `SD_LOADER: mode enter`（restart reason 16/17，
   `xbl.bin` 反汇编 `0x6217768`），可在 SBL1 早期执行自有代码，
   那时 power-on WP 尚未施加 → 直接写 devinfo。
   待判：镜像校验是纯 hash-in-file（可自制）还是带签名（死结）。
2. **9008/EDL**：卡在缺夏普签名 firehose；但 `adb reboot edl` 可进，
   且 `bkerler/Loaders` 有 Sharp/MSM8996 候选 loader，值得逐个 `printgpt` 只读试。
3. 若做内核补丁线：需先证明保护在内核层（目前证据更偏向卡层），
   `/proc/kallsyms` 在此机不存在，`kptr_restrict=2`，定位成本高。

## 6. 风险提示

- `CMD28` 若 `US_PERM_WP_EN` 被置位会**永久锁死该 WP 组**，切勿在未确认
  `USER_WP` 的情况下对真实分区发 CMD28。
- `CMD6` 改 `USER_WP` 是「只置不清」语义；本轮把 `0x10` 改成了 `0x18`
  （`US_PWR_WP_DIS`）。该位为 `C_P` 类型，**断电/硬件复位后自动清零**，
  不会永久改变设备。
- 每次 RPMB ioctl 会把卡切到 RPMB 分区；本工具在每条命令序列前用 CMD6
  切回用户区。不要 unbind `sdhci` host（系统会立刻失去根文件系统）。
- 本目录所有脚本写标记后都会写回原扇区。实机复核：boot/aboot/devinfo
  内容未变，`USER_WP` 断电即恢复。
