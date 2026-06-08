# MPU6050 姿态识别系统

基于全志 H3（Orange Pi）的完整 MPU6050 姿态识别系统，覆盖 Linux 内核驱动、用户空间算法、嵌入式 1D-CNN 手势识别、USB HID 空中鼠标、实时 Web 可视化五个完整层次，代码均为自行实现，无第三方推理框架依赖。

---

## 目录

- [系统架构](#系统架构)
- [功能特性](#功能特性)
- [已知问题与限制](#已知问题与限制)
- [硬件要求](#硬件要求)
- [环境依赖](#环境依赖)
- [目录结构](#目录结构)
- [编译方法](#编译方法)
- [设备树配置](#设备树配置)
- [部署与运行](#部署与运行)
- [数据采集与模型训练](#数据采集与模型训练)
- [使用示例](#使用示例)
- [协议与接口说明](#协议与接口说明)
- [性能指标](#性能指标)

---

## 系统架构

```
MPU6050 硬件 (I2C @400kHz, 500Hz)
         │
         ▼
┌─────────────────────────────────────────┐
│          Linux 内核驱动层               │
│  ・硬中断 top-half 捕获 ktime 时间戳    │
│  ・线程化下半部读取 FIFO 数据           │
│  ・EMA 时间戳平滑 / 单调递增保证        │
│  ・I2C 热插拔监控 / 自动恢复            │
│  ・字符设备 /dev/mpu6050_0              │
└──────────────────┬──────────────────────┘
                   │ read() 系统调用
                   ▼
┌─────────────────────────────────────────┐
│          用户空间应用层（多线程）        │
│                                         │
│  Thread 1 传感器采集                    │
│    LSB 转换 → EMA 滤波 → 卡尔曼解算     │
│    → Pitch / Roll / Yaw 姿态角          │
│                                         │
│  Thread 2  UDP 遥测 (120Hz → PC)        │
│  Thread 3  USB HID 鼠标 / 键盘模拟      │
│  Thread 4  系统监控 (CPU/内存/温度)     │
│  Thread 5  1D-CNN 手势推理 (≈15Hz)      │
└──────────────────┬──────────────────────┘
                   │ UDP 9000
                   ▼
┌─────────────────────────────────────────┐
│      PC Python 后端 (server.py)         │
│  HTTP :8080 / UDP :9000 / SSE 推流      │
└──────────────────┬──────────────────────┘
                   │ SSE 事件流
                   ▼
┌─────────────────────────────────────────┐
│      浏览器前端 (index.html)            │
│  Three.js 3D 姿态 / 数据仪表 / 手势 UI  │
└─────────────────────────────────────────┘
```

---

## 功能特性

### 内核驱动

- **微秒级时间戳**：硬中断 top-half 通过 `ktime_get_boottime_ns()` 在最短路径内捕获时刻，再经 EMA 平滑消除 CPU 调度抖动，帧间时间戳单调递增、均匀间隔，精度约 0.2 ms。
- **双层 IRQ 模型**：top-half 仅记录时刻，线程化下半部负责 I2C FIFO 读取与数据打包，避免长时占用硬中断上下文。
- **硬件 FIFO 管理**：使用 MPU6050 内置 1024 字节 FIFO（@500Hz 约 146 ms 缓冲），软件侧 kfifo 环形缓冲 256 帧，支持阻塞读。
- **I2C 热插拔**：后台监控线程每 5 秒检查 WHO_AM_I / CONFIG 寄存器"指纹"，检测到设备重启后自动调用 `i2c_recover_bus()` 并重新初始化（注意：I2C 控制器锁死场景尚未覆盖，见[已知问题](#已知问题与限制)）。
- **多设备支持**：支持 I2C 地址 0x68 / 0x69，通过 minor_lock 保护并发探测。
- **驱动可独立使用**：仅需按说明修改头文件路径即可在其他项目中复用。

### 算法层

- **EMA 滤波**：加速度系数 0.2，陀螺仪系数 0.1，冷启动首帧直接赋值防止延迟。
- **卡尔曼滤波**：2×2 协方差矩阵，Pitch/Roll 以加速度计重力向量为观测量修正陀螺积分漂移；Yaw 仅陀螺积分（无地磁计），低于 0.2°/s 死区时启用指数衰减（×0.999/帧）。
- **零偏校准**：采集 500 帧静止数据求均值，结果持久化至 `/data/mpu6050.conf`，热启动直接注入卡尔曼初始偏差。

### 手势识别（1D-CNN）

- **网络结构**：3 层 Conv1D（32→64→128 滤波器，核大小 5）+ MaxPool，Flatten，FC(256)→FC(8)，全部 ReLU，输出 Softmax。
- **输入**：256 点 × 6 轴（Ax/Ay/Az/Gx/Gy/Gz）滑动窗口，步长 16，推理频率约 15 Hz。
- **预处理**：Z-Score 标准化或物理尺度缩放（加速度 ÷1.5，陀螺仪 ÷90°/s）。
- **置信度阈值**：0.99，低于阈值判定为背景。
- **权重存储**：静态 C 数组嵌入头文件，无动态加载，无外部推理框架。

#### 支持手势（8 类）

| ID | 名称 | 描述 |
|----|------|------|
| 0 | 背景 | 无动作 / 杂波 |
| 1 | CIRCLE | 顺时针画圆 |
| 2 | INV_CIRCLE | 逆时针画圆 |
| 3 | EQU_TRIANGLE | 正三角 |
| 4 | INV_TRIANGLE | 倒三角 |
| 5 | LINE | 横线 |
| 6 | FLASH | 闪电 |
| 7 | INFINITY | 无穷符号 ∞ |

#### 支持连招（5 种）

| ID | 名称 | 序列 |
|----|------|------|
| 101 | 雷霆万钧 | ∞ → 逆圆 → 正三角 |
| 102 | 绝对零度 | ∞ → 逆圆 → 倒三角 |
| 103 | 裂地冲击 | 倒三角 → 正圆 |
| 104 | 神光审判 | 正三角 → 正圆 |
| 105 | 电磁风暴 | 闪电 → 正圆 |

连招超时 2000 ms，消抖期 16 帧，含前缀扣留逻辑（2 连是 3 连前缀时等待第 3 个动作）。

### USB HID 空中鼠标

- 通过 Linux USB Gadget ConfigFS 将板卡模拟为复合 HID 设备（鼠标 `/dev/hidg0` + 键盘 `/dev/hidg1`）。
- 鼠标模式：Yaw → dX，Pitch → dY，灵敏度 0.25，死区 0.5°/s。
- 手势模式：识别结果通过键盘 HID 上报对应按键，连招映射数字键 1–5。
- **注意**：物理按键功能（用于触发手势采集/上报）尚未实现，见[已知问题](#已知问题与限制)。

### Web 可视化

- Python HTTP 服务器（端口 8080）+ SSE 实时推流。
- 前端 Three.js 渲染 3D 姿态模型，实时显示 Pitch/Roll/Yaw。
- 数据仪表盘：加速度、角速度、温度、CPU/内存/SOC 温度。
- 一键发送零偏校准命令、切换工作模式（IDLE / 鼠标 / 手势）。
- 手势识别界面：点击后弹出演示窗口，画出对应动作触发动画效果。

---

## 已知问题与限制

| 问题 | 状态 | 说明 |
|------|------|------|
| I2C 控制器锁死 | 未修复 | 热插拔监控调用了 `i2c_recover_bus()`，但全志 H3 I2C 控制器锁死时该接口不足以复活控制器，需补充控制器级复位初始化序列 |
| Yaw 漂移 | 已知限制 | 无地磁计，Yaw 轴仅靠陀螺积分，长时间运行存在漂移，死区衰减仅缓解 |
| 物理按键 | 未实现 | 空中鼠标的物理按键用于在手势模式下主动触发采集/确认，当前未接入 |
| 连招键盘上报 | 实现中 | 手势/连招通过键盘 HID 键码上报，前端动画响应已完成，但部分边界动作仍有误判 |

---

## 硬件要求

| 组件 | 规格 |
|------|------|
| 主控板 | Orange Pi（全志 H3，Cortex-A7 四核） |
| 传感器 | MPU6050（I2C，AD0 接 GND → 地址 0x68） |
| 连接 | MPU6050 INT 引脚接 H3 GPIO 并配置为边沿触发中断 |
| 供电 | 5V / ≥2A |
| 存储 | ≥256 MB 可用空间（用于应用程序和校准文件） |
| USB | OTG 端口（用于 USB Gadget HID 模拟） |

---

## 环境依赖

### 交叉编译主机（Linux）

```
arm-none-linux-gnueabihf-    交叉编译工具链（ARM Cortex-A7 硬浮点）
Linux 内核源码               linux-6.18.10（需完整配置并编译过模块）
make / gcc                   构建工具
```

工具链获取示例（以 Linaro 为例）：

```bash
# 下载并解压到 /opt/cross/
wget https://releases.linaro.org/components/toolchain/binaries/latest-7/arm-linux-gnueabihf/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf.tar.xz
tar -xJf gcc-linaro-7.5.0-*.tar.xz -C /opt/cross/
export PATH=/opt/cross/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin:$PATH
```

### 内核配置

在内核 `.config` 中需启用以下选项：

```
CONFIG_USB_GADGET=y              # USB Gadget 框架
CONFIG_USB_CONFIGFS=y            # ConfigFS 接口
CONFIG_USB_CONFIGFS_F_HID=y      # HID 功能模块
CONFIG_I2C_CHARDEV=y             # I2C 字符设备（可选，调试用）
CONFIG_KEYS=y
```

### 目标板（Orange Pi / Armbian）

```
Python 3.x                   PC 端 Web 后端（无需在板上运行）
/data/ 目录可写              用于存储校准文件 mpu6050.conf
USB OTG 驱动                 确认 /sys/kernel/config 存在
```

### PC 端（Web 后端）

```
Python 3.8+
无额外第三方依赖（仅标准库 http.server、socket、struct、threading）
```

数据采集与训练额外依赖：

```bash
pip install torch numpy matplotlib scipy
```

---

## 目录结构

```
mpu6050_system_release/
├── driver/                     内核驱动模块
│   ├── mpu6050_drv.c           驱动主文件（中断、FIFO、热插拔）
│   └── include/
│       ├── mpu6050.h           驱动内部数据结构
│       └── mpu6050_uapi.h      用户空间 ioctl / 数据结构接口
│
├── app/                        用户空间应用程序
│   ├── src/
│   │   ├── main.c              多线程入口
│   │   ├── algo.c              EMA 滤波 / 卡尔曼滤波 / 零偏校准
│   │   ├── mpu_io.c            设备 IO / LSB→物理量转换
│   │   ├── network.c           UDP 遥测发送 / 指令接收
│   │   ├── usb_hid.c           HID 鼠标 / 键盘上报
│   │   ├── ai_inference.c      1D-CNN 推理 / 连招识别
│   │   └── gesture_fifo.c      手势滑动窗口环形缓冲
│   ├── include/
│   │   ├── common.h            全局共享数据结构 SharedData_t
│   │   ├── algo.h              算法接口
│   │   └── gesture_model_weights.h  CNN 权重（静态 C 数组）
│   └── Makefile                应用编译脚本
│
├── h3_tools/
│   └── usb_gadget_init.sh      USB Gadget ConfigFS 初始化脚本
│
├── pc_web_server/
│   ├── server.py               Python HTTP + UDP + SSE 后端
│   ├── server_for_test.py      转发到 VOFA+ 的测试服务器
│   └── web_gui/
│       ├── index.html          主前端（Three.js 3D 姿态可视化）
│       └── fluid.html          粒子流体效果演示页
│
├── pc_tools/
│   └── data_acquisition_for_ai/
│       ├── plot_raw_gesture.py          原始数据可视化
│       ├── clean_and_merge.py           人工标注合并
│       ├── auto_stride_invalid_slicer.py 自动相位偏移切片
│       ├── bbox_perfect_slicer.py       人工标注切片
│       ├── view_npy.py                  清洗后数据可视化
│       ├── view_raw_vs_normalized_data.py 预处理对比
│       ├── train_gesture_1dcnn.py       PyTorch 1D-CNN 训练
│       └── export_to_c.py              权重导出为 C 头文件
│
└── Makefile                    顶层构建入口
```

---

## 编译方法

### 1. 修改顶层 Makefile

打开 `Makefile`，确认以下变量指向正确路径：

```makefile
ARCH          = arm
CROSS_COMPILE = arm-none-linux-gnueabihf-
KERNEL_DIR    = /path/to/linux-6.18.10    # 替换为实际内核源码路径
```

### 2. 一键编译驱动 + 应用

```bash
make
```

产物：

| 文件 | 说明 |
|------|------|
| `driver/mpu6050_drv.ko` | 内核模块 |
| `app/mpu6050_app` | 用户空间可执行文件（默认静态编译） |

### 3. 动态编译（可选）

默认为静态编译（`-static`），如需动态编译，修改 `app/Makefile`：

```makefile
# 注释掉或删除下面这行
CFLAGS += -static
```

重新编译：

```bash
make clean && make
```

> **注意**：动态编译时需确保目标板上存在对应版本的 libc / libpthread / libm。

### 4. 单独编译驱动

```bash
cd driver
make -C /path/to/linux-6.18.10 M=$(pwd) modules ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabihf-
```

### 5. 驱动独立使用

若需在其他项目中单独使用此驱动，只需修改 `driver/mpu6050_drv.c` 顶部的头文件引用路径，将 `include/mpu6050.h` 和 `include/mpu6050_uapi.h` 调整为你的项目目录，其余代码无需改动。

---

## 设备树配置

设备树文件已包含在 `h3_tools/sun8i-h3-orangepi-pc.dts`，需在编译内核前替换（或合并）到内核源码中，否则驱动加载后 `probe` 不会被调用。

### 引脚接线

| MPU6050 引脚 | H3 引脚 | 说明 |
|-------------|---------|------|
| VCC | 3.3V | 供电 |
| GND | GND | 共地 |
| SCL | PA11 | I2C0 时钟，400kHz |
| SDA | PA12 | I2C0 数据 |
| INT | PA7 | 中断，上升沿触发，内部上拉 |
| AD0 | GND | I2C 地址选 0x68 |

### 关键配置说明

**USB OTG 强制 peripheral 模式**：将 USB0 口切换为 Device 模式，是 USB Gadget HID 模拟的前提。同时禁用了 ehci0/ohci0（USB host），避免两者冲突。

```dts
&usb_otg {
    dr_mode = "peripheral";   /* 不能是 "otg" 或 "host" */
    status = "okay";
};
```

**I2C0 双 pinctrl（用于总线恢复）**：配置了两套 pinctrl：正常模式（I2C 功能复用）和 GPIO 模式（用于软件位拼 bit-bang 恢复死锁总线）。当 I2C 控制器锁死时，驱动可将引脚切换为 GPIO，手动发出 STOP 条件解锁。

```dts
&i2c0 {
    pinctrl-0 = <&i2c0_pins>;        /* I2C 复用模式（正常工作） */
    pinctrl-1 = <&i2c0_gpio_pins>;   /* GPIO 模式（总线恢复用） */
    scl-gpios = <&pio 0 11 ...>;
    sda-gpios = <&pio 0 12 ...>;
};
```

> 注：当前驱动的 I2C 控制器锁死恢复尚未完整实现（见[已知问题](#已知问题与限制)），此 pinctrl 为后续修复预留。

**MPU6050 中断节点**：

```dts
mpu6050@68 {
    compatible = "invensense,mpu6050";
    reg = <0x68>;
    interrupt-parent = <&pio>;
    interrupts = <0 7 IRQ_TYPE_EDGE_RISING>;  /* PA7，上升沿 */
};
```

### 应用到内核源码

```bash
# linux-6.18.10 中 H3 Orange Pi PC 的 DTS 路径
DTS_PATH=arch/arm/boot/dts/allwinner/sun8i-h3-orangepi-pc.dts

# 直接替换（推荐，文件已是完整 DTS）
cp h3_tools/sun8i-h3-orangepi-pc.dts /path/to/linux-6.18.10/$DTS_PATH
```

### 编译 DTB

```bash
cd /path/to/linux-6.18.10
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabihf- dtbs
```

产物：`arch/arm/boot/dts/allwinner/sun8i-h3-orangepi-pc.dtb`

### 烧写 DTB 到板上

```bash
# 方式一：直接替换 boot 分区中的 DTB（板上执行）
mount /dev/mmcblk0p1 /mnt/boot
cp sun8i-h3-orangepi-pc.dtb /mnt/boot/
umount /mnt/boot
reboot

# 方式二：通过 SCP 传输后替换
scp arch/arm/boot/dts/allwinner/sun8i-h3-orangepi-pc.dtb \
    root@<board_ip>:/boot/
```

重启后验证设备树生效：

```bash
# 应看到 mpu6050 节点
ls /sys/bus/i2c/devices/
# 预期包含：0-0068

dmesg | grep mpu6050
```

---

## 部署与运行

### 步骤 1：传输文件到板上

```bash
scp driver/mpu6050_drv.ko root@<board_ip>:/root/
scp app/mpu6050_app       root@<board_ip>:/root/
scp h3_tools/usb_gadget_init.sh root@<board_ip>:/root/
```

### 步骤 2：加载内核驱动

```bash
# 在板上执行
insmod /root/mpu6050_drv.ko

# 确认设备节点出现
ls /dev/mpu6050_*
# 预期输出：/dev/mpu6050_0

# 查看驱动日志
dmesg | tail -30
```

正常输出示例：

```
mpu6050: probe device at I2C address 0x68
mpu6050: WHO_AM_I=0x68, hardware OK
mpu6050: IRQ line acquired, GPIO xx
mpu6050: sample rate 500Hz, DLPF 42Hz
```

### 步骤 3：初始化 USB Gadget

```bash
chmod +x /root/usb_gadget_init.sh
/root/usb_gadget_init.sh

# 确认 HID 设备节点
ls /dev/hidg*
# 预期：/dev/hidg0 (鼠标)  /dev/hidg1 (键盘)
```

> USB Gadget 需要内核已启用 `CONFIG_USB_GADGET` 和 `CONFIG_USB_CONFIGFS_F_HID`，板子需通过 USB OTG 口连接到 PC。

### 步骤 4：启动应用程序

```bash
# 后台运行，输出重定向到日志
/root/mpu6050_app &

# 或前台运行查看实时日志
/root/mpu6050_app
```

首次运行会执行零偏校准提示，**请将板子放置在水平静止位置**，等待约 2 秒（采集 500 帧）完成校准，结果保存至 `/data/mpu6050.conf`。

### 步骤 5：启动 PC 端 Web 后端

```bash
cd pc_web_server
python3 server.py
```

修改 `server.py` 中的目标 IP（默认接收来自 `192.168.2.x` 的 UDP 包）以匹配你的网络环境：

```python
UDP_RECV_PORT = 9000    # 遥测数据接收端口
UDP_SEND_PORT = 9001    # 指令发送端口
HTTP_PORT     = 8080    # Web UI 端口
```

### 步骤 6：打开浏览器

```
http://localhost:8080
```

界面功能：

- **3D 姿态模型**：实时显示 MPU6050 当前姿态
- **模式切换**：IDLE / 鼠标模式 / 手势模式
- **零偏校准**：点击按钮发送校准命令（板子需保持静止）
- **系统监控**：CPU 占用率 / 内存占用率 / SOC 温度
- **手势识别**：切换到手势模式后点击"手势识别"按钮，弹出演示界面

---

## 数据采集与模型训练

如需采集新手势数据并训练自定义模型，按以下流程操作。

### 1. 开启数据采集

在 `app/include/common.h`（或对应宏定义文件）中启用采集宏：

```c
#define ENABLE_DATA_COLLECTION  1   // 打开此宏开始采集
```

重新编译并运行，程序会将原始 6 轴数据写入 CSV 文件。每完成一个动作后手动停止或使用时间段截断。

### 2. 可视化原始数据

```bash
cd pc_tools/data_acquisition_for_ai
python3 plot_raw_gesture.py <your_data.csv>
```

### 3. 数据切片（二选一）

**方案 A — 自动切片**（推荐用于动作边界清晰的场景）：

```bash
python3 auto_stride_invalid_slicer.py <your_data.csv> --label 1 --output ./npy_out/
```

**方案 B — 人工标注切片**（适合动作边界模糊时）：

```bash
python3 bbox_perfect_slicer.py <your_data.csv> --label 1 --output ./npy_out/
# 程序会展示波形，需人工在图上点击标注起止点
```

切片参数说明：
- `--label`：手势类别 ID（0–7）
- `--output`：输出 `.npy` 文件目录
- 每个切片为 256 × 6 的 numpy 数组

### 4. 数据清洗与合并

```bash
python3 clean_and_merge.py ./npy_out/ --output dataset.npy
```

查看清洗后数据：

```bash
python3 view_npy.py dataset.npy
python3 view_raw_vs_normalized_data.py dataset.npy   # 对比预处理前后
```

### 5. 训练模型

```bash
python3 train_gesture_1dcnn.py \
    --data dataset.npy \
    --epochs 40 \
    --batch 64 \
    --lr 0.001
```

训练完成后在当前目录生成 `gesture_model.pth`。

### 6. 导出权重为 C 头文件

```bash
python3 export_to_c.py gesture_model.pth \
    --output ../../app/include/gesture_model_weights.h
```

重新编译应用程序即可更新模型。

> **注意**：数据切片（步骤 3）需要人工参与观察波形质量，自动切片的结果也建议通过 `view_npy.py` 抽检确认无异常帧。

---

## 使用示例

### 示例 1：查看实时姿态

1. 板上加载驱动，运行 `mpu6050_app`
2. PC 端运行 `server.py`
3. 浏览器打开 `http://localhost:8080`，观察 3D 模型随板子旋转

### 示例 2：空中鼠标

1. 板子通过 USB OTG 连接 PC
2. 执行 `usb_gadget_init.sh` 后 PC 识别出 HID 鼠标
3. Web 界面切换模式到 **鼠标模式**
4. 晃动板子控制鼠标指针

### 示例 3：手势识别

1. Web 界面切换到 **手势模式**
2. 点击界面上的"手势识别"按钮，弹出动作演示窗口
3. 参照演示画出对应图形（如顺时针画圆），触发动画效果
4. 识别结果同时通过 USB 键盘 HID 上报对应键码至 PC

### 示例 4：连招触发

在手势模式下连续完成组合动作（间隔 <2 秒），例如：

```
画 ∞（无穷符号） → 逆时针画圆 → 正三角
```

触发连招 **雷霆万钧**（ID 101），界面弹出对应特效动画，同时上报数字键 `1`。

### 示例 5：零偏校准

```bash
# 方式一：通过 Web 界面点击"零偏校准"按钮
# 方式二：通过 UDP 指令
echo -ne '\x02\x00' | nc -u <board_ip> 9001
```

板子静置约 2 秒后完成校准，结果写入 `/data/mpu6050.conf` 并在下次启动时自动加载。

### 示例 6：连接 VOFA+ 调试

PC 端启动测试服务器：

```bash
cd pc_web_server
python3 server_for_test.py
```

在 VOFA+ 中添加 UDP 数据源，地址 `127.0.0.1:9500`，协议选择 FireWater，即可看到 6 轴实时波形。

---

## 协议与接口说明

### UDP 遥测包格式

方向：板 → PC，端口 9000，频率 120 Hz。

```
偏移   类型      字段
 0     uint32   packet_seq      包序列号
 4     double   dt              采样间隔（秒）
12     float    temperature     传感器温度（°C）
16     float    pitch           俯仰角（°）
20     float    roll            横滚角（°）
24     float    yaw             偏航角（°）
28     float    accel_x         加速度 X（g）
32     float    accel_y         加速度 Y（g）
36     float    accel_z         加速度 Z（g）
40     float    cpu_usage       CPU 占用率（%）
44     float    mem_usage       内存占用率（%）
48     float    soc_temp        SOC 温度（°C）
52     uint8    current_mode    0=IDLE 1=鼠标 2=手势
53     uint16   checksum        CRC16-CCITT（初值 0xFFFF）
```

### UDP 指令包格式

方向：PC → 板，端口 9001。

```
Byte 0   cmd_type    0x01=切换模式  0x02=触发校准
Byte 1   cmd_val     模式值（0/1/2）或 0x00
```

### 驱动字符设备接口

```c
// 打开设备
int fd = open("/dev/mpu6050_0", O_RDONLY);

// 阻塞读取一帧（24 字节）
struct mpu6050_frame {
    struct mpu6050_sensor_data {
        int16_t accel[3];  // Ax Ay Az (LSB)
        int16_t temp;      // 温度 (LSB)
        int16_t gyro[3];   // Gx Gy Gz (LSB)
    } data;                // 14 字节
    uint64_t timestamp;    // 纳秒时间戳（8 字节）
};                         // 总 24 字节，__packed

read(fd, &frame, sizeof(frame));
```

---

## 性能指标

| 指标 | 数值 | 备注 |
|------|------|------|
| 采样率 | 500 Hz | MPU6050 DLPF@42Hz |
| 时间戳精度 | ≈ 0.2 ms | EMA 平滑后帧间抖动 |
| 硬件 FIFO 缓冲 | 146 ms | @500Hz，1024 字节 |
| 网络遥测频率 | 120 Hz | 500Hz 4 分频 |
| AI 推理频率 | ≈ 15 Hz | 256 点窗口，步长 16 |
| 手势置信度阈值 | 0.99 | 低于此值判为背景 |
| 连招超时 | 2000 ms | 超时后历史缓冲清零 |
| 应用内存占用 | < 50 MB | 静态数组为主 |
| 编译产物 | 静态链接 | 无 libc 版本依赖 |
