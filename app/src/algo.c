#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <unistd.h>

#include "algo.h"
#include "common.h"

#define MPU_CONF_PATH   "/data/mpu6050.conf"

Kalman_t K_Pitch = {0, 0, {{1, 0}, {0, 1}}}; // 卡尔曼Pitch校准
Kalman_t K_Roll  = {0, 0, {{1, 0}, {0, 1}}}; // 卡尔曼Roll校准

/* 卡尔曼增益参数 */
static const float Q_angle = 0.001f; // 过程噪声：角度
static const float Q_gyro  = 0.003f; // 过程噪声：陀螺仪偏差
static const float R_angle = 0.03f;  // 测量噪声：加速度计

/* --- 配置文件加载 --- */
void algo_load_calibration_file(CalibInfo_t *calib) {
    FILE *f = fopen(MPU_CONF_PATH, "rb");
    if (f) {
        fread(&calib->gyro_offset, sizeof(Vector3_t), 1, f);
        fread(&calib->accel_offset, sizeof(Vector3_t), 1, f);
        calib->is_valid = true;
        fclose(f);
        printf("[Algo] 已从文件加载校准数据。\n");
    } else {
        printf("[Algo] 未找到校准文件，建议执行一次校准。\n");
        calib->is_valid = false;
    }
}

/* 
 * --- 保存配置文件 ---
 * 如果文件不存在会自动创建 
 * 如果存在会清空重写 
 */
void algo_save_calibration_file(const CalibInfo_t *calib) {
    FILE *f = fopen(MPU_CONF_PATH, "wb");
    if (f) {
        fwrite(&calib->gyro_offset, sizeof(Vector3_t), 1, f);
        fwrite(&calib->accel_offset, sizeof(Vector3_t), 1, f);
        fflush(f);
        fsync(fileno(f)); 
        fclose(f);
        printf("[Algo] 成功：校准数据已保存至文件 (%s)。\n", MPU_CONF_PATH);
    } else {
        printf("[Algo] 错误：无法打开校准文件进行写入！请检查目录权限或存储空间。\n");
    }
}

/* 
 * 初始卡尔曼K参数
 * 把静态校准的物理零偏作为卡尔曼的初始预测偏置 
 */
void kalman_init_with_calib(const CalibInfo_t *calib) {
    K_Pitch.angle = 0.0f;
    K_Pitch.bias  = calib->gyro_offset.x;
    K_Pitch.P[0][0] = 1;
    K_Pitch.P[0][1] = 0;
    K_Pitch.P[1][0] = 0;
    K_Pitch.P[1][1] = 1;

    K_Roll.angle  = 0.0f;
    K_Roll.bias   = calib->gyro_offset.y;
    K_Roll.P[0][0] = 1;
    K_Roll.P[0][1] = 0;
    K_Roll.P[1][0] = 0;
    K_Roll.P[1][1] = 1;
}

/* --- EMA 数字滤波实现 --- */
Vector3_t algo_ema_filter(Vector3_t current, Vector3_t last, float alpha) {
    Vector3_t output;
    output.x = alpha * current.x + (1.0f - alpha) * last.x;
    output.y = alpha * current.y + (1.0f - alpha) * last.y;
    output.z = alpha * current.z + (1.0f - alpha) * last.z;
    return output;
}

/* --- 卡尔曼滤波核心算法 (一维处理函数) --- */
static float kalman_update(Kalman_t *K, float newAngle,
                           float newRate, double dt) {

    /* x_{predict} = A x_{last} + B u */
    float rate = newRate - K->bias;
    K->angle += dt * rate;

    /* 
     * P_{predict} = A P_{last} A^T + Q
     * Q_angle Q_gyro 等价 Q 矩阵
     */
    K->P[0][0] += dt * (dt * K->P[1][1] - K->P[0][1] - K->P[1][0] + Q_angle);
    K->P[0][1] -= dt * K->P[1][1];
    K->P[1][0] -= dt * K->P[1][1];
    K->P[1][1] += Q_gyro * dt;

    /* K = P H^T / (H P H^T + R) */
    float S = K->P[0][0] + R_angle;
    float Kg[2];
    Kg[0] = K->P[0][0] / S;
    Kg[1] = K->P[1][0] / S;

    /*  
     * hat{x}_{new} = hat{x}_{predict} + 
     *                 K cdot (z_k - H hat{x}_{predict}) 
     */
    float y = newAngle - K->angle;
    K->angle += Kg[0] * y;
    K->bias  += Kg[1] * y;

    /* P_{new} = (I - KH)P_{predict} */
    float P00_temp = K->P[0][0];
    float P01_temp = K->P[0][1];

    K->P[0][0] -= Kg[0] * P00_temp;
    K->P[0][1] -= Kg[0] * P01_temp;
    K->P[1][0] -= Kg[1] * P00_temp;
    K->P[1][1] -= Kg[1] * P01_temp;

    return K->angle;
}

/* --- 姿态解算入口 --- */
Attitude_t algo_kalman_solve(Vector3_t accel, Vector3_t gyro, double dt) {
    Attitude_t result;

    /*
     * 从加速度计计算倾角 (静态参考)
     * Pitch: 绕 Y 轴旋转；Roll: 绕 X 轴旋转
     * 用加速度计算出角度（根据重力对加速度的影响）
     * 把计算出的角度作为zk 观测方程
     * 因为卡尔曼是为了计算出xk的（角度），所以用加速度
     * 计去修正角速度计（角度）的偏移。而如果要反过来，
     * 就成了用角速度计计算加速度，修正加速度的偏移。
     */
    float acc_pitch = RADIANS_TO_DEGREES(atan2f(-accel.x, 
                                                sqrtf(accel.y * accel.y + 
                                                      accel.z * accel.z)));
    float acc_roll  = RADIANS_TO_DEGREES(atan2f(accel.y, accel.z));

     /* 使用卡尔曼滤波融合陀螺仪角速度 */
    result.pitch = kalman_update(&K_Pitch, acc_pitch, gyro.y, dt);
    result.roll  = kalman_update(&K_Roll,  acc_roll,  gyro.x, dt);
    
    /* Yaw 轴处理 (6轴无法通过加速度计修正，仅做积分) */
    static float yaw_integ = 0.0f;
    /* 
     * 如果扣除静态零偏后，角速度依然极小
     * 不做积分累计yaw
     * 说明这是温漂或底噪，并不是人在转动
     * yaw_integ *= 0.999f;处理动态飘逸
     * 当晃动时飘逸还是存在的，加入衰减系数
     * 让yaw在静止时慢慢归0，但还是无法解决
     * yaw飘逸的问题。只有升级九轴带地磁。
     */
    if (gyro.z < -GYRO_Z_DEADZONE || gyro.z > GYRO_Z_DEADZONE)
        yaw_integ += gyro.z * dt;
    else 
        yaw_integ *= 0.999f;

    result.yaw = yaw_integ;
    return result;
}

/* --- 零偏校准处理 --- */

static Vector3_t g_sum_gyro = {0, 0, 0};
static Vector3_t g_sum_accel = {0, 0, 0};

void algo_process_calibration(ProcessedData_t *raw, CalibInfo_t *calib) {
    if (calib->calib_count == 0) {
        g_sum_gyro = (Vector3_t){0, 0, 0};
        g_sum_accel = (Vector3_t){0, 0, 0};
        printf("[Algo] 零偏校准启动，请保持板子静止...\n");
    }

    /* 累加数据 */
    g_sum_gyro.x += raw->gyro.x;
    g_sum_gyro.y += raw->gyro.y;
    g_sum_gyro.z += raw->gyro.z;

    g_sum_accel.x += raw->accel.x;
    g_sum_accel.y += raw->accel.y;
    g_sum_accel.z += raw->accel.z;

    calib->calib_count++;

    /* 达到 500 个样本，计算均值 */
    if (calib->calib_count >= CALIB_SAMPLE_COUNT) {
        calib->gyro_offset.x = g_sum_gyro.x / CALIB_SAMPLE_COUNT;
        calib->gyro_offset.y = g_sum_gyro.y / CALIB_SAMPLE_COUNT;
        calib->gyro_offset.z = g_sum_gyro.z / CALIB_SAMPLE_COUNT;

        calib->accel_offset.x = g_sum_accel.x / CALIB_SAMPLE_COUNT;
        calib->accel_offset.y = g_sum_accel.y / CALIB_SAMPLE_COUNT;

        /* 加速度计 Z 轴应为 1g，所以零偏是均值减去 1.0 (假设单位是 g) */
        calib->accel_offset.z = (g_sum_accel.z / CALIB_SAMPLE_COUNT) - 1.0f;

        calib->is_valid = true;
        calib->is_calibrating = false;
        calib->calib_count = 0;
        printf("[Algo] 校准完成 Offset: G(%.2f, %.2f, %.2f)\n", 
               calib->gyro_offset.x, 
               calib->gyro_offset.y, 
               calib->gyro_offset.z);

        /* 更新K bias */
        kalman_init_with_calib(calib);

        /* 写入校准配置文件 */
        algo_save_calibration_file(calib);
    }
}