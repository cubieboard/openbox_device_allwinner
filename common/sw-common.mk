
BUILD_NUMBER := $(shell date +%Y%m%d)

PRODUCT_COPY_FILES += \
	device/allwinner/common/bin/fsck.exfat:system/bin/fsck.exfat \
	device/allwinner/common/bin/mkfs.exfat:system/bin/mkfs.exfat \
	device/allwinner/common/bin/mount.exfat:system/bin/mount.exfat \
	device/allwinner/common/bin/ntfs-3g:system/bin/ntfs-3g \
	device/allwinner/common/bin/ntfs-3g.probe:system/bin/ntfs-3g.probe \
	device/allwinner/common/bin/mkntfs:system/bin/mkntfs \
	device/allwinner/common/bin/busybox:system/bin/busybox	

# 3g dongle conf
PRODUCT_COPY_FILES += \
	device/allwinner/common/rild/ip-down:system/etc/ppp/ip-down \
	device/allwinner/common/rild/ip-up:system/etc/ppp/ip-up \
	device/allwinner/common/rild/call-pppd:system/etc/ppp/call-pppd \
	device/allwinner/common/rild/usb_modeswitch.sh:system/etc/usb_modeswitch.sh \
	device/allwinner/common/rild/3g_dongle.cfg:system/etc/3g_dongle.cfg \
	device/allwinner/common/rild/usb_modeswitch:system/bin/usb_modeswitch \
	device/allwinner/common/rild/liballwinner-ril.so:system/lib/liballwinner-ril.so

PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,device/allwinner/common/rild/usb_modeswitch.d,system/etc/usb_modeswitch.d)
