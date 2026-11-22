#!/system/bin/sh
R=/data/local/tmp/rootshell
RC=/data/local/tmp/rpmb_cmd
M=/data/local/tmp/raw512_marker.bin
D=/data/local/tmp
T=$D/wpscan.log
: > $T
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk 887ff p30_boot
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk c8fff p33_aboot
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk c97ff p34_abootdup
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk ae7fff p39_devinfo
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk ae0000 p39_devinfo_FIRST
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk aeffff p40_apdp
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk af7fff p42_blank00
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk bd2bff p50_misc
chk() { $R $RC r1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC w1 "$1" $M >/dev/null 2>&1; $R $RC r1 "$1" $D/sn.bin >/dev/null 2>&1; if $R cmp -s $M $D/sn.bin; then echo "$2 WRITABLE" >> $T; else echo "$2 BLOCKED" >> $T; fi; $R $RC w1 "$1" $D/so.bin >/dev/null 2>&1; $R $RC r1 "$1" $D/sr.bin >/dev/null 2>&1; $R cmp -s $D/so.bin $D/sr.bin || echo "  $2 RESTORE_FAILED" >> $T; }
chk cd31ff p54_fotacache
chk ae0001 devinfo_off1
chk ae0400 devinfo_off1024
chk ae2000 devinfo_off8192
chk ae4000 devinfo_off16384
chk ae7fff devinfo_off32767
echo DONE >> $T
$R chmod 666 $T
