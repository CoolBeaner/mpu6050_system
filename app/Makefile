# --- 路径定义 ---
# 外部头文件路径
# 若顶层没有传入默认使用
EXTRA_INC ?=
INC_DIR = include 
# 外部头文件
INC_DIR += $(EXTRA_INC) 
SRC_DIR = src
OBJ_DIR = build

TARGET  ?= mpu6050_app

# 若在板子上直接编译注释掉 CROSS_COMPILE
CROSS_COMPILE ?= arm-none-linux-gnueabihf-
CC      = $(CROSS_COMPILE)gcc
STRIP   = $(CROSS_COMPILE)strip

# --- 编译选项 ---
# -O2: 优化等级
# -Wall: 开启所有警告
# -pthread: 启用多线程支持
# -I$(INC_DIR): 指定头文件搜索路径
# -static: 测试阶段使用静态编译不需要移植依赖
CFLAGS  = -static -O2 -Wall -lpthread -I$(INC_DIR)

# --- 链接选项 ---
# -lm: 链接数学库 (algo.c 中使用了 math.h)
# -lpthread: 链接线程库
LDFLAGS = -lpthread -lm

# 获取源文件与目标文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(OBJ_DIR) $(TARGET)
	@echo "==== The app has been compiled successfully: $(TARGET) ===="

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
# 去除符号表以减小体积，调试时可注释掉
	# $(STRIP) $@  

# 编译 .c ---> .o 
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "==== The app cleaning is complete ===="

.PHONY: all clean