#!/system/bin/sh
R=/data/local/tmp/rootshell
D=/data/local/tmp
T=$D/wptest.log
: > $T
probe() {
  dev=$1; sec=$2; tag=$3
  $R dd if=$dev of=$D/wp_${tag}_before.bin bs=4096 skip=$sec count=1 >>$T 2>&1
  $R dd if=$D/wp_marker.bin of=$dev bs=4096 seek=$sec count=1 >>$T 2>&1
  $R sh -c "sync; echo 3 > /proc/sys/vm/drop_caches" >>$T 2>&1
  $R dd if=$dev of=$D/wp_${tag}_after.bin bs=4096 skip=$sec count=1 >>$T 2>&1
  if $R cmp -s $D/wp_marker.bin $D/wp_${tag}_after.bin; then echo "$tag PERSISTED" >>$T; else echo "$tag DROPPED" >>$T; fi
  $R dd if=$D/wp_${tag}_before.bin of=$dev bs=4096 seek=$sec count=1 >>$T 2>&1
  $R sh -c "sync; echo 3 > /proc/sys/vm/drop_caches" >>$T 2>&1
  $R chmod 666 $D/wp_${tag}_before.bin $D/wp_${tag}_after.bin >>$T 2>&1
}
probe /dev/block/mmcblk0p50 1 misc
probe /dev/block/mmcblk0p39 100 dvhi
echo DONE >>$T
$R chmod 666 $T
