# SHV34 量产内核的反 root 加固（实测发现）

本文记录在 SHV34（Android 6.0.1, kernel 3.18.20-perf）实机上验证的内核级反 root
机制。**夏普 OSS 源码里没有这些补丁**（OSS 源码编译产物与量产内核在数据段符号
上完全一致，但量产内核带有闭源加固代码）——对其他 au/夏普机型是重要参考。

## 实测结论

| 尝试 | 结果 | 说明 |
|---|---|---|
| remount /system rw | **EPERM** | `MNT_LOCK_READONLY`（init ro 挂载时锁定标志），dm-verity enforcing 也开着 |
| 在 /system/xbin、/system/etc 上叠挂 tmpfs | **EPERM** | 原版 3.18 内核不可能返回此错误，量产内核加了拦截补丁 |
| 在 /system（挂载点根）上叠挂 | 会成功 | 成功后 /system 整体被隐藏，系统几十秒内崩溃重启 |
| 在 /vendor/bin 上叠挂 tmpfs | 挂载成功 | 但系统随后崩溃重启（vendor 守护进程体系不容忍叠层） |
| chmod 4755（setuid 位） | **EPERM** | 对 /data tmpfs、/vendor 叠挂统统拒绝——**setuid 路径全灭** |
| 写 /system 块设备 | 只读 | dm-0 verity 映射，块级只读；直写底层分区会破坏哈希树 → 变砖风险 |

## 为什么这决定了 SuperSU/Magisk 的命运

SuperSU 的标准安装模型需要把 `su` + `daemonsu` 写进 `/system/xbin` 并设置 setuid：

1. /system 写不进（verity + 只读锁 + 挂载拦截）
2. 绕过 /system 用 tmpfs 叠挂：/system 系被 EPERM 拦截；/vendor 叠挂虽能挂上
   但系统不稳 + setuid 位被内核拒绝
3. setuid 位全局被禁 → 即使 su 二进制放进了 PATH，也无法提权
   （实测 shell 执行 su：`cannot become root: Operation not permitted`）

**结论：不解 BL 锁，SuperSU（以及任何依赖 setuid su 的 App 级 root 管理）
在这台机器上不可能实现。** root 的唯一入口是利用链本体（rootshell，adb 场景）。

## 对利用链的影响

- `exploit/payload/wb.sh` 里的 setuid su 流程（.su tmpfs + 4755）在 SHV34 上
  **setuid 部分静默失败**（chmod 被 2>/dev/null 吞掉），不影响 rootshell。
- 日常 root 访问一律走：
  ```
  adb shell /data/local/tmp/rootshell '命令'
  adb shell /data/local/tmp/rootshell      # 交互式
  ```

## 移植提示（其他机型）

- 如果你的机型 OSS 源码编译产物能通过符号校验（见 kernel/README.md），但
  利用链某一环出现"原版内核不可能返回"的错误（EPERM/EINVAL），几乎可以断定
  量产内核有闭源加固补丁。本文的方法学（受控挂载矩阵 + errno 判定 + dmesg
  avc 交叉验证）可以用来定位。
- 2016 年 au/KDDI 机型（夏普、京瓷、索尼日版）普遍做了这类加固。

## 验证方法学速查

1. 用裸 `mount(2)` 测试器（绕过 toybox）对不同目标做挂载矩阵，记录 errno；
2. 对照 dmesg avc 日志：**没有 avc 记录的 EPERM = 内核私有补丁拒绝**
   （SELinux permissive 下任何 LSM 拒绝都会留 avc 日志）；
3. 对照 OSS 源码排除原版内核逻辑（`fs/namespace.c` 的 EPERM 位置全部核对）。
