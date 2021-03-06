#!/system/bin/sh

######## BootMenu Script
######## Execute Pre BootMenu


export PATH=/sbin:/system/xbin:/system/bin


######## Main Script

mount -o remount,rw rootfs /

BUSYBOX="/sbin/busybox"

# RECOVERY tool includes busybox
cp -f /system/bootmenu/recovery/sbin/recovery $BUSYBOX

chmod 755 /sbin
chmod 755 $BUSYBOX
$BUSYBOX chown 0.0 $BUSYBOX
$BUSYBOX chmod 4755 $BUSYBOX
$BUSYBOX chmod +rx /sbin/*

# busybox sym link..

for cmd in $($BUSYBOX --list); do
  $BUSYBOX ln -s /sbin/busybox /sbin/$cmd
done

busybox chmod -R +x /sbin

# replace /sbin/adbd..

cp -f /system/bootmenu/binary/adbd /sbin/adbd.root
chmod 4755 /sbin/adbd.root
chown 0.0 /sbin/adbd.root


## missing system files

[ ! -c /dev/tty0 ]  && ln -s /dev/tty /dev/tty0


## /default.prop replace..

rm -f /default.prop
cp -f /system/bootmenu/config/default.prop /default.prop


## mount cache

mkdir -p /cache

if [ -x /system/bin/mount_ext3.sh ]; then
  /system/bin/mount_ext3.sh cache /cache
else
  mount -t ext3 -o nosuid,nodev,noatime,nodiratime,barrier=1 /dev/block/cache /cache
fi

exit
