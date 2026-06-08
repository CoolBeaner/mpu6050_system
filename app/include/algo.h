#ifndef _ALGO_H_
#define _ALGO_H_

#include "common.h"

/* --- 函数原型声明 --- */

/**
 * @brief 指数移动平均滤波 (EMA)
 * @param current 当前采样原始数据
 * @param last    上一次滤波后的数据
 * @param alpha   滤波系数 (0.0 - 1.0)
 * @return 滤波后的平滑数据
 */
Vector3_t algo_ema_filter(Vector3_t current, Vector3_t last, float alpha);

/**
 * @brief 核心卡尔曼滤波姿态解算
 * @param accel 经过 EMA 滤波后的加速度数据 (单位: g)
 * @param gyro  经过 EMA 滤波后的角速度数据 (单位: deg/s)
 * @param dt    真实采样时间间隔 (单位: 秒)
 * @return 融合后的 Pitch, Roll, Yaw 姿态角
 */
Attitude_t algo_kalman_solve(Vector3_t accel, Vector3_t gyro, double dt);

/**
 * @brief 零偏校准处理逻辑
 * @param raw   当前读取的原始数据指针
 * @param calib 全局校准信息结构体指针
 * * 说明：该函数会在 calib->is_calibrating 为 true 时累加数据，
 * 达到预定样本数后自动计算均值并更新 Offset。
 */
void algo_process_calibration(ProcessedData_t *raw, CalibInfo_t *calib);

/**
 * @brief 从本地文件系统加载校准参数
 * @param calib 指向校准信息结构体的指针
 */
void algo_load_calibration_file(CalibInfo_t *calib);

/**
 * @brief 将当前校准参数保存至本地文件
 * @param calib 指向校准信息结构体的指针
 */
void algo_save_calibration_file(const CalibInfo_t *calib);

/**
 * @brief 带有静态零偏注入的卡尔曼滤波器初始化 (热启动)
 * * 在系统启动或用户完成一次全新的静态校准后调用此函数。
 * 本函数不仅会初始化滤波器的协方差矩阵，还会直接将测量到的
 * 陀螺仪物理零偏作为卡尔曼的初始偏差 (Bias) 进行注入。
 * 这能让滤波器在启动的第 1 毫秒直接进入“已收敛”状态，
 * 彻底消除常规卡尔曼滤波开机前几秒的姿态滑行现象。
 * * @param calib 指向校准信息结构体的指针。
 * 注意：调用前请确保 calib->is_valid 为 true，
 * 即内部的 gyro_offset 已被正确赋值。
 */
void kalman_init_with_calib(const CalibInfo_t *calib);

#endif // _ALGO_H_