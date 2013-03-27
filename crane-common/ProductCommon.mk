# full crane product config
#$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, device/allwinner/crane-common/device_base.mk)
$(call inherit-product, device/allwinner/common/sw-common.mk)
$(call inherit-product, frameworks/base/build/tablet-dalvik-heap.mk)

DEVICE_PACKAGE_OVERLAYS := device/allwinner/crane-common/overlay

PRODUCT_PACKAGES += \
	hwcomposer.exDroid \
	display.sun4i \
	lights.sun4i \
	gps.sun4i \
	make_ext4fs \
	libjni_pinyinime
		
PRODUCT_PACKAGES += \
	audio.primary.exDroid \
	audio.a2dp.default \
	camera.exDroid \
	libfacedetection \
	libaudioutils \
	libcedarxbase \
	libcedarxosal \
	libcedarxsftdemux \
	libcedarv \
    libcedarv_base \
    libcedarv_adapter \
    libve \
	libaw_audio \
	libaw_audioa \
	libswdrm \
	libthirdpartstream \
	libcedarxsftstream \
	libstagefright_soft_cedar_h264dec \
	libjni_mosaic \
	u3gmonitor \
	chat \
	TvdVideo \
	TvdFileManager \
	TvdSettings \
	Phone

PRODUCT_PACKAGES += \
	libsmbclient \
	libsmbjni \
	sambaserver \
	securefileserver \
	libsecurefile_jni \
	libsecurefileservice \
	isomountmanagerservice \
	libisomountmanager_jni \
	libisomountmanagerservice	

PRODUCT_PACKAGES += \
	e2fsck \
	libext2fs \
	libext2_blkid \
	libext2_uuid \
	libext2_profile \
	libext2_com_err \
	libext2_e2p

PRODUCT_PACKAGES += \
	LiveWallpapersPicker \
	LiveWallpapers \
	android.software.live_wallpaper.xml

# init.rc, kernel
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/init.rc:root/init.rc

# keylayout
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/axp20-supplyer.kl:system/usr/keylayout/axp20-supplyer.kl

# gps conf
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/gps.conf:system/etc/gps.conf	

# wifi conf
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf 

# mali lib so
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/egl/gralloc.sun4i.so:system/lib/hw/gralloc.sun4i.so \
	device/allwinner/crane-common/egl/libMali.so:system/lib/libMali.so \
	device/allwinner/crane-common/egl/libUMP.so:system/lib/libUMP.so \
	device/allwinner/crane-common/egl/egl.cfg:system/lib/egl/egl.cfg \
	device/allwinner/crane-common/egl/libEGL_mali.so:system/lib/egl/libEGL_mali.so \
	device/allwinner/crane-common/egl/libGLESv1_CM_mali.so:system/lib/egl/libGLESv1_CM_mali.so \
	device/allwinner/crane-common/egl/libGLESv2_mali.so:system/lib/egl/libGLESv2_mali.so \
	device/allwinner/crane-common/egl/libppbox-armandroid-r4-gcc44-mt-1.0.0.so:system/lib/libppbox-armandroid-r4-gcc44-mt-1.0.0.so \
	device/allwinner/crane-common/egl/libpplivep2p-jni.so:system/lib/libpplivep2p-jni.so \
	device/allwinner/crane-common/egl/libwebp.so:system/lib/libwebp.so \
	device/allwinner/crane-common/egl/libCTZip.so:system/lib/libCTZip.so

PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/preinstall.sh:/system/bin/preinstall.sh \
	device/allwinner/crane-common/data_resume.sh:/system/bin/data_resume.sh 

#for sop. add by Gary 2012.5.21
PRODUCT_COPY_FILES += \
    device/allwinner/crane-common/sop.sh:/system/bin/sop.sh

PRODUCT_PACKAGES += \
        PPPoEService

#premission feature
PRODUCT_COPY_FILES += \
	device/allwinner/crane-common/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.location.xml:system/etc/permissions/android.hardware.location.xml \
    frameworks/base/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.xml:system/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml
    
PRODUCT_PROPERTY_OVERRIDES += \
	dalvik.vm.heapsize=48m \
	ro.kernel.android.checkjni=0 \
	persist.sys.timezone=Asia/Hong_Kong \
	persist.sys.language=en \
	persist.sys.country=US \
	wifi.interface=wlan0 \
	wifi.supplicant_scan_interval=15 \
	debug.egl.hw=1 \
	ro.display.switch=1 \
	ro.opengles.version=131072 \
	persist.sys.strictmode.visual=0 \
	persist.sys.strictmode.disable=1 \
	hwui.render_dirty_regions=false \
	media.stagefright.maxsubfont=72 \
	audio.routing=2 \
	audio.defCurve=false \
    ro.chip_type=a10 \
    display.area_radio=display.area_radio

# pre-installed apks
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*.apk,$(LOCAL_PATH)/apk,system/preinstall)

#google service
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,$(LOCAL_PATH)/googleservice,system/app) \
	$(call find-copy-subdir-files,*,$(LOCAL_PATH)/apkinstall,system/app) \
	$(call find-copy-subdir-files,*,$(LOCAL_PATH)/apklib,system/lib)


# Overrides
PRODUCT_BRAND  := allwinners
PRODUCT_NAME   := crane_common
PRODUCT_DEVICE := crane-common


