# BoardConfig.mk
#
# Product-specific compile-time definitions.
#

include device/allwinner/crane-common/BoardConfigCommon.mk

BUILD_NUMBER := "4.0"

# image related
TARGET_NO_BOOTLOADER := true
TARGET_NO_RECOVERY := false
TARGET_NO_KERNEL := false

INSTALLED_KERNEL_TARGET := kernel
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_CMDLINE := console=ttyS0,115200 rw init=/init loglevel=5
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 512000000
#BOARD_USERDATAIMAGE_PARTITION_SIZE := 1073741824

# recovery stuff
TARGET_RECOVERY_PIXEL_FORMAT := "BGRA_8888"
TARGET_RECOVERY_UI_LIB := librecovery_ui_cubieboard
#TARGET_RECOVERY_UPDATER_LIBS :=

# Wifi related defines
#BOARD_WPA_SUPPLICANT_DRIVER := WEXT
#WPA_SUPPLICANT_VERSION      := VER_0_8_X

# Wifi chipset select
# usb: realtek "rtl8192cu" wext-sta, ralink "rt5370";
# sdio: "nanowifi"/"ar6302"/"usibcm4329"
#SW_BOARD_USR_WIFI := rtl8192cu
#SW_BOARD_USR_WIFI := rt5370
#SW_BOARD_USR_WIFI := hwmw269v2
#SW_BOARD_USR_WIFI := hwmw269v3
#SW_BOARD_USR_WIFI := usibm01a
#SW_BOARD_USR_WIFI := bcm40181
#SW_BOARD_USR_WIFI := bcm40183
#SW_BOARD_USR_WIFI := ar6003
#SW_BOARD_USR_WIFI := ar6302

#BOARD_HAVE_BLUETOOTH := true
#BOARD_HAVE_BLUETOOTH_BCM := true
#BOARD_HAVE_BLUETOOTH_CSR:= true
#SW_BOARD_HAVE_BLUETOOTH_RTK:= true
#SW_BOARD_HAVE_BLUETOOTH_NAME := hwmw269v2
#SW_BOARD_HAVE_BLUETOOTH_NAME := usibm01a
#SW_BOARD_HAVE_BLUETOOTH_NAME := bcm40183

# product code : "cubieboard"
PRODUCT_CODE := cubieboard

SW_BOARD_USES_GSENSOR_TYPE := usbremote
SW_BOARD_GSENSOR_DIRECT_X := true
SW_BOARD_GSENSOR_DIRECT_Y := false
SW_BOARD_GSENSOR_DIRECT_Z := true
SW_BOARD_GSENSOR_XY_REVERT := false

# realtek rtl8723as combo(wifi+bt) configuration
# set BOARD_HAVE_BLUETOOTH := true;
# set SW_BOARD_HAVE_BLUETOOTH_RTK:= true
# set BOARD_WIFI_VENDOR := realtek
# add bluetooth feature in the xml to display settings
# copy bt firmware rlt8723a_chip_b_cut_bt40_fw_asic_rom_patch.bin
# use nl80211 instead of wext in wpa_supplicant service@init.sun4i.rc

# realtek wifi support "sta/softap/wifi direct" function
# 1. enable BOARD_WIFI_VENDOR := realtek below
# 2. use nl80211 instead of wext interface in wpa_supplicant service@init.sun4i.rc
# 3. add android.hardware.wifi.direct.xml file to system/etc/permissions/
# 4. add interface for softap tether in the overylay/.../value/config.xml
# rtl8192cu: set SW_BOARD_USR_WIFI and BOARD_WLAN_DEVICE "rtl8192cu"
# rtl8188eu: set SW_BOARD_USR_WIFI and BOARD_WLAN_DEVICE "rtl8188eu"
# rtl8189es: set SW_BOARD_USR_WIFI and BOARD_WLAN_DEVICE "rtl8189es"
# rtl8723as: set SW_BOARD_USR_WIFI and BOARD_WLAN_DEVICE "rtl8723as"
BOARD_WIFI_VENDOR := realtek
ifeq ($(BOARD_WIFI_VENDOR), realtek)
    WPA_SUPPLICANT_VERSION := VER_0_8_X
    BOARD_WPA_SUPPLICANT_DRIVER := NL80211
    BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_rtl
    BOARD_HOSTAPD_DRIVER        := NL80211
    BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_rtl
    WIFI_DRIVER_MODULE_PATH          := "/system/lib/modules/8192cu.ko"
    WIFI_DRIVER_MODULE_NAME          := 8192cu

    SW_BOARD_USR_WIFI := rtl8192cu
    BOARD_WLAN_DEVICE := rtl8192cu

    #SW_BOARD_USR_WIFI := rtl8188eu
    #BOARD_WLAN_DEVICE := rtl8188eu

    #SW_BOARD_USR_WIFI := rtl8189es
    #BOARD_WLAN_DEVICE := rtl8189es

    #SW_BOARD_USR_WIFI := rtl8723as
    #BOARD_WLAN_DEVICE := rtl8723as

endif
