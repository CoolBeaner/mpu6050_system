#ifndef _NETWORK_H_
#define _NETWORK_H_

#include "common.h"

/* --- 函数原型声明 --- */

/**
 * @brief 初始化网络组件
 * * 创建两个 UDP Socket：
 * 1. 发送 Socket：用于 400Hz 高速遥测数据传输。
 * 2. 接收 Socket：绑定特定端口并设为非阻塞模式，用于监听 PC 端指令。
 * * @return int 0 成功，-1 失败
 */
int network_init();

/**
 * @brief 打包并发送遥测数据包 (400Hz 级别)
 * * 将共享内存中的姿态角、加速度、系统状态等数据按照二进制协议打包，
 * 计算 CRC16 校验码后通过 UDP 发送到目标 PC。
 * * @param data 指向全局共享数据结构体的指针
 */
void network_send_telemetry(SharedData_t *data);

/**
 * @brief 检查并处理来自 PC 端的控制指令 (非阻塞)
 * * 检查是否有来自 PC 的 UDP 指令包，包含：
 * 1. 模式切换 (鼠标模式/手势模式/空闲)
 * 2. 触发零偏校准流程
 * * @param data 指向全局共享数据结构体的指针 (用于修改 mode 和 calib 状态)
 */
void network_check_commands(SharedData_t *data);

/**
 * @brief 关闭网络 Socket 并释放相关资源
 */
void network_cleanup();

/**
 * @brief 线程 2: 卡尔曼解算与网络遥测
 */
void* thread_network_telemetry(void* arg);

#endif // _NETWORK_H_