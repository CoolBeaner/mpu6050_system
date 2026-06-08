#ifndef __SHARE_UAPI_H__
#define __SHARE_UAPI_H__

#include <linux/types.h>

/* Ioctl */

#define MPU6050_IOC_MAGIC 'M'
#define MPU6050_IOC_SET_INT_DATA_RDY    _IOW(MPU6050_IOC_MAGIC, 1, int)
#define MPU6050_IOC_SET_ACCEL_RANGE     _IOW(MPU6050_IOC_MAGIC, 2, int)
#define MPU6050_IOC_SET_SMPLRT_DIV      _IOW(MPU6050_IOC_MAGIC, 3, int)
#define MPU6050_IOC_SET_SMPLRT_RATE     _IOW(MPU6050_IOC_MAGIC, 4, int)
#define MPU6050_IOC_SET_MOTION_DET      _IOW(MPU6050_IOC_MAGIC, 5, int)

/* Core structure */

/**
 * struct mpu6050_sensor_data - Raw data frame read from hardware FIFO
 * @accel_x:  Raw accelerometer X-axis reading
 * @accel_y:  Raw accelerometer Y-axis reading
 * @accel_z:  Raw accelerometer Z-axis reading
 * @temp:     Raw internal temperature reading
 * @gyro_x:   Raw gyroscope X-axis reading
 * @gyro_y:   Raw gyroscope Y-axis reading
 * @gyro_z:   Raw gyroscope Z-axis reading
 * @reserved: Padding for 16-byte structure alignment
 */
struct mpu6050_sensor_data {
    __s16 accel_x;
    __s16 accel_y;
    __s16 accel_z;
    __s16 temp;
    __s16 gyro_x;
    __s16 gyro_y;
    __s16 gyro_z;
    __s16 reserved;
};

/**
 * struct mpu6050_frame - Timestamped sensor data frame for kfifo
 * @data:      Raw sensor payload (accel, gyro, temp)
 * @timestamp: 64-bit monotonic timestamp (typically in nanoseconds)
 *
 * Marked as __packed to prevent compiler padding, ensuring a strict
 * 24-byte ABI size for safe copying to user space.
 */
struct mpu6050_frame {
    struct mpu6050_sensor_data data;
    __u64 timestamp;
} __attribute__((__packed__));

#endif