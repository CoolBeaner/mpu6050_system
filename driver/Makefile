# 替换为 Linux 内核源码的绝对路径
# 若在板子上本地编译  
# 使用/lib/modules/$(uname -r)/build
KDIR ?= /home/stu/projects/h3_transplant/kernel/linux-6.18.10

# 若顶层没有传入默认使用
CROSS_COMPILE ?= arm-none-linux-gnueabihf-

# 模块名
MODULE_NAME ?= mpu6050_drv

# 外部头文件路径
# 若顶层没有传入默认使用
EXTRA_INC ?=

# 指定编译模块
obj-m += $(MODULE_NAME).o

# 外部头文件路径
ccflags-y += $(EXTRA_INC)

# 私有头文件路径
ccflags-y += -I$(src)/include

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) CROSS_COMPILE=$(CROSS_COMPILE) clean
