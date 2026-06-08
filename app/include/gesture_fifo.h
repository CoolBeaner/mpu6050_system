#ifndef _GESTURE_FIFO_H_
#define _GESTURE_FIFO_H_

#include <pthread.h> 
#include <stdbool.h>

#include "common.h"

/* --- 函数声明 --- */

/**
 * @brief 初始化手势环形缓冲区
 */
int init_g_fifo(GestureFifo_t *fifo);

/**
 * @brief 销毁手势环形缓冲区
 */
void destroy_g_fifo(GestureFifo_t *fifo);

/**
 * @brief 往 FIFO 塞入一帧新的 6 轴原始数据 (Thread 1 采集线调用)
 */
void fifo_push(GestureFifo_t *fifo, Vector3_t *accel, Vector3_t *gyro);

/**
 * @brief 提取出按时间排好序的 128 点大矩阵 (Thread 4 AI推理线调用)
 * @return int 1 代表成功拿到新窗口，0 代表新数据还不够步长
 */
int fifo_get_window(GestureFifo_t *fifo, 
                    float out_window[FIFO_SIZE][GESTURE_CHANNELS]);

/**
 * @brief 线程 5: 数据采集线程
 *        通过串口控制
 */
void* thread_data_collection(void* arg);

#endif // _GESTURE_FIFO_H_