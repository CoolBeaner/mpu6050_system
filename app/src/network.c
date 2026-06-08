#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "network.h"
#include "common.h"

#define PC_IP           "192.168.2.50"  // PC IP 地址
#define TELEMETRY_PORT  9000            // 发送数据的端口
#define COMMAND_PORT    9001            // 接收指令的端口

static int sockfd_tele = -1;
static int sockfd_cmd  = -1;
static struct sockaddr_in pc_addr;
static uint32_t seq_count = 0;

/* --- CRC16-CCITT 校验 --- */
static uint16_t calculate_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) 
                crc = (crc << 1) ^ 0x1021;
            else 
                crc <<= 1;
        }
    }
    return crc;
}

/* --- 初始化网络 --- */
int network_init() {

    /* 创建遥测发送 Socket (UDP) */
    sockfd_tele = socket(AF_INET, SOCK_DGRAM, 0);
    
    memset(&pc_addr, 0, sizeof(pc_addr));
    pc_addr.sin_family      = AF_INET;
    pc_addr.sin_port        = htons(TELEMETRY_PORT);
    pc_addr.sin_addr.s_addr = inet_addr(PC_IP);

    /* 创建指令接收 Socket (UDP) */
    sockfd_cmd = socket(AF_INET, SOCK_DGRAM, 0);
    
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(COMMAND_PORT);

    if (bind(sockfd_cmd, 
             (struct sockaddr *)&serv_addr, 
             sizeof(serv_addr)) < 0) {
        perror("[Network] 指令端口绑定失败");
        return -1;
    }

    /* 设置接收 Socket 为非阻塞模式 防止网络线程卡死 */
    int flags = fcntl(sockfd_cmd, F_GETFL, 0);

    fcntl(sockfd_cmd, F_SETFL, flags | O_NONBLOCK);
    printf("[Network] 网络组件初始化完成。目标 PC: %s\n", PC_IP);
    return 0;
}

/* --- 发送遥测数据 --- */
void network_send_telemetry(SharedData_t *data) {
    if (sockfd_tele < 0) return;

    /* 降帧处理 50Hz减轻网络压力 */
    static int send_divider_count = 0;
    TelemetryPkg_t pkg;

    memset(&pkg, 0, sizeof(pkg));

    /*
     * 降帧处理 减少网络发包压力
     * 用户看到的帧率60Hz就很流畅了
     * 为了保证高于60Hz因此需要排除
     * 系统调度延迟将帧率调整大于60Hz
     * 但不需要大太多
     */
    if (send_divider_count < NET_SEND_DIVIDER) { 
        send_divider_count++;
        return;
    }
    send_divider_count = 0;

    /* 数据打包 (从共享结构体抓取) */
    pkg.packet_seq   = seq_count++;
    pkg.dt           = data->dt;
    pkg.temperature  = data->temperature;
    pkg.attitude     = data->attitude;
    pkg.raw_accel    = data->ema_accel; // 发送滤波后的加速度用于绘图
    //pkg.raw_accel  = data->accel;
    pkg.sys          = data->sys_status;
    pkg.current_mode = (uint8_t)data->mode;

    /* 计算校验码 (排除结构体末尾的 checksum 字段本身) */
    pkg.checksum = calculate_crc16((uint8_t*)&pkg, 
                                   sizeof(pkg) - sizeof(uint16_t));

    ssize_t sent = sendto(sockfd_tele, &pkg, sizeof(pkg), 0, 
                          (struct sockaddr *)&pc_addr, sizeof(pc_addr));    
    if (sent < 0) {
        if (errno == ENETUNREACH || errno == EPIPE) {
            printf("[Network] 检测到断开，尝试重连...\n");
        }
    }
}

/* --- 检查 PC 指令 (非阻塞监听) --- */
void network_check_commands(SharedData_t *data) {
    uint8_t cmd_buffer[16];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (sockfd_cmd < 0) return;

    ssize_t len = recvfrom(sockfd_cmd, cmd_buffer, sizeof(cmd_buffer), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
    if (len > 0) {
        
        /*
         * 指令解析协议 
         * (示例：0x01:模式切换, 0x02:校准)
         */
        uint8_t cmd_type = cmd_buffer[0];
        uint8_t cmd_val  = cmd_buffer[1];

        switch (cmd_type) {
            case 0x01: // 切换模式
                data->mode = (Mode_e)cmd_val;
                printf("[Network] 指令：切换模式至 %d\n", cmd_val);
                break;
            case 0x02: // 启动校准
                data->calib_info.is_calibrating = true;
                data->calib_info.calib_count = 0;
                printf("[Network] 指令：开始零偏校准\n");
                break;
            default:
                printf("[Network] 未知指令: 0x%02X\n", cmd_type);
                break;
        }
    }
}

/* --- 清理资源 --- */
void network_cleanup() {
    if (sockfd_tele >= 0) close(sockfd_tele);
    if (sockfd_cmd  >= 0)  close(sockfd_cmd);
    printf("[Network] Socket 已关闭。\n");
}

/**
 * @brief 线程 2: 卡尔曼解算与网络遥测
 */
void* thread_network_telemetry(void* arg) {
    
    /* 记录上次发包的版本号 */
    uint32_t last_sent_seq = 0;
    SharedData_t local_snapshot;

    printf("[Thread 2] 网络遥测与卡尔曼线程已启动...\n");
    network_init(); 

    while (g_keep_running) {
        bool has_new_data = false;

        pthread_mutex_lock(&g_data.g_data_lock);

        if (g_data.update_seq != last_sent_seq) {
            local_snapshot = g_data;
            last_sent_seq  = g_data.update_seq;
            has_new_data   = true;
        }

        pthread_mutex_unlock(&g_data.g_data_lock);

        /*
        * 打包并发送 UDP 二进制数据 
        * 包含姿态与系统负载
        * 如果数据版本号没有更新
        * 就不发送并让出CPU时间
        */
        if (has_new_data) 
            network_send_telemetry(&local_snapshot);
        else 
            usleep(500);

        /* 监听 PC 端回传指令 (非阻塞) */
        pthread_mutex_lock(&g_data.g_data_lock);
        network_check_commands(&g_data);
        pthread_mutex_unlock(&g_data.g_data_lock);
    }
    network_cleanup();
    return NULL;
}
