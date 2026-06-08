# 顶层 Makefile 绝对路径
export PROJ_ROOT := $(shell pwd)

export ARCH := arm

APP_NAME := mpu6050_app
DRV_NAME := mpu6050_drv

# 定义全局 include 路径
GLOBAL_INC := -I$(PROJ_ROOT)/include

KERNEL_DIR := /home/stu/projects/h3_transplant/kernel/linux-6.18.10

# 如果交叉编译工具不同，只要 ABI 相同 使用以下方案
# 需要修改：
# driver:  
# CROSS_COMPILE=$(DRV_TOOLCHAIN)
# app:
# CROSS_COMPILE=$(APP_TOOLCHAIN)
# clean:
# CROSS_COMPILE=$(DRV_TOOLCHAIN)
# 打开一下注释
# APP_TOOLCHAIN := arm-none-linux-gnueabihf-
# DRV_TOOLCHAIN := arm-none-linux-gnueabihf-

CROSS_COMPILE := arm-none-linux-gnueabihf-
SUBDIRS := driver app

# 声明伪目标
.PHONY: all clean $(SUBDIRS)

# 编译所有子目录
all: $(SUBDIRS)

driver:
	@echo "==== Building Driver ===="
	$(MAKE) -C driver \
		KDIR=$(KERNEL_DIR) \
		MODULE_NAME=$(DRV_NAME) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		EXTRA_INC="$(GLOBAL_INC)"	
	cp driver/$(DRV_NAME).ko $(PROJ_ROOT)

app:
	@echo "==== Building App ===="
	$(MAKE) -C app \
		TARGET=$(APP_NAME) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		EXTRA_INC="$(GLOBAL_INC)"
	cp app/$(APP_NAME) $(PROJ_ROOT)

clean:
	@echo "==== Cleaning All ===="
	$(MAKE) -C driver clean KDIR=$(KERNEL_DIR) CROSS_COMPILE=$(CROSS_COMPILE)
	$(MAKE) -C app clean
	rm -f $(PROJ_ROOT)/$(DRV_NAME).ko $(PROJ_ROOT)/$(APP_NAME)