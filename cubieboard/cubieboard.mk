# cubieboard product config

BOARD_USE_PRIV_GENERIC_KL := device/allwinner/cubieboard/custom/Generic.kl

$(call inherit-product, device/allwinner/crane-common/ProductCommon.mk)

DEVICE_PACKAGE_OVERLAYS := device/allwinner/cubieboard/overlay

PRODUCT_COPY_FILES += \
	device/allwinner/cubieboard/kernel:kernel \
	device/allwinner/cubieboard/recovery.fstab:recovery.fstab \
	frameworks/base/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \

PRODUCT_COPY_FILES += \
	device/allwinner/cubieboard/ueventd.sun4i.rc:root/ueventd.sun4i.rc \
	device/allwinner/cubieboard/init.sun4i.rc:root/init.sun4i.rc \
	device/allwinner/cubieboard/init.sun4i.usb.rc:root/init.sun4i.usb.rc \
	device/allwinner/cubieboard/media_profiles.xml:system/etc/media_profiles.xml \
	device/allwinner/cubieboard/camera.cfg:system/etc/camera.cfg \
	frameworks/base/data/etc/android.hardware.camera.xml:system/etc/permissions/android.hardware.camera.xml

#input device config
PRODUCT_COPY_FILES += \
	device/allwinner/cubieboard/sun4i-keyboard.kl:system/usr/keylayout/sun4i-keyboard.kl \
	device/allwinner/cubieboard/sun4i-ir.kl:system/usr/keylayout/sun4i-ir.kl \
	device/allwinner/cubieboard/sun4i-ts.idc:system/usr/idc/sun4i-ts.idc
PRODUCT_COPY_FILES += \
	device/allwinner/cubieboard/initlogo.rle:root/initlogo.rle \
    device/allwinner/cubieboard/hwc_mid.idx:system/usr/hwc/hwc_mid.idx \
    device/allwinner/cubieboard/hwc_mid.pal:system/usr/hwc/hwc_mid.pal \
    device/allwinner/cubieboard/hwc_big.idx:system/usr/hwc/hwc_big.idx \
    device/allwinner/cubieboard/hwc_big.pal:system/usr/hwc/hwc_big.pal

# pre-installed apks
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*.apk,$(LOCAL_PATH)/apk,system/preinstall)

#google service
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,$(LOCAL_PATH)/googleservice,system/app)

PRODUCT_COPY_FILES += \
	device/allwinner/cubieboard/vold.fstab:system/etc/vold.fstab

PRODUCT_PACKAGES += \
		gatord \
        TvdLauncher

PRODUCT_PROPERTY_OVERRIDES += \
	persist.sys.usb.config=mass_storage,adb \
	ro.sf.lcd_density=160 \
	ro.udisk.lable=cubieboard \
	ro.product.firmware=1.4 \
	ro.softmouse.left.code=6 \
	ro.softmouse.right.code=14 \
	ro.softmouse.top.code=67 \
	ro.softmouse.bottom.code=10 \
	ro.softmouse.leftbtn.code=2 \
	ro.softmouse.midbtn.code=-1 \
	ro.softmouse.rightbtn.code=-1 \
	keyguard.disable=true \
	ro.sw.hidesoftkbwhenhardkbin=0 \
	audio.routing=2 \
	audio.output.active=AUDIO_CODEC \
	audio.input.active=AUDIO_CODEC \
	ro.audio.multi.output=false \
	ro.sw.defaultlauncherpackage=com.allwinner.launcher \
	ro.sw.defaultlauncherclass=com.allwinner.launcher.Launcher \
	ro.sw.usedHardwareMouse=false

$(call inherit-product-if-exists, device/allwinner/cubieboard/modules/modules.mk)

PRODUCT_CHARACTERISTICS := tablet

# Overrides
PRODUCT_BRAND  := allwinners
PRODUCT_NAME   := cubieboard
PRODUCT_DEVICE := cubieboard
PRODUCT_MODEL  := cubieboard

