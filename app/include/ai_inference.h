#ifndef _AI_INFERENCE_H_
#define _AI_INFERENCE_H_

#include "common.h"
#include "gesture_model_weights.h"

/* --- 函数声明 --- */

/**
 * @brief 初始化手势识别结果结构体
 */
int init_g_gesture(GestureShareData_S *gesture);

/**
 * @brief 销毁手势识别结果结构体
 */
void destroy_g_gesture(GestureShareData_S *gesture);

/**
 * @brief 对 128x6 的时序窗口数据进行通道独立的 Z-Score 标准化
 * @param window 输入的 FIFO_SIZE行 * GESTURE_CHANNELS列 的时序大盘子
 * 直接在原内存上修改（In-place）
 */
void z_score_normalize(float window[FIFO_SIZE][GESTURE_CHANNELS]);

/**
 * @brief 对 128x6 的时序窗口数据进行通道独立的 自定义预处理
 * @param window 输入的 FIFO_SIZE行 * GESTURE_CHANNELS列 的时序大盘子
 * 直接在原内存上修改（In-place）
 */
void perfect_scale_normalize(float window[FIFO_SIZE][GESTURE_CHANNELS]);

/**
 * @brief  AI 推理核心入口 TFLite C++ 运行模型
 * @return GestureResult_E 返回识别出来的魔咒 enum
 */
GestureResult_E run_ai_inference(float window[FIFO_SIZE][GESTURE_CHANNELS]);

/**
 * @brief 线程 5: 模型推理
 */
void* thread_ai_inference(void* arg); 

#endif // _AI_INFERENCE_H_