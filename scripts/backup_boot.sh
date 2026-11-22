#!/system/bin/sh
# 备份 SHV34 关键分区（拿到临时 root 后第一时间做！）
# 用法：adb shell sh /data/local/tmp/backup_boot.sh
# 然后把 /data/local/tmp/backup/*.img 拉回电脑：
#   adb pull /data/local/tmp/backup/
# 电脑端归档保存，boot 备份是救砖 / 刷 Magisk 的底线。

D=/data/local/tmp/backup
mkdir -p $D

# boot（mmcblk0p30，SHV34 实测；换机型先 cat /proc/partitions 对分区表确认）
dd if=/dev/block/mmcblk0p30 of=$D/boot.img bs=4096
dd if=/dev/block/mmcblk0p30 of=$D/boot.img.md5 bs=4096 count=1 2>/dev/null

# 校验与清单
md5sum $D/boot.img
ls -la $D
cat /proc/partitions > $D/partitions.txt
cat /proc/mounts > $D/mounts.txt
getprop > $D/props.txt

echo "[+] 备份完成：$D/  （记得 adb pull 回电脑）"
