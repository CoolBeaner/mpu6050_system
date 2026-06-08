#ifndef __MPU6050_H__
#define __MPU6050_H__

/* HW Register Address */

#define MPU6050_REG_SELF_TEST_X          0x0D
#define MPU6050_REG_SELF_TEST_Y          0x0E
#define MPU6050_REG_SELF_TEST_Z          0x0F
#define MPU6050_REG_SELF_TEST_A          0x10
#define MPU6050_REG_SMPLRT_DIV           0x19
#define MPU6050_REG_CONFIG               0x1A
#define MPU6050_REG_GYRO_CONFIG          0x1B
#define MPU6050_REG_ACCEL_CONFIG         0x1C
#define MPU6050_REG_MOT_THR              0x1F
#define MPU6050_REG_MOT_DUR              0x20
#define MPU6050_REG_FIFO_EN              0x23
#define MPU6050_REG_I2C_MST_CTRL         0x24
#define MPU6050_REG_I2C_SLV0_ADDR        0x25
#define MPU6050_REG_I2C_SLV0_REG         0x26
#define MPU6050_REG_I2C_SLV0_CTRL        0x27
#define MPU6050_REG_I2C_SLV1_ADDR        0x28
#define MPU6050_REG_I2C_SLV1_REG         0x29
#define MPU6050_REG_I2C_SLV1_CTRL        0x2A
#define MPU6050_REG_I2C_SLV2_ADDR        0x2B
#define MPU6050_REG_I2C_SLV2_REG         0x2C
#define MPU6050_REG_I2C_SLV2_CTRL        0x2D
#define MPU6050_REG_I2C_SLV3_ADDR        0x2E
#define MPU6050_REG_I2C_SLV3_REG         0x2F
#define MPU6050_REG_I2C_SLV3_CTRL        0x30
#define MPU6050_REG_I2C_SLV4_ADDR        0x31
#define MPU6050_REG_I2C_SLV4_REG         0x32
#define MPU6050_REG_I2C_SLV4_DO          0x33
#define MPU6050_REG_I2C_SLV4_CTRL        0x34
#define MPU6050_REG_I2C_SLV4_D1          0x35
#define MPU6050_REG_I2C_MST_STATUS       0x36
#define MPU6050_REG_INT_PIN_CFG          0x37
#define MPU6050_REG_INT_ENABLE           0x38
#define MPU6050_REG_INT_STATUS           0x3A
#define MPU6050_REG_ACCEL_XOUT_H         0x3B
#define MPU6050_REG_ACCEL_XOUT_L         0x3C
#define MPU6050_REG_ACCEL_YOUT_H         0x3D
#define MPU6050_REG_ACCEL_YOUT_L         0x3E
#define MPU6050_REG_ACCEL_ZOUT_H         0x3F
#define MPU6050_REG_ACCEL_ZOUT_L         0x40
#define MPU6050_REG_TEMP_OUT_H           0x41
#define MPU6050_REG_TEMP_OUT_L           0x42
#define MPU6050_REG_GYRO_XOUT_H          0x43
#define MPU6050_REG_GYRO_XOUT_L          0x44
#define MPU6050_REG_GYRO_YOUT_H          0x45
#define MPU6050_REG_GYRO_YOUT_L          0x46
#define MPU6050_REG_GYRO_ZOUT_H          0x47
#define MPU6050_REG_GYRO_ZOUT_L          0x48
#define MPU6050_REG_EXT_SENS_DATA_00     0x49
#define MPU6050_REG_EXT_SENS_DATA_01     0x4A
#define MPU6050_REG_EXT_SENS_DATA_02     0x4B
#define MPU6050_REG_EXT_SENS_DATA_03     0x4C
#define MPU6050_REG_EXT_SENS_DATA_04     0x4D
#define MPU6050_REG_EXT_SENS_DATA_05     0x4E
#define MPU6050_REG_EXT_SENS_DATA_06     0x4F
#define MPU6050_REG_EXT_SENS_DATA_07     0x50
#define MPU6050_REG_EXT_SENS_DATA_08     0x51
#define MPU6050_REG_EXT_SENS_DATA_09     0x52
#define MPU6050_REG_EXT_SENS_DATA_10     0x53
#define MPU6050_REG_EXT_SENS_DATA_11     0x54
#define MPU6050_REG_EXT_SENS_DATA_12     0x55
#define MPU6050_REG_EXT_SENS_DATA_13     0x56
#define MPU6050_REG_EXT_SENS_DATA_14     0x57
#define MPU6050_REG_EXT_SENS_DATA_15     0x58
#define MPU6050_REG_EXT_SENS_DATA_16     0x59
#define MPU6050_REG_EXT_SENS_DATA_17     0x5A
#define MPU6050_REG_EXT_SENS_DATA_18     0x5B
#define MPU6050_REG_EXT_SENS_DATA_19     0x5C
#define MPU6050_REG_EXT_SENS_DATA_20     0x5D
#define MPU6050_REG_EXT_SENS_DATA_21     0x5E
#define MPU6050_REG_EXT_SENS_DATA_22     0x5F
#define MPU6050_REG_EXT_SENS_DATA_23     0x60
#define MPU6050_REG_I2C_SLV0_D0          0x63
#define MPU6050_REG_I2C_SLV1_D0          0x64
#define MPU6050_REG_I2C_SLV2_D0          0x65
#define MPU6050_REG_I2C_SLV3_D0          0x66
#define MPU6050_REG_I2C_MST_DELAY_CTRL   0x67
#define MPU6050_REG_SIGNAL_PATH_RESET    0x68
#define MPU6050_REG_MOT_DETECT_CTRL      0x69
#define MPU6050_REG_USER_CTRL            0x6A
#define MPU6050_REG_PWR_MGMT_1           0x6B
#define MPU6050_REG_PWR_MGMT_2           0x6C
#define MPU6050_REG_FIFO_COUNTH          0x72
#define MPU6050_REG_FIFO_COUNTL          0x73
#define MPU6050_REG_FIFO_R_W             0x74
#define MPU6050_REG_WHO_AM_I             0x75

/* Drv internal config */

#define MPU6050_MAX_DEVS                    2
#define MPU6050_I2C_RETRIES                 3

#define MPU6050_KFIFO_FRAMES                256
#define MPU6050_HW_FIFO_SIZE                1024
#define MPU6050_SENSOR_DATA_SIZE            14

/* About timestamps */

#define MPU6050_TS_RESYNC                   0
#define MPU6050_MAX_DRIFT_PERCENT           5
#define MPU6050_JITTER_TOLERANCE_PERCENT    20
#define MPU6050_EMA_MIN_COEFF               10
#define MPU6050_EMA_TIME_CONST_MS           500

/* HW register config */

#define MPU6050_ID_68                       0x68
#define MPU6050_ID_69                       0x69

#define MPU6050_GYRO_BASE_RATE_8K           8000
#define MPU6050_GYRO_BASE_RATE_1K           1000

#define MPU6050_PWR_WAKEUP                  0x00
#define MPU6050_PWR_SLEEP                   0x40
#define MPU6050_PWR_RESET                   0x80
#define MPU6050_PWR_CLKSEL_X_GYRO           0x01

#define MPU6050_INT_DATA_RDY_EN             0x01
#define MPU6050_INT_I2C_MST_EN              0x08
#define MPU6050_INT_FIFO_OFLOW_EN           0x10
#define MPU6050_INT_MOT_EN                  0x40
#define MPU6050_INT_DISABLE                 0x00

#define MPU6050_INT_STATUS_DATA_RDY         0x01
#define MPU6050_INT_STATUS_I2C_MST          0x08
#define MPU6050_INT_STATUS_FIFO_OFLOW       0x10
#define MPU6050_INT_STATUS_MOT              0x40

#define MPU6050_MOT_DUR_DEFAULT             10
#define MPU6050_INT_ACTIVE_LOW              (1 << 7)
#define MPU6050_INT_LATCH_EN                (1 << 5)

#define MPU6050_DLPF_256HZ                  0x00
#define MPU6050_DLPF_188HZ                  0x01
#define MPU6050_DLPF_98HZ                   0x02
#define MPU6050_DLPF_42HZ                   0x03
#define MPU6050_DLPF_RESERVED               0x07

#define MPU6050_SMPLRT_DIV_0                0x00
#define MPU6050_SMPLRT_DIV_1                0x01
#define MPU6050_SMPLRT_DIV_2                0x02
#define MPU6050_SMPLRT_DIV_3                0x03
#define MPU6050_SMPLRT_DIV_4                0x04

#define MPU6050_GYRO_FS_250DPS              0x00
#define MPU6050_GYRO_FS_500DPS              0x08
#define MPU6050_GYRO_FS_1000DPS             0x10
#define MPU6050_GYRO_FS_2000DPS             0x18

#define MPU6050_ACCEL_FS_2G                 0x00
#define MPU6050_ACCEL_FS_4G                 0x08
#define MPU6050_ACCEL_FS_8G                 0x10
#define MPU6050_ACCEL_FS_16G                0x18

#define MPU6050_SIGNAL_RESET_ALL            0x07

#define MPU6050_FIFO_EN_ALL_SENSORS         0xF8
#define MPU6050_FIFO_DIS_ALL_SENSORS        0x00

#define MPU6050_USER_CTRL_FIFO_EN_RESET     0x44
#define MPU6050_USER_CTRL_FIFO_RESET        0x04
#define MPU6050_USER_CTRL_FIFO_EN           0x40
#define MPU6050_USER_CTRL_FIFO_DIS          0x00

/* Internal structure definition */

/**
 * enum mpu6050_state - State machine for MPU6050 hardware and driver
 * @MPU_STATE_INIT:       Initialization or zero-user state. Hardware 
 * should be in low-power sleep mode with FIFO and interrupts disabled.
 * @MPU_STATE_RUNNING:    Normal operation state. Device is opened by at 
 * least one user. Hardware is actively writing to FIFO and triggering
 * interrupts.
 * @MPU_STATE_FAULT:      Fault detection state. Triggered when the IRQ 
 * handler detects a FIFO overflow or continuous I2C errors. Data reading 
 * is suspended pending recovery.
 * @MPU_STATE_RECOVERING: Fault recovery state. A workqueue is resetting
 * the hardware bus and restoring registers. Any read requests from user
 * space
 * should be blocked or dropped during this period.
 */
enum mpu6050_state {
    MPU_STATE_INIT = 0,
    MPU_STATE_RUNNING,
    MPU_STATE_FAULT,
    MPU_STATE_RECOVERING
};

/**
 * struct mpu6050_config - Cached hardware configuration
 * @chip_id:      Cached WHO_AM_I register value (expected 0x68)
 * @smplrt_div:   Sample rate divider
 * @dlpf_cfg:     Digital low-pass filter setting
 * @accel_range:  Accelerometer full-scale range (e.g., +/- 2g)
 * @gyro_range:   Gyroscope full-scale range (e.g., +/- 250dps)
 * @fifo_en_mask: Mask of sensors routed to the hardware FIFO
 * @int_en_mask:  Interrupt enable mask (e.g., data ready, overflow)
 * @reg_fifo_rw:  Address of the FIFO R/W register
 * @fingerprint:  Configuration checksum for fast state validation during 
 *                recovery
 */
struct mpu6050_config {
    u8 chip_id;
    u8 smplrt_div;
    u8 dlpf_cfg;
    u8 accel_range;
    u8 gyro_range;
    u8 fifo_en_mask;
    u8 int_en_mask;
    u8 reg_fifo_rw;
    u8 fingerprint;
};

/**
 * struct mpu6050_ts_sync - Time synchronization and jitter filter state
 * @sync_requests:      Counter for sync requests (must use atomic bitops
 *                      /atomic_t)
 * @fifo_overflow:      Flag indicating a hardware FIFO overflow occurred
 * @sample_rate_hz:     Configured sampling rate in Hz
 * @nominal_period_ns:  Expected time between samples in nanoseconds
 * @learn_threshold_ns: Threshold for initial period estimation
 * @max_drift_ns:       Maximum allowed drift before resetting the sync state
 * @filter_coeff:       EMA filter coefficient for smoothing the chip period
 * @chip_period:        Estimated actual hardware sampling period
 * @hardirq_ns:         Raw timestamp captured at the hardware interrupt handler
 * @last_ts:            Smoothed timestamp assigned to the last processed frame
 */
struct mpu6050_ts_sync {
    /* use atomic operations */
    unsigned long sync_requests;

    bool fifo_overflow; 

    u32 sample_rate_hz;
    u64 nominal_period_ns;
    u64 learn_threshold_ns;
    u64 max_drift_ns;
    u32 filter_coeff;

    u64 chip_period;
    u64 hardirq_ns;
    u64 last_ts;
};

/**
 * struct mpu6050_dev - Top-level device context for the MPU6050 driver
 * @cdev:         Character device representation
 * @device:       Device model pointer (for sysfs and dev_info/err/debug logs)
 * @minor:        Assigned minor number
 * @devid:        Allocated device ID (Major + Minor)
 * @irq:          Hardware interrupt number mapped from device tree
 * @irq_ctrl:     Interrupt control grouping
 * @irq_ctrl.enabled:  Current state of the hardware interrupt
 * @irq_ctrl.irq_lock: Mutex protecting the IRQ enable/disable state 
 *                     transitions.
 *                     The lock order is dev.lock --> irq_ctrl.irq_lock
 * @data_fifo:    Lockless ring buffer (kfifo) for parsed, timestamped sensor 
 *                frames
 * @hw_rx_buf:    Pre-allocated buffer for i2c_transfer to read raw data chunks
 *                (size MPU6050_HW_FIFO_SIZE) before timestamping and pushing 
 *                to kfifo
 * @read_queue:   Wait queue for blocking user-space read() operations
 * @exiting:      Atomic abort flag set by release() to interrupt the 120ms
 * @users:        Atomic reference count for open() and release() management
 * @monitor_work: Delayed work for monitoring hardware health and executing 
 *                recovery
 * @state:        Current state in the driver state machine
 * @client:       Pointer to the underlying I2C client device
 * @config:       Cached hardware configuration and calibration data
 * @lock:         Main mutex protecting I2C R/W operations, state machine 
 *                transitions,and config data. (Intended to protect ts_sync for 
 *                future ioctl support)
 * @ts_sync:      Time synchronization and jitter filtering context
 */
struct mpu6050_dev {
    struct cdev cdev;
    struct device *device;
    int minor;
    dev_t devid;

    int irq;
    struct {
        bool enabled;
        /* mutex dev.lock ---> mutex irq_lock  */
        struct mutex irq_lock;  
    } irq_ctrl;

    DECLARE_KFIFO_PTR(data_fifo, struct mpu6050_frame);
    u8 *hw_rx_buf;

    wait_queue_head_t read_queue;
    atomic_t exiting;
    atomic_t users; 

    struct delayed_work monitor_work;

    enum mpu6050_state state;
    struct i2c_client *client;
    struct mpu6050_config config;
    struct mutex lock;

    struct mpu6050_ts_sync ts_sync;
};

#endif
