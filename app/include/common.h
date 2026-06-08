#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "share_uapi.h"

/* --- 系统配置常量 --- */
#define IMU_SAMPLE_RATE_HZ     500       // 采样频率
#define ALPHA_ACCEL            0.2f      // 加速度计 EMA 滤波系数 (越小越平滑，延迟越大)
#define ALPHA_GYRO             0.1f      // 陀螺仪 EMA 滤波系数
#define CALIB_SAMPLE_COUNT     500       // 校准采样点数 (约1秒)
#define NETWORK_SEND_HZ        120       // 发包帧率 用于降帧
#define AI_SAMPLE_RATE_HZ      100       // 1D-CNN 模型期望的时序输入频率 (Hz)

/* --- FIFO 参数配置 --- */
#define FIFO_SIZE              256       // 滑动窗口总长度 约1.28秒
#define GESTURE_CHANNELS       6         // 6轴通道: 3轴加速度 + 3轴陀螺仪
#define FIFO_STRIDE            16        // 滑动步长：新攒满 16 个点触发一次 AI 推理

/* 时间转换 us->s  us->ms*/
#define SEC_CHENGE             1e9
#define MS_CHENGE              1e6

/* 弧度转角度 */
#define DEG_TO_RAD              (0.0174533f)
/* 角度转弧度 */
#define RAD_TO_DEG              (57.29578f)
#define RADIANS_TO_DEGREES(rad) ((rad) * RAD_TO_DEG)

/* 
 * Yaw 过滤 (Deadzone)
 * 0.2f 阈值 代表 0.2°/s 
 *可根据情况调整为 0.15f ~ 0.5f 
 */
#define GYRO_Z_DEADZONE          (0.2f)    

/* 推理窗口大小 暂时没用 */
#define WINDOW_SIZE              FIFO_SIZE


#ifdef DATA_ACQUISITION

/* 串口控制线程允许的最大连续录制时长 (秒) */
#define MAX_RECORD_TIME_SEC      300    

/* 
 * 一次可采集最大点数
 * 共6轴 每组数据代表一个点
 */
#define MAX_RECORD_POINTS          (MAX_RECORD_TIME_SEC * AI_SAMPLE_RATE_HZ) 

#endif

/* 网络发包分频器计算公式 */
#if (NETWORK_SEND_HZ == 0)
    #error " NETWORK_SEND_HZ CAN'T BE '0'"
#else
    #define NET_SEND_DIVIDER       (IMU_SAMPLE_RATE_HZ / NETWORK_SEND_HZ)
#endif

/* 计算重采样抽稀系数：500 / 100 = 5 */
#if (AI_SAMPLE_RATE_HZ == 0)
    #error " AI_SAMPLE_RATE_HZ CAN'T BE '0'"
#else
    #define DOWNSAMPLE_FACTOR      (IMU_SAMPLE_RATE_HZ / AI_SAMPLE_RATE_HZ)
#endif

/* --- 驱动原始数据结构 (与驱动层格式对应) --- */
typedef struct mpu6050_frame RawData_t;

/* --- 手势识别结果 枚举 --- */
typedef enum {
    GESTURE_NONE = 0,      // 无动作
    GESTURE_CIRCLE,        // 正圈
    GESTURE_INV_CIRCLE,    // 逆圈
    GESTURE_EQU_TRIANGLE,  // 正三角
    GESTURE_INV_TRIANGLE,  // 逆三角
    GESTURE_LINE,          // 横线
    GESTURE_FLASH,         // 闪电
    GESTURE_INFINITY       // 无穷
} GestureResult_E;

 /**
 * @brief 连招 ID 枚举
 */
typedef enum {
    COMBO_NONE              = 0,
    COMBO_LEITING_WANJUN    = 101,  // 无穷 -> 逆圆 -> 正三角 
    COMBO_JUEDUI_LINGDU     = 102,  // 无穷 -> 逆圆 -> 倒三角 
    COMBO_LIEDI_CHONGJI     = 103,  // 倒三角 -> 正圆 
    COMBO_SHENGUANG_SHENPAN = 104,  // 正三角 -> 正圆 
    COMBO_DIANCI_FENGBAO    = 105   // 闪电 -> 正圆 
} ComboActionId_E;

/* 定义 手势识别结果 共享数据结构体 */
typedef struct {
    uint32_t update_seq;                // 序列号/版本号，每触发一次新动作就 +1
    GestureResult_E predicted_class;    // AI 推理出的手势类别 (0-7)
    ComboActionId_E combo_action;       // 连招动作代码 (0代表无连招)
    pthread_mutex_t gesture_lock;
} GestureShareData_S;

/* 
 * 卡尔曼滤波内部参数 
 * 针对 Pitch 和 Roll 各维护一套
 */
typedef struct {
    float angle; // 状态：角度
    float bias;  // 状态：陀螺仪偏差
    float P[2][2]; // 误差协方差矩阵
} Kalman_t;

/**
 * @brief 手势识别的环形缓冲区（FIFO）结构体
 *        从线程 1 中读到的原数据 (非ADC数据 未滤波)
 */
typedef struct {
    float data[FIFO_SIZE][GESTURE_CHANNELS]; // 128行 * 6轴的循环阵列
    int tail;                                // 队列指针 指向下一个要写入的位置
    int count;                               // 当前缓冲区里的有效点数
    bool is_full;                            // 标记缓冲区是否被填满
    pthread_mutex_t fifo_lock;               // 专属互斥锁
} GestureFifo_t;

/* --- 模式定义 --- */
typedef enum {
    MODE_IDLE = 0,    // 仅数据传输，不执行应用逻辑
    MODE_MOUSE,       // 虚拟鼠标模式
    MODE_GESTURE      // 手势识别模式
} Mode_e;

/* --- 基础数学结构体 --- */
typedef struct {
    float x;
    float y;
    float z;
} Vector3_t;

typedef struct {
    float pitch;
    float roll;
    float yaw;
} Attitude_t;

/* --- 算法使用的物理单位结构 (供 T1 处理后存入全局) --- */
typedef struct {
    double timestamp;      // 转换为秒 (double) 内核打下的高精度时间戳
    Vector3_t accel;       // 转换为 g 转换后的加速度 (单位: g)
    Vector3_t gyro;        // 转换为 deg/s  转换后的角速度 (单位: deg/s)
    float temperature;     // 转换为 摄氏度
    double dt;             // 采样间隔
} ProcessedData_t;

/* --- 校准信息结构体 --- */
typedef struct {
    Vector3_t gyro_offset; // 陀螺仪零偏
    Vector3_t accel_offset;// 加速度计零偏
    int calib_count;       // 当前累计样本数
    bool is_calibrating;   // 是否正在执行校准
    bool is_valid;         // 校准数据是否有效
} CalibInfo_t;

/* --- 系统状态结构体 --- */
typedef struct {
    float cpu_usage;       // CPU 占用率 (0.0 - 100.0)
    float mem_usage;       // 内存占用率
    float soc_temp;        // H3 芯片温度
} SysStatus_t;

/* --- UDP 遥测数据包 (发往 PC, 必须强制字节对齐) --- */
typedef struct __attribute__((packed)) {
    uint32_t packet_seq;   // 包序列号
    double dt;             // 真实采样间隔
    float temperature;     // mpu6050芯片温度
    Attitude_t attitude;   // 卡尔曼解算后的姿态
    Vector3_t raw_accel;   // 原始加速度 (用于上位机绘图)
    SysStatus_t sys;       // 系统状态
    uint8_t current_mode;  // 当前板子运行模式
    uint16_t checksum;     // CRC 校验码
} TelemetryPkg_t;

/* --- 核心全局共享结构体 --- */
typedef struct {
    /* 时间戳差值 */
    double dt;
    
    /* mpu6050芯片温度 */
    float temperature;  

    /* 
     * 数据版本号 
     * 解决网络及usb发包帧率问题
     * 如果发的快会重复发数据
     * 如果加固定延迟会有延迟
     * 每次更新数据update_seq++
     */
    uint32_t update_seq;

    /* EMA滤波数据 (由 T1 更新) */
    Vector3_t ema_accel;
    Vector3_t ema_gyro;

    /* 未EMA滤波数据 (由 T1 更新) */
    Vector3_t accel;
    Vector3_t gyro;

    /* 姿态解算结果 (由 T1 更新) */
    Attitude_t attitude;

    /* 系统状态与控制 (由 T4 和 网络指令 更新) */
    SysStatus_t sys_status;
    CalibInfo_t calib_info;
    Mode_e mode;

    pthread_mutex_t g_data_lock;
} SharedData_t;

/* --- 全局变量声明 (在 main.c 中定义) --- */

extern SharedData_t  g_data;
extern volatile bool g_keep_running;

/* 系统运行标志位 */
extern volatile bool   g_keep_running;   

#ifdef DATA_ACQUISITION
/* 
 * 临时存放采集的数据 
 * MAX_RECORD_POINTS 组数据
 * 每组 6 轴数据
 */
extern float g_record_big_buf[MAX_RECORD_POINTS][6]; 

/* 纪录当前录制到第几个点了 */
extern volatile int g_record_idx;        

/* 数据采集状态控制
 * 0: 停止录制
 * 1: 正在录制 
 */
extern volatile bool g_record_flag;

#else

/* 手势识别原始数据环形缓冲区 */
extern GestureFifo_t   g_gesture_fifo;  

/* 手势识别结果共享结构体 */  
extern GestureShareData_S g_gesture_data;

/* 名字查找表：用于线程里一行代码盲打输出中文/英文日志 */
extern const char* GESTURE_NAMES[];

#endif

#endif // _COMMON_H_