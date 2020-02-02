#
# Copyright (C) 2008 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=i2c-dog
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define KernelPackage/i2c-dog
  SUBMENU:=I2C support
  TITLE:=Adoge Perigh-based I2C device
  DEPENDS:=kmod-i2c-core +kmod-i2c-ralink
  FILES:=$(PKG_BUILD_DIR)/i2c-dog.ko
  KCONFIG:=
endef

define KernelPackage/i2c-dog/description
 Kernel module for register a custom i2c-dog platform device.
endef

EXTRA_KCONFIG:= \
	CONFIG_I2C_DOG=m

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \

MAKE_OPTS:= \
	ARCH="$(LINUX_KARCH)" \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	SUBDIRS="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,i2c-dog))
