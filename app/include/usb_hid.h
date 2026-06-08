#ifndef _USB_HID_H_
#define _USB_HID_H_

#include "common.h"

/* --- 函数原型声明 --- */

/**
 * @brief 初始化 USB Gadget 复合设备
 * * 调用外部 shell 脚本配置 ConfigFS (设置 VendorID, ProductID 等)。
 * * 挂载并打开虚拟 HID 设备节点 /dev/hidg0。
 * * @return int 0 成功, -1 失败
 */
int usb_hid_init();

/**
 * @brief 虚拟鼠标逻辑处理 (4-byte HID Mouse Report)
 * * 将陀螺仪角速度 (Gyro) 映射为鼠标相对位移 (dX, dY)。
 * * 实现类似于“激光笔”的空中鼠标交互手感。
 * * @param gyro 经过滤波后的角速度数据向量
 */
void usb_hid_process_mouse(Vector3_t gyro);

/**
 * @brief 手势识别应用逻辑 (8-byte HID Keyboard Report)
 * * 触发 AI 模型推理逻辑，根据识别结果模拟键盘按键上报。
 * * 包含“按键按下”与“自动松开”的完整上报周期。
 * * @param accel 经过滤波后的加速度数据向量 (供 FFT/模型推理使用)
 */
void usb_hid_process_gesture(GestureShareData_S *res);

/**
 * @brief 辅助函数：限制数值在指定范围内
 * * 确保发送给 HID 驱动的 byte 数据不会发生溢出。
 * * @param val 输入值
 * * @param min 最小值
 * * @param max 最大值
 * * @return 限制后的结果
 */
float constrain(float val, float min, float max);

/**
 * @brief 关闭 HID 设备文件描述符并清理资源
 */
void usb_hid_cleanup();

/**
 * @brief 线程 3: USB HID 应用 (鼠标/手势)
 */
void* thread_usb_app(void* arg);

#endif // _USB_HID_H_