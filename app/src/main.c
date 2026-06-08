#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <math.h>

/* 
 * 数据采集宏 
 * 如果需要采集数据需要开启该宏
 * 该宏会打开采集线程
 * 在串口提示用户新手势数据的采集
 * 采集完的数据按照 .csv 格式保存
 * 在 /tmp/raw_gesture_%d.csv 文件下
 * 请确保根目录下有tmp目录
 */
//#define DATA_ACQUISITION

#include "common.h"
#include "mpu_io.h"
#include "algo.h"
#include "network.h"
#include "usb_hid.h"
#include "gesture_fifo.h"
#include "ai_inference.h"

/* --- 全局变量定义 --- */

SharedData_t    g_data;                  // 核心共享数据结构体
volatile bool   g_keep_running = true;   // 系统运行标志位

#ifdef DATA_ACQUISITION

/* 
 * 临时存放采集的数据 
 * MAX_RECORD_POINTS 组数据
 * 每组 6 轴数据
 */
float g_record_big_buf[MAX_RECORD_POINTS][6]; 

/* 纪录当前录制到第几个点了 */
volatile int g_record_idx = 0;        

/* 数据采集状态控制
 * 0: 停止录制
 * 1: 正在录制 
 */
volatile bool g_record_flag = false; 

#else

/* 手势识别原始数据环形缓冲区 */
GestureFifo_t   g_gesture_fifo;  

/* 手势识别结果共享结构体 */  
GestureShareData_S g_gesture_data; 

/* 
 * 名字查找表
 * 用于线程里一行代码盲打输出中文/英文日志 
 */
const char* GESTURE_NAMES[] = {
    "背景杂波/无动作",
    "正圈 (CIRCLE)",
    "逆圈 (INV_CIRCLE)",
    "正三角 (EQU_TRIANGLE)",
    "逆三角 (INV_TRIANGLE)",
    "横线 (LINE)",
    "闪电 (FLASH)",
    "无穷 (INFINITY)"
};

#endif

/**
 * @brief 初始化核心共享数据结构体 
 */
static int init_g_data(SharedData_t *data) {
    if (pthread_mutex_init(&data->g_data_lock, NULL) != 0) {
        return -1;
    }
    memset(data, 0, sizeof(SharedData_t));
    
    data->mode = MODE_IDLE;
    //data->mode = MODE_GESTURE;
    data->calib_info.is_valid = false;
    data->calib_info.is_calibrating = true;

    /* EMA 冷启动标准 */
    data->ema_accel.x = -999.0f; 
    return 0;
}

/**
 * @brief 销毁核心共享数据结构
 */
static void destroy_g_data(SharedData_t *data) {
    pthread_mutex_destroy(&data->g_data_lock);
}

/**
 * @brief 初始化数据结构体 
 *        加载配置文件 写入默认K值 初始锁
 */
static int sys_init(void) {

    /* 初始化核心共享数据结构体 */
    if (init_g_data(&g_data)) 
        return -1;

#ifndef DATA_ACQUISITION
    /* 初始化手势识别fifo数据结构体 */   
    if (init_g_fifo(&g_gesture_fifo)) 
        return -1;   
    
        /* 初始化手势识别结果数据结构体 */   
    if (init_g_gesture(&g_gesture_data)) 
        return -1; 
#endif 

    /* 加载本地校准配置文件 */
    algo_load_calibration_file(&g_data.calib_info);

    /* 更新K bias */
    kalman_init_with_calib(&g_data.calib_info);

    return 0;
}

/**
 * @brief 销毁数据结构及销毁锁
 */
static void sys_destroy(void) {
    destroy_g_data(&g_data);

#ifndef DATA_ACQUISITION
    destroy_g_fifo(&g_gesture_fifo);
    destroy_g_gesture(&g_gesture_data);
#endif
}

/**
 * @brief 信号处理退出 
 */
void handle_sigint(int sig) {
    printf("\n[System] 捕获信号 %d, 正在安全关闭系统...\n", sig);
    g_keep_running = false;
}

/**
 * @brief 主程序
 */
int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("   MPU6050 Industrial System Starting   \n");
    printf("========================================\n");

    /* 初始化信号处理 */
    signal(SIGINT, handle_sigint);

    /* 初始化数据结构体及锁 加载配置文件 */
    if(sys_init()) 
        return -1;

    pthread_t t1, t2, t3, t4, t5;
    pthread_create(&t1, NULL, thread_sensor_acq, NULL);
    pthread_create(&t2, NULL, thread_network_telemetry, NULL);
    pthread_create(&t3, NULL, thread_usb_app, NULL);
    pthread_create(&t4, NULL, thread_sys_monitor, NULL);
    
#ifdef DATA_ACQUISITION
    pthread_create(&t5, NULL, thread_data_collection, NULL);
#else
    pthread_create(&t5, NULL, thread_ai_inference, NULL);
#endif

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    pthread_join(t5, NULL);

    sys_destroy();
    printf("[System] 退出成功, 感谢使用.\n");

    return 0;
}