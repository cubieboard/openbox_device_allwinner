#this file act as the "full_base.mk"

PRODUCT_PACKAGES := \
    drmserver \
    libdrmframework \
    libdrmframework_jni \
    libfwdlockengine \
    OpenWnn \
    libWnnEngDic \
    libWnnJpnDic \
    libwnndict \
    WAPPushManager

PRODUCT_PACKAGES += \
    AlarmProvider \
    Bluetooth \
    CertInstaller \
    DrmProvider \
    Email \
    Exchange \
    Gallery2 \
    LatinIME \
    Launcher2 \
    Music \
    MusicFX \
    Provision \
    QuickSearchBox \
    Sync \
    SystemUI \
    Updater \
    CalendarProvider \
    SyncProvider \
    bluetooth-health \
    hostapd \
    wpa_supplicant.conf

# Additional settings used in all AOSP builds
PRODUCT_PROPERTY_OVERRIDES := \
    ro.com.android.dateformat=MM-dd-yyyy \
    ro.config.ringtone=Ring_Synth_04.ogg \
    ro.config.notification_sound=pixiedust.ogg

# Get some sounds
$(call inherit-product-if-exists, frameworks/base/data/sounds/AllAudio.mk)

# Get the TTS language packs
$(call inherit-product-if-exists, external/svox/pico/lang/all_pico_languages.mk)

# Get a list of languages.
$(call inherit-product, $(SRC_TARGET_DIR)/product/locales_full.mk)

# Get everything else from the parent package
PRODUCT_PACKAGES += \
    icu.dat

PRODUCT_PACKAGES += \
    librs_jni \
    libvideoeditor_jni \
    libvideoeditorplayer \
    libvideoeditor_core

PRODUCT_PACKAGES += \
    audio.primary.default \
    audio_policy.default

PRODUCT_COPY_FILES += \
        system/bluetooth/data/audio.conf:system/etc/bluetooth/audio.conf \
        system/bluetooth/data/auto_pairing.conf:system/etc/bluetooth/auto_pairing.conf \
        system/bluetooth/data/blacklist.conf:system/etc/bluetooth/blacklist.conf \
        system/bluetooth/data/input.conf:system/etc/bluetooth/input.conf \
        system/bluetooth/data/network.conf:system/etc/bluetooth/network.conf \
        frameworks/base/media/libeffects/data/audio_effects.conf:system/etc/audio_effects.conf \
        device/allwinner/crane-common/smb.conf:system/etc/smb.conf

$(call inherit-product-if-exists, frameworks/base/data/fonts/fonts.mk)
$(call inherit-product-if-exists, external/lohit-fonts/fonts.mk)
$(call inherit-product-if-exists, frameworks/base/data/keyboards/keyboards.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)

