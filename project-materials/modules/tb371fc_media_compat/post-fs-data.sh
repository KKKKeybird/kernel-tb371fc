#!/system/bin/sh
MODDIR=${0%/*}
TARGET=/system/lib64/libstagefright.so
EXPECTED=9ad61fd38cc40b8c01ed284876962877d14393958644f5ea336e71dc1b4f7a82
CURRENT=$(sha256sum "$TARGET" | cut -d ' ' -f 1)
if [ "$CURRENT" != "$EXPECTED" ]; then
  echo "Skipped: system media library differs from tested build" > "$MODDIR/mount.log"
  exit 0
fi
/system/bin/nsenter -t 1 -m -- mount -o bind "$MODDIR/payload/media_codecs_vendor_audio.xml" /vendor/etc/media_codecs_vendor_audio.xml || exit 1
/system/bin/nsenter -t 1 -m -- mount -o bind "$MODDIR/payload/libstagefright.so" "$TARGET" || exit 1
echo "Registered existing Qualcomm AAC and restored normal codec metadata lookup" > "$MODDIR/mount.log"
