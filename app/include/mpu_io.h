#ifndef _MPU_IO_H_
#define _MPU_IO_H_

#include "common.h"

/* --- 函数原型声明 --- */

/**
 * @brief 初始化 MPU6050 I/O 设备
 * * 打开由驱动程序创建的字符设备节点（通常为 /dev/mpu6050）。
 * * @return int 成功返回文件描述符 (fd)，失败返回 -1。
 */
int mpu_io_init(void);

/**
 * @brief 从驱动程序读取一帧原始数据
 * * 该函数封装了 read() 系统调用。由于驱动层使用了 kfifo，
 * read 操作会阻塞直到新数据到达（由硬件中断触发）。
 * * @param fd 设备文件描述符
 * @param out_data 输出数据缓冲区指针，类型为 RawData_t
 * @return int 成功返回 1；数据包长度异常返回 -2；底层 I/O 错误（如 EMI 导致掉线）返回 -1。
 */
int mpu_io_read(int fd, ProcessedData_t *out_data);

/**
 * @brief 采集 Linux 系统性能数据
 * * 访问 /proc 和 /sys 接口，获取当前 H3 芯片的 CPU 占用率和温度。
 * 建议在低频线程（如 1Hz）中调用，以减少文件 IO 开销。
 * * @return SysStatus_t 包含 CPU 使用率、温度等信息的结构体。
 */
SysStatus_t mpu_io_get_sys_info(void);

/**
 * @brief 关闭设备文件并执行清理
 * * @param fd 设备文件描述符
 */
void mpu_io_close(int fd);

/**
 *  @brief 线程 1: 数据采集与基础预处理 (500Hz)
 */
void* thread_sensor_acq(void* arg);

/**
 * @brief 线程 4: 系统监控 (1Hz)
 */
void* thread_sys_monitor(void* arg);

#endif // _MPU_IO_H_