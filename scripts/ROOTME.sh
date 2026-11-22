#!/system/bin/sh
# 一键恢复临时 root（SHV34，重启后跑这个）
# 前提：/data/local/tmp/ 下已有 probe26、trig_bin、rootshell、.su.bin、wb.sh
#   （即 exploit/ 目录 deploy.py 部署过的那套文件）
adb shell /data/local/tmp/probe26 -y -L /data/local/tmp/probe26.log \
  && adb shell /data/local/tmp/trig_bin
sleep 2
adb shell /data/local/tmp/rootshell id
