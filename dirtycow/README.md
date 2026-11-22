# dirtycow — 备选攻击线（CVE-2016-5195）

主攻击线是 CVE-2019-2215（见 [../exploit/](../exploit/)）。DirtyCow 是独立备选：
SHV34 的 3.18.20 内核同样受影响。**注意：本路线在 SHV34 上只做到
"zygote 域代码执行实证"，没有走完全程到 root，作为参考研究保留。**

## 已验证的事实（SHV34 实机）

- `cow.c`（MAP_PRIVATE + /proc/self/mem 竞态写）可以稳定改写只读文件页
  （page cache），MAP_SHARED 直读可确认写穿透了真实缓存页。
- 成功案例：改写 `app_process64`（zygote）的 ELF entry（`e_entry` 重定向到
  注入代码），`adb shell stop/start` 让 init 重新 exec app_process64，
  注入代码在 **zygote 域**执行成功。
- `logtest3.c`：Android 6.0.1 logd 的 logdw 用 **12 字节**
  `android_log_header_t`（id+pad+tid+sec+nsec），不是老资料里的 20 字节，
  做日志侧信道的注意。

## 致命局限（为什么主推 2215）

- **page cache 易失**：写的是缓存页，`sync` 也写不回磁盘；但反过来重启就丢。
  要持久化必须让 init 在本 boot 内重新 exec 目标文件。
- `reboot` 不行——重启即清空 page cache，磁盘上仍是原版文件。
- 注入 `linker64` 不可行：64 位 cow 自身加载被注入 linker 时崩溃，
  引发系统服务崩溃风暴，只能 `adb reboot` 恢复。
- 无 SELinux 旁路：zygote 域拿不到内核任意写，离 root 还差很远。

## 文件

| 文件 | 说明 |
|---|---|
| `cow.c` | DirtyCow 竞态写，v2：MAP_SHARED 直读 page cache 验证真值（v1 的 COW 私有副本会误报 VERIFIED） |
| `payload.s` | app_process64 注入代码（aarch64） |
| `logtest3.c` | 12 字节 logd header 验证 |
| `test_memwrite.c` | /proc/self/mem 写最小验证 |

## 什么时候用它

- 想在 **zygote/shell 域**持久驻留某个 boot 期（比如开机自跑脚本）
- CVE-2019-2215 的 binder 探针不通过、前置检查失败时的退路
- 学习研究 page cache 攻击面

编译（NDK r21+）：

```bash
aarch64-linux-android23-clang -O2 -static -o cow cow.c
aarch64-linux-android23-clang -c payload.s
```
