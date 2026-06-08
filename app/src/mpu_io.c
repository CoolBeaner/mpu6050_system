#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "mpu_io.h"
#include "common.h"
#include "algo.h"
#include "gesture_fifo.h"

#define MPU_DEV_PATH "/dev/mpu6050_0"

/* 根据 MPU6050 手册在特定量程下的灵敏度倒数 */
#define ACCEL_LSB_4G    8192.0f  
#define ACCEL_LSB_8G    4096.0f
#define GYRO_LSB_1000   32.8f    
#define GYRO_LSB_500    65.5f

/* --- 驱动初始化与打开 --- */
int mpu_io_init() {
    int fd = open(MPU_DEV_PATH, O_RDONLY);
    if (fd < 0) {
        perror("[IO] 初始打开 MPU6050 失败");
        return -1;
    }
    printf("[IO] 成功打开设备: %s\n", MPU_DEV_PATH);
    return fd;
}

/* --- 数据读取 (支持自动复活/热插拔) --- */
int mpu_io_read(int fd, ProcessedData_t *out_data) {
    RawData_t frame;
    static uint64_t last_ns = 0;

    /* 驱动层应该在 read 调用中返回符合 RawData_t 格式的字节流 */
    ssize_t len = read(fd, &frame, sizeof(RawData_t));

    if (len < 0) {

        /* 如果读取失败，可能是驱动正在重置 MPU6050 或设备掉线 */
        if (errno == ENODEV || errno == EIO) {
            printf("[IO] 设备连接异常 (EMI 或 掉线)，尝试静默恢复...\n");
            sleep(3);
        }

        return -1;
    }

    if (len != sizeof(RawData_t)) {
        /* 数据长度不完整 */
        return -2; 
    }

    /* 时间戳转换与 dt 计算 (纳秒 -> 秒) */
    out_data->timestamp = (double)frame.timestamp / SEC_CHENGE;

    /* 第一帧使用理论值 采样率的倒数 */
    if (last_ns != 0) {
        out_data->dt = (double)(frame.timestamp - last_ns) / SEC_CHENGE;
    } else {
        out_data->dt = 1.0 / IMU_SAMPLE_RATE_HZ;
    }

    last_ns = frame.timestamp;

    /* 加速度转换 (LSB -> g): 使用 4g 量程对应的 8192 */
    out_data->accel.x = (float)frame.data.accel_x / ACCEL_LSB_4G;
    out_data->accel.y = (float)frame.data.accel_y / ACCEL_LSB_4G;
    out_data->accel.z = (float)frame.data.accel_z / ACCEL_LSB_4G;

    /* 角速度转换 (LSB -> deg/s): 使用 1000 deg/s 量程对应的 32.8 */
    out_data->gyro.x = (float)frame.data.gyro_x / GYRO_LSB_1000;
    out_data->gyro.y = (float)frame.data.gyro_y / GYRO_LSB_1000;
    out_data->gyro.z = (float)frame.data.gyro_z / GYRO_LSB_1000;

    /* 温度转换 (MPU6050 标准公式) */
    out_data->temperature = ((float)frame.data.temp / 340.0f) + 36.53f;

    return 1;
}

/* --- 获取系统状态 (CPU/温度等) --- */
SysStatus_t mpu_io_get_sys_info() {
    SysStatus_t info = {0};
    
    /* 读取 H3 温度 (全志标准的 thermal zone) */
    FILE *t_fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (t_fp) {
        int raw_temp;
        if (fscanf(t_fp, "%d", &raw_temp) == 1) {
            /* 转化为摄氏度 */
            info.soc_temp = (float)raw_temp / 1000.0f; 
        }
        fclose(t_fp);
    }

    /* 读取 CPU 占用率 (解析 /proc/stat) */
    static long long last_user, last_nice, last_system, last_idle;
    static long long last_iowait, last_irq, last_softirq;
    long long user, nice, system, idle, iowait, irq, softirq;

    FILE *s_fp = fopen("/proc/stat", "r");
    if (s_fp) {
        if (fscanf(s_fp, "cpu %lld %lld %lld %lld %lld %lld %lld", 
                   &user, &nice, &system, &idle, 
                   &iowait, &irq, &softirq) >= 7) {
            
            long long total = user   + nice + \
                              system + idle + \
                              iowait + irq  + \
                              softirq;
            long long last_total = last_user   + last_nice + \
                                   last_system + last_idle + \
                                   last_iowait + last_irq  + \
                                   last_softirq;
            long long diff_idle  = idle  - last_idle;
            long long diff_total = total - last_total;

            if (diff_total > 0) {
                info.cpu_usage = 100.0f * 
                                 (1.0f - (float)diff_idle / diff_total);
            }

            last_user = user; 
            last_nice = nice; 
            last_system = system; 
            last_idle = idle;
            last_iowait = iowait; 
            last_irq = irq; 
            last_softirq = softirq;
        }
        fclose(s_fp);
    }
   
    /* 读取内存占用率 (解析 /proc/meminfo) */
    FILE *m_fp = fopen("/proc/meminfo", "r");
    if (m_fp) {
        char line[128];
        long mem_total = 0;
        long mem_free = 0;

        /* 逐行扫描文件，提取总内存和空闲内存 */
        while (fgets(line, sizeof(line), m_fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%ld", &mem_total);
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line + 8, "%ld", &mem_free);
                break;
            }
        }
        fclose(m_fp);

        /* 计算占用率：100% * (Total - Free) / Total */
        if (mem_total > 0) {
            info.mem_usage = 100.0f * (1.0f - (float)mem_free / mem_total);
        }
    }
    return info;
}

/* --- 资源关闭 --- */
void mpu_io_close(int fd) {
    if (fd >= 0) {
        close(fd);
        printf("[IO] MPU6050 设备已关闭。\n");
    }
}

/**
 *  @brief 线程 1: 数据采集与基础预处理 (500Hz)
 */
void* thread_sensor_acq(void* arg) {

    /* 给AI推理数据降速至 100HZ */
    static int downsample_counter = 0;

    printf("[Thread 1] 采集线程已启动...\n");
    
    /* 初始化驱动文件 */
    int fd = mpu_io_init();
    if (fd < 0) return NULL;

    while (g_keep_running) {
        ProcessedData_t raw;
        int ret = 0;

        /* 阻塞读取驱动数据 (驱动层带时间戳) */
        ret = mpu_io_read(fd, &raw);
        if (ret <= 0) continue;

        pthread_mutex_lock(&g_data.g_data_lock);

        bool  do_calib = g_data.calib_info.is_calibrating;
        bool  is_valid = g_data.calib_info.is_valid;
        float g_off_x  = g_data.calib_info.gyro_offset.x;
        float g_off_y  = g_data.calib_info.gyro_offset.y;
        float g_off_z  = g_data.calib_info.gyro_offset.z;
        float a_off_x  = g_data.calib_info.accel_offset.x;
        float a_off_y  = g_data.calib_info.accel_offset.y;
        float a_off_z  = g_data.calib_info.accel_offset.z;
        
        pthread_mutex_unlock(&g_data.g_data_lock);  

        /* 如果处于校准模式 零偏校准  */
        if (do_calib) {
            pthread_mutex_lock(&g_data.g_data_lock);

            algo_process_calibration(&raw, &g_data.calib_info);
            g_data.ema_accel.x = -999.0f;

            pthread_mutex_unlock(&g_data.g_data_lock);
            continue;
        }

        downsample_counter++;
        if (downsample_counter >= DOWNSAMPLE_FACTOR) 
            downsample_counter = 0;
            
#ifdef DATA_ACQUISITION
        if (g_record_flag == true &&
            downsample_counter == 0 &&
            g_record_idx < MAX_RECORD_POINTS) { 

            g_record_big_buf[g_record_idx][0] = raw.accel.x;
            g_record_big_buf[g_record_idx][1] = raw.accel.y;
            g_record_big_buf[g_record_idx][2] = raw.accel.z;
            g_record_big_buf[g_record_idx][3] = raw.gyro.x;
            g_record_big_buf[g_record_idx][4] = raw.gyro.y;
            g_record_big_buf[g_record_idx][5] = raw.gyro.z;
            g_record_idx++;            
        }        

        if(g_record_flag == true &&
           downsample_counter == 0 &&
           g_record_idx >= MAX_RECORD_POINTS) {

            printf("[STOP] 已超时 录制结束\n");
            g_record_flag = false; 
        }
#else
        /* 填入手势识别fifo */
        if (g_data.mode == MODE_GESTURE &&
            downsample_counter == 0)
            fifo_push(&g_gesture_fifo, &raw.accel, &raw.gyro);
#endif

        if (is_valid) {
            raw.gyro.x  -= g_off_x;
            raw.gyro.y  -= g_off_y;
            raw.gyro.z  -= g_off_z;
            raw.accel.x -= a_off_x;
            raw.accel.y -= a_off_y;
            raw.accel.z -= a_off_z;
        }

        /* 卡尔曼姿态解算 */
        Attitude_t attitude = algo_kalman_solve(raw.accel, 
                                                raw.gyro, 
                                                raw.dt);
                
        /* 执行 EMA 数字滤波 */
        Vector3_t curr_ema_accel, curr_ema_gyro;

        /* 处理 EMA 滤波器冷启动 */
        if (g_data.ema_accel.x == -999.0f) {
            curr_ema_accel = raw.accel;
            curr_ema_gyro  = raw.gyro;
        } else {
            curr_ema_accel = algo_ema_filter(raw.accel, 
                                             g_data.ema_accel, 
                                             ALPHA_ACCEL);
            curr_ema_gyro  = algo_ema_filter(raw.gyro, 
                                             g_data.ema_gyro, 
                                             ALPHA_GYRO);
        }

        pthread_mutex_lock(&g_data.g_data_lock);

        g_data.dt = raw.dt;
        /* mpu6050芯片温度 */
        g_data.temperature = raw.temperature;
        g_data.attitude    = attitude;
        g_data.ema_accel   = curr_ema_accel;
        g_data.ema_gyro    = curr_ema_gyro;
        g_data.accel       = raw.accel;
        g_data.gyro        = raw.gyro;
        g_data.update_seq++;

        pthread_mutex_unlock(&g_data.g_data_lock);
    }
    mpu_io_close(fd);
    return NULL;
}


/**
 * @brief 线程 4: 系统监控 (1Hz)
 */
void* thread_sys_monitor(void* arg) {
    printf("[Thread 4] 系统监控线程已启动...\n");
    while (g_keep_running) {

        /* 读取 CPU 占用率 温度等 */
        SysStatus_t current_sys_status = mpu_io_get_sys_info();
        
        pthread_mutex_lock(&g_data.g_data_lock);
        g_data.sys_status = current_sys_status;
        pthread_mutex_unlock(&g_data.g_data_lock);
        sleep(1); 
    }
    return NULL;
}