#include <stdio.h>
#include <string.h>

#include "gesture_fifo.h"

int init_g_fifo(GestureFifo_t *fifo) {
    if (pthread_mutex_init(&fifo->fifo_lock, NULL) != 0) {
        return -1;
    }

    fifo->tail    = 0;
    fifo->count   = 0;
    fifo->is_full = false;
    
    memset(fifo->data, 0, sizeof(fifo->data));
    return 0;
}

void destroy_g_fifo(GestureFifo_t *fifo) {
    pthread_mutex_destroy(&fifo->fifo_lock);
}

void fifo_push(GestureFifo_t *fifo, Vector3_t *accel, Vector3_t *gyro) {
    pthread_mutex_lock(&fifo->fifo_lock); 

    /* 3 轴加速度 */
    fifo->data[fifo->tail][0] = accel->x;
    fifo->data[fifo->tail][1] = accel->y;
    fifo->data[fifo->tail][2] = accel->z;
    
    /* 3 轴陀螺仪 */
    fifo->data[fifo->tail][3] = gyro->x;
    fifo->data[fifo->tail][4] = gyro->y;
    fifo->data[fifo->tail][5] = gyro->z;

    /* tail 指针顺时针往前推，到 127 自动滚回 0 */
    fifo->tail = (fifo->tail + 1) % FIFO_SIZE;

    /* 开机后 128 个点第一次全部凑齐 */
    if (fifo->tail == 0)
        fifo->is_full = true; 

    /* 新到数据个数自增 */
    fifo->count++;

    pthread_mutex_unlock(&fifo->fifo_lock);
}

int fifo_get_window(GestureFifo_t *fifo, 
                    float out_window[FIFO_SIZE][GESTURE_CHANNELS]) {
    pthread_mutex_lock(&fifo->fifo_lock);

    /* 
     * 开机冷启动保护
     * 如果 128 个数据没用填满不做推理
     */
    if (!fifo->is_full) {
        pthread_mutex_unlock(&fifo->fifo_lock);
        return 0;
    }

    /* 如果新来的点数还没攒够 16 个数据不做推理 */
    if (fifo->count < FIFO_STRIDE) {
        pthread_mutex_unlock(&fifo->fifo_lock);
        return 0; 
    }

    /* 
     * 顺时针解环：
     * 从当前头指针位置 tail 开始 依次往后读 128 个数据
     */
    int read_ptr = fifo->tail;
    for (int i = 0; i < FIFO_SIZE; i++) {
        for (int j = 0; j < GESTURE_CHANNELS; j++) {
            out_window[i][j] = fifo->data[read_ptr][j];
        }
        read_ptr = (read_ptr + 1) % FIFO_SIZE;
    }

    fifo->count = 0;

    pthread_mutex_unlock(&fifo->fifo_lock);
    return 1;
}

#ifdef DATA_ACQUISITION

/**
 * @brief 线程 5: 数据采集线程
 *        通过串口控制
 */
void* thread_data_collection(void* arg) {

    /* 自动生成文件名后缀 */
    static int file_counter = 1;
    char filename[64];

    printf("[Thread 5] 魔法数据录制终端\n");

    while (g_keep_running) {
        printf("[提示] 按 回车键 开始/停止录制...\n");
        printf("[提示] 录制请前确保/目录下有tmp目录\n");

        /* 阻塞等待串口敲回车 */
        getchar(); 
        if (!g_keep_running) return NULL;
        
        g_record_idx = 0;
        g_record_flag = true;
        printf("[RECORD] 正在录制中... 挥舞你的魔杖 结束请再次敲 回车键 \n");
        printf("[RECORD] 最大录制时间大约为%ds 时间到会停止录制\n", 
               MAX_RECORD_TIME_SEC);
        printf("[RECORD] 但依然需要敲回车保存数据\n");

        /* 阻塞等待串口敲回车 */
        getchar();

        g_record_flag = false;
        int total_saved_points = g_record_idx;
        printf("[STOP] 录制结束 共捕获 %d 个点\n", total_saved_points);
        
        snprintf(filename, sizeof(filename), 
                 "/tmp/raw_gesture_%d.csv", file_counter++);

        FILE *fp = fopen(filename, "w");
        if (fp == NULL) {
            perror("文件打开失败");
            continue;
        }
        for (int i = 0; i < total_saved_points; i++) {
            /* 组装原始数据 */
            fprintf(fp, "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                    g_record_big_buf[i][0], g_record_big_buf[i][1], 
                    g_record_big_buf[i][2], g_record_big_buf[i][3], 
                    g_record_big_buf[i][4], g_record_big_buf[i][5]);
        }
        fclose(fp);
        printf("[SUCCESS] 原始数据已保存至: %s\n", filename);
    }

    return NULL;
}

#endif