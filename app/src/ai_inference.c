#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "ai_inference.h"
#include "common.h"
#include "gesture_fifo.h"

/* 
 * AI 推理消抖：同动作触发后的屏蔽期帧数
 * 线程每 160ms 做一次推理 
 * 6 帧代表 6 * 160ms = 960ms 的滑窗流出保护期 
 */
#if (FIFO_STRIDE == 0)
    #error " FIFO_STRIDE CAN'T BE '0'"
#else
    #define GESTURE_HOLD_FRAMES    (FIFO_SIZE / FIFO_STRIDE)
#endif

/* 置信度阈值：AI 必须有多少把握才准释放动作 */
#define GESTURE_CONF_THRESHOLD    0.99f  

/* 预处理 加速度计（前 3 轴）物理缩放分母 */
#define ACCEL_SCALE_FACTOR        1.5f   

/* 预处理 陀螺仪（后 3 轴）物理缩放分母 */
#define GYRO_SCALE_FACTOR         90.0f

/* 连招最大支持 3 连击 */
#define COMBO_BUF_SIZE            3      

/* 连招动作之间的最大等待期（毫秒）*/
#define COMBO_TIMEOUT_MS          2000    

/**
 * @brief 连招树分析状态枚举
 */
typedef enum {
    COMBO_STATUS_NO_MATCH         = 0,  // 连招不匹配/中途断档
    COMBO_STATUS_PREFIX           = 1,  // 连招前缀
    COMBO_STATUS_FULL_MATCH       = 2,  // 完满命中长连招
    COMBO_STATUS_MATCH_AND_PREFIX = 3   // 命中短连招 但又是长连招前缀
} ComboStatus_E;

/* 单个连招结构体 */
typedef struct {
    int recipe[COMBO_BUF_SIZE];    // 最长 3 连击
    int length;                    // 连招长度
    int combo_id;                  // 连招代号
} ComboRecipe_S;

/*
 * 用于检测 连招 的tree 
 * 严格意义上不是tree 
 * 而是所有 连招 构成的数组
 * 可以直接添加新连招
 */
static const ComboRecipe_S combo_book[] = {
    /* 无穷->正圆->正三角 */
    { .recipe = {GESTURE_INFINITY, GESTURE_INV_CIRCLE, GESTURE_EQU_TRIANGLE}, 
      .length = 3, .combo_id = COMBO_LEITING_WANJUN }, 
    
    /* 无穷->正圆->倒三角 */
    { .recipe = {GESTURE_INFINITY, GESTURE_INV_CIRCLE, GESTURE_INV_TRIANGLE}, 
      .length = 3, .combo_id = COMBO_JUEDUI_LINGDU }, 
    
    /* 倒三角->正圆 */
    { .recipe = {GESTURE_INV_TRIANGLE, GESTURE_CIRCLE},                   
      .length = 2, .combo_id = COMBO_LIEDI_CHONGJI }, 
    
    /* 正三角->正圆 */
    { .recipe = {GESTURE_EQU_TRIANGLE, GESTURE_CIRCLE},                   
      .length = 2, .combo_id = COMBO_SHENGUANG_SHENPAN }, 
    
    /* 闪电->正圆 */
    { .recipe = {GESTURE_FLASH,        GESTURE_CIRCLE},                   
      .length = 2, .combo_id = COMBO_DIANCI_FENGBAO }  
};

#define RECIPE_COUNT (sizeof(combo_book) / sizeof(combo_book[0]))

/* 连招核心历史状态账本 */
static int      g_hist_buf[COMBO_BUF_SIZE] = {0};  // 连招缓存fifo
static int      g_hist_len        = 0;             // 连招当前长度
static uint32_t g_last_input_time = 0;
static int      g_held_combo_id   = 0;             // 扣留区：暂扣的短大招代号

/* 
 * 声明静态轻量缓冲区
 * 避免在全志 H3 栈空间中频繁申请导致溢出 
 * 给 c_gesture_inference 推理使用
 */
static float buf1[32][256];
static float pool1[32][128];
static float buf2[64][128];
static float pool2[64][64];
static float buf3[128][64];
static float pool3[128][32];
static float flat[4096];
static float fc1_out[256];
static float fc2_out[8];

/**
 * @brief 获取系统单调递增的毫秒时间戳
 * @return uint32_t 当前系统运行的毫秒数
 */
static uint32_t get_system_time_ms(void) {
    struct timespec ts;
    
    /* 使用 CLOCK_MONOTONIC（单调时钟）*/
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    /* 秒转化为毫秒 + 纳秒转化为毫秒 */
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / MS_CHENGE);
}

/**  
 * @brief 连招分析器：拿着账本去对撞秘籍
 * @return 0: 彻底不匹配 
 *         1: 纯连招前缀
 *         2: 完满命中长连招
 *         3: 命中短大招但它同时也是长连招的前缀
 */
static ComboStatus_E analyze_combo_tree(const int hist[], 
                                        int len, 
                                        int *out_combo_id) {
    int is_prefix = 0;
    int is_match  = 0;
    *out_combo_id = 0;

    for (int i = 0; i < RECIPE_COUNT; i++) {
        const ComboRecipe_S *r = &combo_book[i];
        
        /* 检查当前 连招 fifo 是否是某个长连招的前缀 */
        if (r->length > len) {
            int match = 1;
            for (int j = 0; j < len; j++) {
                if (r->recipe[j] != hist[j]) { 
                    match = 0; 
                    break; 
                }
            }
            if (match) is_prefix = 1;
        }
        /* 检查当前 连招 fifo 是否命中了某个连招 */
        if (r->length == len) {
            int match = 1;
            for (int j = 0; j < len; j++) {
                if (r->recipe[j] != hist[j]) { 
                    match = 0;
                    break; 
                }
            }
            if (match) {
                is_match = 1;
                *out_combo_id = r->combo_id;
            }
        }
    }

    if (is_match && is_prefix)   return COMBO_STATUS_MATCH_AND_PREFIX; 
    if (is_match)                return COMBO_STATUS_FULL_MATCH; 
    if (is_prefix)               return COMBO_STATUS_PREFIX; 
    return COMBO_STATUS_NO_MATCH;
}

/**
 * @brief 根据手势识别结果更新 g_gesture_data
 */
static void update_gesture_to_share(GestureShareData_S *gesture, 
                                    GestureResult_E raw_ai_res) {
    int final_combo = 0;
    int final_class = 0;
    int is_updated  = 0;
    uint32_t now = get_system_time_ms();

    /*
     * 主动超时检查
     * 无论当前有没有新动作
     * 只要手停了且超时了立刻结算
     */
    const int is_timeout = (g_hist_len > 0 && 
                            (now - g_last_input_time > COMBO_TIMEOUT_MS));

    if (is_timeout) {
        pthread_mutex_lock(&gesture->gesture_lock);
        gesture->combo_action    = g_held_combo_id;
        gesture->predicted_class = g_hist_buf[g_hist_len - 1];
        gesture->update_seq++;
        pthread_mutex_unlock(&gesture->gesture_lock);
        g_hist_len = 0;
        g_held_combo_id = 0;
        printf(" 续招超时 释放之前暂扣的动作 强制清空历史fifo\n");
    }

    /* 如果是无动作 退出 */
    if (raw_ai_res == GESTURE_NONE)  return;

    /* 拦截溢出保护 */
    if (g_hist_len >= COMBO_BUF_SIZE) 
        g_hist_len = 0;

    /* 新动作送入数组fifo */
    g_hist_buf[g_hist_len++] = raw_ai_res;
    
    /* 状态机树状分析 */
    int combo_id = 0;
    int status = analyze_combo_tree(g_hist_buf, g_hist_len, &combo_id);

    switch (status) {
        case COMBO_STATUS_FULL_MATCH:
            /* 完满命中最终大连招（如 3 连击成功）*/
            final_combo = combo_id;
            final_class = 0; 
            is_updated  = 1;
            g_hist_len  = 0; 
            g_held_combo_id = 0;
            printf("连招爆发：%d\n", combo_id);
            break;

        case COMBO_STATUS_PREFIX:

            /* 
             * 起手动作 / 中间动作
             * 不发送单发动作 
             * 等待倒计时结束 还没有下一个连招动作再发
             */ 
            g_last_input_time = now;
            printf("起手动作 / 中间动作 当前长度:%d\n", g_hist_len);
            break;

        case COMBO_STATUS_MATCH_AND_PREFIX:

            /* 
             * 中了短大招 但它同时又是长招的前缀
             * 不发送连招动作 
             * 等待倒计时结束 还没有更长连招再发
             */ 
            g_held_combo_id = combo_id; 
            g_last_input_time = now;
            printf("短连招 %d 等待续招...\n", combo_id);
            break;

        case COMBO_STATUS_NO_MATCH:
        default:

            /* 连招断档 或者是 单动作 */

            /* 如果是连招断档 发送暂扣 短连招 */
            if (g_held_combo_id != 0) {
                pthread_mutex_lock(&gesture->gesture_lock);
                gesture->combo_action    = g_held_combo_id;
                gesture->predicted_class = 0;
                gesture->update_seq++;
                pthread_mutex_unlock(&gesture->gesture_lock);
                g_held_combo_id = 0;
                printf(" 连招断档 补齐之前扣留的短连招...\n");
            }

            /* 如果是单动作直接发送 */
            if (g_hist_len == 1) {
                final_combo  = 0;
                final_class  = raw_ai_res;
                is_updated   = 1;
                g_hist_len   = 0; 
                break;
            }

            /* 如果是单动作打断连招 检测新动作是否为连招起手动作 */
            g_hist_buf[0] = raw_ai_res;
            g_hist_len = 1;
            
            int re_status = analyze_combo_tree(g_hist_buf, g_hist_len, &combo_id);

            /* 不是起手动作 直接发送 */
            if (re_status == COMBO_STATUS_NO_MATCH) {               
                final_combo  = 0;
                final_class  = raw_ai_res;
                is_updated   = 1;
                g_hist_len   = 0; 
                break;
            }

            /* 如果是起手动作 从计倒计时 */
            if (re_status == COMBO_STATUS_PREFIX) {
                g_last_input_time = now;
                printf("起手动作 / 中间动作 当前长度:%d\n", g_hist_len);
                break;
            }
    }

    if (is_updated) {
        pthread_mutex_lock(&gesture->gesture_lock);
        gesture->combo_action    = final_combo;
        gesture->predicted_class = final_class;
        gesture->update_seq++;
        pthread_mutex_unlock(&gesture->gesture_lock);
    }
}

int init_g_gesture(GestureShareData_S *gesture) {
    if (pthread_mutex_init(&gesture->gesture_lock, NULL) != 0) {
        return -1;
    }

    gesture->update_seq      = 0;
    gesture->predicted_class = 0;
    gesture->combo_action    = 0;
    return 0;
}

void destroy_g_gesture(GestureShareData_S *gesture) {
    pthread_mutex_destroy(&gesture->gesture_lock);
}

void z_score_normalize(float window[FIFO_SIZE][GESTURE_CHANNELS]) {

    /* 
     * 挨个通道进行洗礼
     * 0 1 2 是加速计
     * 3 4 5 是陀螺仪
     */
    for (int ch = 0; ch < GESTURE_CHANNELS; ch++) {
        
        /* 计算当前通道在 128 个点里的 均值 (Mean) */
        float sum = 0.0f;
        for (int t = 0; t < FIFO_SIZE; t++) {
            sum += window[t][ch];
        }
        float mean = sum / (float)FIFO_SIZE;

        /* 计算当前通道的 方差 (Variance) */
        float variance_sum = 0.0f;
        for (int t = 0; t < FIFO_SIZE; t++) {
            float diff = window[t][ch] - mean;
            variance_sum += diff * diff;
        }
        float variance = variance_sum / (float)FIFO_SIZE;

        /* 
         * 方差开根号
         * 得到 标准差 (Standard Deviation)
         * 必须用 sqrtf 如果误写成 sqrt 
         * Linux 会调用双精度浮点
         * 白白浪费 A7 芯片的算力 
         */
        float std_dev = sqrtf(variance);

        /*
         * Z-Score 变换
         * 如果mpu6050静止在桌上 
         * 标准差 std_dev 可能会无限趋近于 0
         * 直接除以 0 会触发 CPU 的浮点异常导致系统异常（NaN）
         * 所以在分母里加一个微小的安全垫 epsilon
         */
        float epsilon = 1e-6f;
        for (int t = 0; t < FIFO_SIZE; t++)
            window[t][ch] = (window[t][ch] - mean) / (std_dev + epsilon);
    }
}

void perfect_scale_normalize(float window[FIFO_SIZE][GESTURE_CHANNELS]) {
    
    /* 
     * 动态去重力直流分量与传感器静态零偏
     * 挨个通道计算均值 并让整个滑窗减去均值
     * 强行把起手基准线扣平到 0 轴
     * 0 1 2 是加速计 
     * 3 4 5 是陀螺仪
     */
    for (int ch = 0; ch < GESTURE_CHANNELS; ch++) {
        
        /* 计算当前通道在 256 个点里的均值 (Mean) */
        float sum = 0.0f;
        for (int t = 0; t < FIFO_SIZE; t++) {
            sum += window[t][ch];
        }
        float mean = sum / (float)FIFO_SIZE;

        /* 减去自身均值，消除位置依赖性和硬件硬件漂移 */
        for (int t = 0; t < FIFO_SIZE; t++) {
            window[t][ch] -= mean;
        }
    }

    /* 
     * 全局固定经验量程物理级缩放
     * 放弃 Z-Score 
     * 保护特征与手抖噪声之间的真实物理比例
     * 必须带上 'f' 后缀，确保全志 H3 的硬件单精度浮点 FPU 全速爆发
     */
    for (int t = 0; t < FIFO_SIZE; t++) {

        /* 
         * 前 3 轴（加速计）动态最大值约为 1.5g
         * 统一除以 1.5f 
         */
        window[t][0] /= ACCEL_SCALE_FACTOR;  // Acc X
        window[t][1] /= ACCEL_SCALE_FACTOR;  // Acc Y
        window[t][2] /= ACCEL_SCALE_FACTOR;  // Acc Z
          
        
        /*
         * 后 3 轴（陀螺仪）动态最大值约为 180°/s
         * 统一除以 180.0f
         */
        window[t][3] /= GYRO_SCALE_FACTOR; // Gyro X
        window[t][4] /= GYRO_SCALE_FACTOR; // Gyro Y
        window[t][5] /= GYRO_SCALE_FACTOR; // Gyro Z
    }
}

/* 纯 C 语言 1D-CNN 推理核心 */
static int c_gesture_inference(float window[FIFO_SIZE][GESTURE_CHANNELS]) {

    /* LAYER 1: CONV1 (6->32, K=5, P=2) -> MaxPool(2) */
    for (int c_out = 0; c_out < 32; c_out++) {
        for (int t = 0; t < 256; t++) {
            float sum = CONV1_B[c_out];
            for (int c_in = 0; c_in < 6; c_in++) {
                for (int k = 0; k < 5; k++) {
                    /* Padding = 2 */
                    int in_t = t + k - 2; 
                    if (in_t >= 0 && in_t < 256) {
                        /* 
                         * 板子上采集的 window 是 [256][6] 
                         * 这里对应 Python 轴对换后的取法 
                         */
                        sum += window[in_t][c_in] * CONV1_W[c_out][c_in][k];
                    }
                }
            }
            /* ReLU 激活 */
            buf1[c_out][t] = (sum > 0.0f) ? sum : 0.0f;
        }
    }
    /* MaxPool1d (stride=2, kernel=2) */
    for (int c = 0; c < 32; c++) {
        for (int t = 0; t < 128; t++) {
            float m1 = buf1[c][t * 2];
            float m2 = buf1[c][t * 2 + 1];
            pool1[c][t] = (m1 > m2) ? m1 : m2;
        }
    }

    /* LAYER 2: CONV2 (32->64, K=5, P=2) -> MaxPool(2) */
    for (int c_out = 0; c_out < 64; c_out++) {
        for (int t = 0; t < 128; t++) {
            float sum = CONV2_B[c_out];
            for (int c_in = 0; c_in < 32; c_in++) {
                for (int k = 0; k < 5; k++) {
                    int in_t = t + k - 2;
                    if (in_t >= 0 && in_t < 128) {
                        sum += pool1[c_in][in_t] * CONV2_W[c_out][c_in][k];
                    }
                }
            }
            buf2[c_out][t] = (sum > 0.0f) ? sum : 0.0f;
        }
    }
    for (int c = 0; c < 64; c++) {
        for (int t = 0; t < 64; t++) {
            float m1 = buf2[c][t * 2];
            float m2 = buf2[c][t * 2 + 1];
            pool2[c][t] = (m1 > m2) ? m1 : m2;
        }
    }

    /* LAYER 3: CONV3 (64->128, K=3, P=1) -> MaxPool(2) */
    for (int c_out = 0; c_out < 128; c_out++) {
        for (int t = 0; t < 64; t++) {
            float sum = CONV3_B[c_out];
            for (int c_in = 0; c_in < 64; c_in++) {
                for (int k = 0; k < 3; k++) {
                    /* Padding = 1 */
                    int in_t = t + k - 1; 
                    if (in_t >= 0 && in_t < 64) {
                        sum += pool2[c_in][in_t] * CONV3_W[c_out][c_in][k];
                    }
                }
            }
            buf3[c_out][t] = (sum > 0.0f) ? sum : 0.0f;
        }
    }
    for (int c = 0; c < 128; c++) {
        for (int t = 0; t < 32; t++) {
            float m1 = buf3[c][t * 2];
            float m2 = buf3[c][t * 2 + 1];
            pool3[c][t] = (m1 > m2) ? m1 : m2;
        }
    }

    /* LAYER 4: Flatten 展平 (形状变为 [4096]) */
    int flat_idx = 0;
    for (int c = 0; c < 128; c++) {
        for (int t = 0; t < 32; t++) {
            flat[flat_idx++] = pool3[c][t];
        }
    }

    /* FC1 全连接层 (4096 -> 256) */
    for (int i = 0; i < 256; i++) {
        float sum = FC1_B[i];
        for (int j = 0; j < 4096; j++) {
            sum += flat[j] * FC1_W[i][j];
        }
        fc1_out[i] = (sum > 0.0f) ? sum : 0.0f; // ReLU
    }

    /* LAYER 6: FC2 输出层 (256 -> 8) */
    for (int i = 0; i < 8; i++) {
        float sum = FC2_B[i];
        for (int j = 0; j < 256; j++) {
            sum += fc1_out[j] * FC2_W[i][j];
        }
        /* 
         * 这里无需做昂贵的 Softmax
         * 因为最大值的相对位置是一样的
         */
        fc2_out[i] = sum; 
    }

    /* LAYER 7: Argmax 找出最大置信度对应的标签 */
    int predicted_class = 0;
    float max_val = fc2_out[0];
    for (int i = 1; i < 8; i++) {
        if (fc2_out[i] > max_val) {
            max_val = fc2_out[i];
            predicted_class = i;
        }
    }

    /* 
     * 计算 Softmax 分母
     * 减去 max_val 
     * 用于防止 expf 发生指数爆炸级上溢
     */
    float sum_exp = 0.0f;
    for (int i = 0; i < 8; i++) {
        sum_exp += expf(fc2_out[i] - max_val); 
    }
    /* 算出当前胜出类别的绝对概率置信度 */
    float confidence = 1.0f / sum_exp; 

    /* 
     * 如果 AI 的把握低于 99%
     * 判定为 0 无动作/杂波
     */
    if (confidence < GESTURE_CONF_THRESHOLD) {
        predicted_class = 0; 
    }

    return predicted_class;
}

GestureResult_E run_ai_inference(float window[FIFO_SIZE][GESTURE_CHANNELS]) {
    /* 记忆上一次成功触发结果 */
    static GestureResult_E last_gesture = GESTURE_NONE; 
    /*  窗口重叠期屏蔽倒计时 */
    static int hold_frames = 0;                         

    GestureResult_E raw_res = (GestureResult_E)c_gesture_inference(window);

    /* 
     * 当前无动作 且还处于上一次动作的滑窗流出保护期 
     * -> 递减倒计时 
     */
    if (raw_res == GESTURE_NONE && hold_frames > 0) {
        hold_frames--;
        return GESTURE_NONE;
    }

    /* 
     * 当前无动作 且保护期已完全结束
     * -> 彻底重置状态机 
     * 允许下一次冷启动
     */
    if (raw_res == GESTURE_NONE && hold_frames <= 0) {
        last_gesture = GESTURE_NONE;
        return GESTURE_NONE;
    }

    /* 
     * 检测到一个跟之前完全不同的结果
     * -> 放行 无视延迟 并拉满新保护期
     */
    if (raw_res != last_gesture) {
        last_gesture = raw_res;
        hold_frames = GESTURE_HOLD_FRAMES; 
        return raw_res;
    }

    /* 
     * 检测到相同大招 
     * 但在滑窗流出的保护期内 
     * -> 递减倒计时 
     * 强制静音
     */
    if (raw_res == last_gesture && hold_frames > 0) {
        hold_frames--;
        return GESTURE_NONE;
    }

    hold_frames = GESTURE_HOLD_FRAMES;
    return raw_res;
}

#ifndef DATA_ACQUISITION

/**
 * @brief 线程 5: 模型推理
 */
void* thread_ai_inference(void* arg) {
    float local_window[FIFO_SIZE][GESTURE_CHANNELS];

    printf("[Thread 5] 模型推理线程已启动...\n");

    while (g_keep_running) {

        /* 从fifo中获取一个窗口的数据 */
        if (fifo_get_window(&g_gesture_fifo, local_window)) {

            /* 清洗数据 数据标准化处理 */
            //z_score_normalize(local_window);

            /* 清洗数据 数据自定义预处理 */
            perfect_scale_normalize(local_window);
            
            /* 喂给 1D-CNN 模型 做推理 */
            GestureResult_E result = run_ai_inference(local_window); 
            update_gesture_to_share(&g_gesture_data, result);
            
            if (result != GESTURE_NONE) {
                printf("[Thread 5] 成功检测到: %s 魔咒\n", 
                       GESTURE_NAMES[result]);
            }
        }
        usleep(10000);
    }
    return NULL;
}

#endif