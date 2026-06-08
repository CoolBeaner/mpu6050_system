#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "usb_hid.h"
#include "common.h"

/* 由 ConfigFS 生成的设备节点 */
#define HID_MOUSE_DEVICE "/dev/hidg0" 
#define HID_KBD_DEVICE   "/dev/hidg1" 

/* --- 空中鼠标参数 --- */
#define MOUSE_GAIN_X             0.25f   // X轴灵敏度
#define MOUSE_GAIN_Y             0.25f   // Y轴灵敏度
#define MOUSE_DEADZONE           0.5f    // 滤波死区

/* 定义 鼠标标准 4 字节数据包结构体 */
typedef struct {
    uint8_t buttons;              /* 按键状态：bit0左键，bit1右键 */
    int8_t dx;                    /* X 轴相对位移 */
    int8_t dy;                    /* Y 轴相对位移 */
    int8_t wheel;                 /* 滚轮动量 */
} __attribute__((packed)) MouseReport_S;

/* 定义 键盘标准 8 字节数据包结构体 */
typedef struct {
    uint8_t modifiers;            /* 特殊控制键（Ctrl/Shift等） */
    uint8_t reserved;             /* 固定保留 0 */
    uint8_t keycodes[6];          /* 6 键无冲键码槽 */
} __attribute__((packed)) KeyboardReport_S;


/* 定义 统一动作按键映射结构体 */
typedef struct {
    int action_id;                /* 动作ID (通用支持单招枚举值或连招枚举值) */
    uint8_t keycode;              /* 对应的 HID 键盘标准键码 */
} ActionKeyMap_S;

/* 
 * 单招与连招 动作映射自定义配置表
 * 想给动作改键或者加新配方
 * 直接在下面增删改这张表即可
 */
static const ActionKeyMap_S g_action_key_book[] = {
/* ---- 7个单招映射配置 ---- */
    { GESTURE_CIRCLE,       0x06 },    /* 映射为 C 键 */
    { GESTURE_INV_CIRCLE,   0x19 },    /* 映射为 V 键 */
    { GESTURE_EQU_TRIANGLE, 0x17 },    /* 映射为 T 键 */
    { GESTURE_INV_TRIANGLE, 0x0A },    /* 映射为 G 键 */
    { GESTURE_LINE,         0x0F },    /* 映射为 L 键 */
    { GESTURE_FLASH,        0x09 },    /* 映射为 F 键 */
    { GESTURE_INFINITY,     0x0C },    /* 映射为 I 键 */

    /* ---- 5个连招映射配置 ---- */
    { COMBO_LEITING_WANJUN,    0x1E }, /* 映射为 1 键 */
    { COMBO_JUEDUI_LINGDU,     0x1F }, /* 映射为 2 键 */
    { COMBO_LIEDI_CHONGJI,     0x20 }, /* 映射为 3 键 */
    { COMBO_SHENGUANG_SHENPAN, 0x21 }, /* 映射为 4 键 */
    { COMBO_DIANCI_FENGBAO,    0x22 }  /* 映射为 5 键 */
};

#define ACTION_KEY_MAP_COUNT (sizeof(g_action_key_book) / sizeof(g_action_key_book[0]))

static int mouse_fd = -1;
static int kbd_fd = -1;

/**
 * @brief 初始化 USB Gadget
 */
int usb_hid_init() {
    printf("[USB] 正在配置 USB Gadget 模式...\n");

    /* 调用脚本初始化 ConfigFS */
    system("./usr/bin/usb_gadget_init.sh");
    
    /* 等待设备节点生成 */
    sleep(1); 

    mouse_fd = open(HID_MOUSE_DEVICE, O_RDWR | O_NONBLOCK);
    if (mouse_fd < 0) {
        perror("[USB] 无法打开 HID MOUSE 设备节点");
        return -1;
    }

    printf("[USB] 虚拟 HID MOUSE 设备已就绪: %s\n", HID_MOUSE_DEVICE);

    kbd_fd = open(HID_KBD_DEVICE, O_RDWR | O_NONBLOCK);
    if (mouse_fd < 0) {
        perror("[USB] 无法打开 HID KBD 设备节点");
        return -1;
    }

    printf("[USB] 虚拟 HID KBD 设备已就绪: %s\n", HID_KBD_DEVICE);   
    return 0;
}

/**
 * @brief 虚拟鼠标处理 (陀螺仪映射)
 */
void usb_hid_process_mouse(Vector3_t gyro) {
    if (mouse_fd < 0) return;

    /*
     * 使用相对位移映射
     * 映射逻辑：
     * Yaw(z轴) 控制 X 轴移动
     * Pitch(y轴) 控制 Y 轴移动
     */
    float dx = -gyro.z * MOUSE_GAIN_X;
    float dy = -gyro.y * MOUSE_GAIN_Y;

    /*
     * 角速度绝对值小于这个数 直接视为静止
     * 消除手部微小颤抖带来的位移
     */
    if (fabs(dx) <= MOUSE_DEADZONE &&
        fabs(dy) <= MOUSE_DEADZONE)
        return;

    /* HID 鼠标标准报告格式 (4 字节)*/
    MouseReport_S mouse_report;

    /* Byte 0: Bit0左键 Bit1右键  暂时没有 */    
    mouse_report.buttons = 0;

    /* 限制范围在 -127 到 127 之间 */
    mouse_report.dx = (int8_t)constrain(dx, -127, 127);
    mouse_report.dy = (int8_t)constrain(dy, -127, 127);

    /* Byte 3: 滚轮方向 暂时没有 */

    mouse_report.wheel = 0;

    /* 写入设备节点，模拟硬件上报 */
    if (write(mouse_fd, &mouse_report, sizeof(mouse_report)) < 0) {
        /* 忽略非阻塞模式下的资源暂时不可用错误 */
    }
}

/**
 * @brief 根据手势识别结果 查表分发并发送一次对应的键盘字符
 */
static void usb_hid_keyboard_send_key(int action_id, int kbd_fd) {
    uint8_t target_keycode = 0;
    int i = 0;
    int ret = 0;
    KeyboardReport_S report = {0};

    /* 过滤无动作ID（NONE为0） 且确保键盘节点文件句柄有效 */
    if (action_id == 0 || kbd_fd < 0)
        return;

    /* 线性总线查表：遍历自定义配置表获取目标按键键码 */
    for (i = 0; i < ACTION_KEY_MAP_COUNT; i++) {
        if (g_action_key_book[i].action_id == action_id) {
            target_keycode = g_action_key_book[i].keycode;
            break;
        }
    }

    /* 若在表中没有注册有效的映射键码 退出 */
    if (target_keycode == 0) return;

    printf("[USB 键盘发包] 触发动作ID: %d, 映射发送键码: 0x%02X\n", 
           action_id, target_keycode);

    /* 构造标准 8 字节键盘报告并向内核写入按下状态 */
    report.modifiers = 0;
    report.reserved = 0;
    report.keycodes[0] = target_keycode;

    while (1) {
        ret = write(kbd_fd, &report, sizeof(report));
        if (ret == sizeof(report)) break;

        /* 若硬件缓冲区满 退避 1 毫秒后高频重试 */
        usleep(1000);              
    }

    /* 立刻发送全 0 清洗释放包 防止键盘按键卡死连发 */
    report.keycodes[0] = 0;

    while (1) {
        ret = write(kbd_fd, &report, sizeof(report));
        if (ret == sizeof(report)) {
            break;
        }
        usleep(1000);              
    }
}

/**
 * @brief 手势识别处理 (AI 结果映射键盘)
 */
void usb_hid_process_gesture(GestureShareData_S *res) {

    /* 
     * 纪录上一次成功处理的版本号
     * 防止 10ms 高频重复读
     */
    static uint32_t last_sent_seq = 0;

    uint32_t current_seq = 0;
    GestureResult_E local_predicted = 0;
    ComboActionId_E local_combo = 0;
    
    if (kbd_fd < 0) return;

    pthread_mutex_lock(&res->gesture_lock);
    
    current_seq     = res->update_seq;
    local_predicted = res->predicted_class;
    local_combo     = res->combo_action;
    
    pthread_mutex_unlock(&res->gesture_lock);

    if (current_seq == last_sent_seq) return;

    /* 同步本地版本号 */
    last_sent_seq = current_seq; 

    /* 优先看有没有连招 */
    if (local_combo != 0) {
        printf("连招:  %d \n", local_combo);
        usb_hid_keyboard_send_key(local_combo, kbd_fd);
        return;
    } 
    /* 单招检查并发送 */
    if (local_predicted != 0) {
        printf("单招: %s \n", GESTURE_NAMES[local_predicted]);
        usb_hid_keyboard_send_key(local_predicted, kbd_fd);
    }
}

/**
 * @brief 辅助函数：限制范围
 */
float constrain(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * @brief 清理资源
 */
void usb_hid_cleanup() {
    if (mouse_fd >= 0) {
        close(mouse_fd);
        mouse_fd = -1;
    }

    if (kbd_fd >= 0) {
        close(kbd_fd);
        kbd_fd = -1;
    }
    printf("[USB] HID 设备已关闭。\n");
}

/**
 * @brief 线程 3: USB HID 应用 (鼠标/手势)
 */
void* thread_usb_app(void* arg) {

    /* 记录上次发包的版本号 */
    uint32_t last_sent_seq = 0;

    printf("[Thread 3] USB 应用线程已启动...\n");

    /* 配置 ConfigFS 并打开 /dev/hidg0 */
    if (usb_hid_init()) return NULL; 

    while (g_keep_running) {
        Vector3_t curr_ema_gyro;
        int current_mode;
        bool has_new_data = false;

        pthread_mutex_lock(&g_data.g_data_lock);

        if (g_data.update_seq != last_sent_seq) {
            current_mode  = g_data.mode;
            curr_ema_gyro = g_data.ema_gyro;
            last_sent_seq = g_data.update_seq;
            has_new_data  = true;
        }

        pthread_mutex_unlock(&g_data.g_data_lock);

        if (!has_new_data) {

            /* 200Hz 足够满足 HID 响应 */
            usleep(5000); 
            continue;
        }

        if (current_mode == MODE_MOUSE) {

            /* 模式 A: 陀螺仪角速度映射鼠标位移 */
            usb_hid_process_mouse(curr_ema_gyro);
        }

        if (current_mode == MODE_GESTURE) {

            /* 模式 B: 获取 AI 模型识别手势结果并发包 */
            usb_hid_process_gesture(&g_gesture_data);

        }

        /* 200Hz 足够满足 HID 响应 */
        usleep(5000); 
    }
    usb_hid_cleanup();
    return NULL;
}