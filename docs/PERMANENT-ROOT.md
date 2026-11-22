# 永久 root（Magisk）路线与现状

## 结论先行

**SHV34 无法直接刷 Magisk**：BL（bootloader）锁未解，`fastboot flash` 任何分区
都会被拒绝/导致无法启动。永久 root 的唯一正规路径是**先解 BL 锁**。

## 现状

- ✅ 临时 root 完全可用（重启失效，跑 ROOTME.sh 恢复）
- ✅ boot 分区已完整备份到电脑（64MB，mmcblk0p30）
- ⛔ BL 未解锁
- 📦 Magisk v25.2 APK 已就绪（未做任何刷写）

## BL 锁现状（au/KDDI 日版）

日版 au 机型**没有官方 BL 解锁渠道**（索尼日版、夏普、京瓷均如此）：

1. **官方渠道：无**。SHARP/au 不提供 unlock code 申请页面（美版/欧版夏普也没有）。
2. **第三方解锁服务**：有商家提供 msm8996 一代机型的解锁服务（多为 exploits 或
   修改的 aboot），可自行搜索评估风险。
3. **工程 aboot / 刷 9008**：msm8996 可用 EDL(9008) 模式配合 firehose 编程器
   强写分区，但 SHV34 的 firehose 签名（programmer）不公开，需要付费泄露资源，
   且写错 aboot 直接变砖（EDL 也救不回）。
4. **社区跟进**：XDA 搜索 "SHV34 unlock" / "AQUOS SHV34 bootloader"。

## 解锁后的永久 root 步骤

前提：BL 已解锁 + boot.img 备份在电脑。

```bash
# 1. 提取 Magisk patch：手机装 Magisk APK，用它 patch 备份的 boot_shv34.img
adb push boot_shv34.img /sdcard/Download/
# Magisk app → 安装 → 选择并修补一个文件 → 得 magisk_patched-xxxxx.img
adb pull /sdcard/Download/magisk_patched-xxxxx.img

# 2. 解锁后刷入
adb reboot bootloader
fastboot flash boot magisk_patched-xxxxx.img
fastboot reboot

# 3. 开机后 Magisk app 里装好，即可管理 su
```

注意事项：

- Magisk 版本别太新：Android 6.0 需要旧版 Magisk（v24 及以下兼容良好，
  v25.2 也支持 6.0；v26+ 已放弃 6.0）。
- 首次刷 patched boot 前确保 `fastboot oem unlock`/`flashing unlock` 真的成功
  （`fastboot getvar unlocked` 确认），否则机器起不来。
- 刷前先确认 fastboot 下能 `fastboot boot magisk_patched.img`（临时引导测试）
  的机型优先用这个方式验证，失败重启无损失。

## 解锁前的日常方案

临时 root 完全够用（adb 场景）：

```bash
# 每次重启后
sh scripts/ROOTME.sh
adb shell /data/local/tmp/rootshell '你的命令'
```

限制：只能通过 adb/rootshell 使用 root，普通 App 无法调 su（.su 临时 tmpfs
方案下第三方 App 要用 su 需要把 `/sbin` 放行，且重启失效）。
