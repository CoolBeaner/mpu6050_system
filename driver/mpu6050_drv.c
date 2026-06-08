#define DEBUG
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>

/**
 * Use "share_uapi.h" for full system co-development.
 * Use "mpu6050_uapi.h" for out-of-tree standalone driver builds. 
 */

/* #include "mpu6050_uapi.h" */
#include "share_uapi.h"

#include "mpu6050.h"

/*
 * Global context for multi-device support (e.g., I2C addrs 0x68 and 0x69).
 * The minor_lock ensures thread-safe assignment of minor numbers
 * when multiple MPU6050 devices are probed concurrently.
 */
static dev_t mpu6050_dev_base;      /* Allocated major & base minor number */
static struct class *mpu6050_class; /* Sysfs class for /dev node creation */
static int mpu6050_minor_cnt = 0;  /* Tracks the next available minor number */
static DEFINE_MUTEX(minor_lock);    /* Protects minor_cnt during probe() */

/**
 * mpu6050_safe_enable_irq - Idempotent IRQ enable wrapper
 * @dev: Top-level device context
 *
 * Checks the software state to prevent mismatched enable_irq() calls, 
 * which could lead to kernel IRQ depth counter imbalance.
 */
static void mpu6050_safe_enable_irq(struct mpu6050_dev *dev)
{
    /* mutex lock ---> mutex irq_lock  */

    mutex_lock(&dev->irq_ctrl.irq_lock);
    if (!dev->irq_ctrl.enabled) {
        enable_irq(dev->client->irq);
        dev->irq_ctrl.enabled = true;
        dev_dbg(&dev->client->dev, "IRQ safely enabled\n");
    }
    mutex_unlock(&dev->irq_ctrl.irq_lock);
}

/**
 * mpu6050_safe_disable_irq_nosync - Idempotent IRQ disable wrapper
 * @dev: Top-level device context
 *
 * Safely disables the interrupt without waiting for the handler to complete.
 * Used primarily in error paths and the monitor worker to avoid deadlocks.
 */
static void mpu6050_safe_disable_irq_nosync(struct mpu6050_dev *dev)
{
    /* mutex lock ---> mutex irq_lock  */

    mutex_lock(&dev->irq_ctrl.irq_lock);
    if (dev->irq_ctrl.enabled) {
        disable_irq_nosync(dev->client->irq);
        dev->irq_ctrl.enabled = false;
        dev_dbg(&dev->client->dev, "IRQ safely disabled\n");
    }
    mutex_unlock(&dev->irq_ctrl.irq_lock);
}

/**
 * mpu6050_update_timing_params - Recalculate timestamping thresholds
 * @config: Current hardware configuration shadow
 * @ts:     Time synchronization context to update
 *
 * Derives software timing boundaries (expected period, drift limits,
 * and EMA filter weight) from the active hardware sample rate.
 */
static void mpu6050_update_timing_params(struct mpu6050_config *config,
                                         struct mpu6050_ts_sync *ts)
{
    u32 gyro_rate;

    /*
     * Datasheet quirk: Gyroscope output rate is 8kHz when DLPF is disabled
     * (DLPF_CFG = 0 or 7). For all other DLPF settings, it operates at 1kHz.
     */
    if (config->dlpf_cfg == MPU6050_DLPF_256HZ || 
        config->dlpf_cfg == MPU6050_DLPF_RESERVED)
        gyro_rate = MPU6050_GYRO_BASE_RATE_8K;
    else 
        gyro_rate = MPU6050_GYRO_BASE_RATE_1K;

    ts->sample_rate_hz = gyro_rate / (1 + config->smplrt_div);
    ts->nominal_period_ns = div_s64(NSEC_PER_SEC, ts->sample_rate_hz);
    ts->learn_threshold_ns = div_s64(ts->nominal_period_ns *
                                          MPU6050_JITTER_TOLERANCE_PERCENT, 
                                          100);
    ts->max_drift_ns = div_s64(ts->nominal_period_ns *
                                    MPU6050_MAX_DRIFT_PERCENT,
                                    100);
    /* 
     * Translate the fixed time constant (ms) into a dynamic sample count 
     * (filter_coeff) based on the current Hz. Clamp to a minimum to prevent 
     * the EMA from stalling.
     */
    ts->filter_coeff = (ts->sample_rate_hz * MPU6050_EMA_TIME_CONST_MS) / 1000;

    if (ts->filter_coeff < MPU6050_EMA_MIN_COEFF)
        ts->filter_coeff = MPU6050_EMA_MIN_COEFF;

    /*
     * Hardware timing has been altered. Invalidate current EMA estimations
     * and force a hard resynchronization on the next incoming IRQ.
     */
    set_bit(MPU6050_TS_RESYNC, &ts->sync_requests);

    pr_debug("ts->sample_rate_hz: %u\n", ts->sample_rate_hz);
    pr_debug("ts->nominal_period_ns: %llu\n", ts->nominal_period_ns);
    pr_debug("ts->learn_threshold_ns: %llu\n", ts->learn_threshold_ns);
    pr_debug("ts->max_drift_ns: %llu\n", ts->max_drift_ns);
    pr_debug("ts->filter_coeff: %u\n", ts->filter_coeff);
}

/**
 * mpu6050_hw_init - Wakes up hardware and applies current configurations
 * @dev: Top-level device context
 *
 * Applies the cached register settings from dev->config directly to the MPU6050.
 * This function handles identity verification, software reset timing, clock
 * stabilization, and baseline register provisioning.
 *
 * Return: 0 on success, negative error code on I2C failure.
 */
static int mpu6050_hw_init(struct mpu6050_dev *dev)
{
    struct i2c_client *client = dev->client;
    int ret;
    u8 chip_id;

    /* Verify hardware identity (WHO_AM_I) before proceeding */
    ret = i2c_smbus_read_byte_data(client, MPU6050_REG_WHO_AM_I);
    if (ret < 0) {
        dev_err(&client->dev, 
                "Failed to read WHO_AM_I register (err:%d)\n", 
                ret);
        return ret;
    }

    chip_id = (u8)ret;
    if (chip_id != MPU6050_ID_68 && 
        chip_id != MPU6050_ID_69) {
        dev_err(&client->dev, 
                "Device ID mismatch Found 0x%02x\n", 
                chip_id);
        return -ENODEV;
    }

    dev->config.chip_id = chip_id;
    dev_dbg(&client->dev, "MPU6050_ID: 0x%02x\n", chip_id);

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_PWR_MGMT_1, 
                                    MPU6050_PWR_RESET);
    if (ret < 0) return ret;

    /*
     * Datasheet requirement: Wait at least 100ms after a software reset.
     * This ensures all internal signal paths and registers are fully 
     * cleared before any subsequent I2C operations.
     */
    msleep(100);

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_PWR_MGMT_1, 
                                    MPU6050_PWR_WAKEUP |
                                    MPU6050_PWR_CLKSEL_X_GYRO);
    if (ret < 0) return ret;

    /*
     * Wait for the PLL to lock and stabilize. 
     * Switching the clock source from the internal 8MHz oscillator to 
     * the Gyro X-axis reference requires a brief stabilization period.
     */
    msleep(20); 

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_SMPLRT_DIV, 
                                    dev->config.smplrt_div);
    if (ret < 0) return ret;
    
    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_CONFIG, 
                                    dev->config.dlpf_cfg);
    if (ret < 0) return ret;

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_GYRO_CONFIG, 
                                    dev->config.gyro_range);
    if (ret < 0) return ret;

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_ACCEL_CONFIG, 
                                    dev->config.accel_range);
    if (ret < 0) return ret;

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_PWR_MGMT_1, 
                                    MPU6050_PWR_SLEEP);
    if (ret < 0) return ret;

    return 0;
}

/**
 * mpu6050_fill_shadow_config - Populates the shadow configuration with defaults
 * @dev: Top-level device context
 *
 * Initializes the software cache (dev->config) with baseline parameters.
 * This can be safely called before physical hardware is connected or verified.
 */
static void mpu6050_fill_shadow_config(struct mpu6050_dev *dev)
{
    /* * Populate the shadow configuration.
     * These defaults yield a 500Hz sample rate with a 42Hz low-pass filter.
     */
    dev->config.smplrt_div   = MPU6050_SMPLRT_DIV_1;
    dev->config.dlpf_cfg     = MPU6050_DLPF_42HZ;
    dev->config.gyro_range   = MPU6050_GYRO_FS_1000DPS;
    dev->config.accel_range  = MPU6050_ACCEL_FS_4G;
    dev->config.fifo_en_mask = MPU6050_FIFO_EN_ALL_SENSORS;
    dev->config.int_en_mask  = MPU6050_INT_DATA_RDY_EN | 
                               MPU6050_INT_FIFO_OFLOW_EN;
    dev->config.reg_fifo_rw  = MPU6050_REG_FIFO_R_W;
    dev->config.fingerprint  = MPU6050_DLPF_42HZ;

    mpu6050_update_timing_params(&dev->config, &dev->ts_sync);
}

static int mpu6050_recover(struct mpu6050_dev *dev)
{
    struct i2c_client *client = dev->client;
    int ret;

    //msleep(1000);
    i2c_recover_bus(client->adapter);

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_PWR_MGMT_1, 
                                    MPU6050_PWR_RESET);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Reset (err: %d)\n", 
                ret);
        return ret;
    }

    msleep(100);

    if (atomic_read(&dev->exiting)) {
        dev_dbg(&client->dev, 
                "Recover: Aborted due to device release.\n");
        return -ENODEV;
    }

    /* reset gyro accel temp adc */
    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_SIGNAL_PATH_RESET, 
                                    MPU6050_SIGNAL_RESET_ALL);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Reset SIGNAL_PATH (err: %d)\n", 
                ret);
        return ret;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_PWR_MGMT_1, 
                                    MPU6050_PWR_WAKEUP | 
                                    MPU6050_PWR_CLKSEL_X_GYRO);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Wakeup (err: %d)\n", 
                ret);
        return ret;
    }

    msleep(20);

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_SMPLRT_DIV, 
                                    dev->config.smplrt_div);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set DIV (err: %d)\n", 
                ret);
        goto out_error;
    }
    
    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_CONFIG, 
                                    dev->config.dlpf_cfg);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set DLPF (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_GYRO_CONFIG, 
                                    dev->config.gyro_range);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set GYRO_RANGE (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_ACCEL_CONFIG, 
                                    dev->config.accel_range);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set ACCEL_RANGE (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_USER_CTRL, 
                                    MPU6050_USER_CTRL_FIFO_DIS);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Disable FIFO (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_USER_CTRL, 
                                    MPU6050_USER_CTRL_FIFO_RESET);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Reset FIFO (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_FIFO_EN, 
                                    dev->config.fifo_en_mask);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set FIFO_EN_MASK (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_USER_CTRL, 
                                    MPU6050_USER_CTRL_FIFO_EN);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to EN FIFO (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_write_byte_data(client, 
                                    MPU6050_REG_INT_ENABLE, 
                                    dev->config.int_en_mask);
    if (ret < 0) {
        dev_dbg(&client->dev, 
                "Recover: Failed to Set INT_EN_MASK (err: %d)\n", 
                ret);
        goto out_error;
    }

    ret = i2c_smbus_read_byte_data(client, MPU6050_REG_CONFIG);
    if (ret < 0 || (u8)ret != dev->config.fingerprint) {
        dev_dbg(&client->dev, 
                "Recover: Bad fingerprint: 0x%02x\n", 
                ret);
        ret = -EIO;
        goto out_error;
    }

    ret = i2c_smbus_read_byte_data(client, 
                                   MPU6050_REG_WHO_AM_I);
    if (ret < 0 || (u8)ret != dev->config.chip_id) {
        dev_dbg(&client->dev, 
                "Recover: Error MPU6050_ID: 0x%02x\n", 
                ret);
        ret = -EIO;
        goto out_error;
    }

    return 0;

out_error:
    i2c_smbus_write_byte_data(client, 
                              MPU6050_REG_PWR_MGMT_1, 
                              MPU6050_PWR_SLEEP);
    return ret;
}

static int mpu6050_open(struct inode *inode, struct file *filp)
{
    struct mpu6050_dev *dev = container_of(inode->i_cdev, 
                                           struct mpu6050_dev, 
                                           cdev);
    struct i2c_client *client = dev->client;
    int user_count;
    int ret;

    user_count = atomic_inc_return(&dev->users);

    switch (READ_ONCE(dev->state)) {
        case MPU_STATE_FAULT:       
            ret = -EIO;
            goto err_dec_users;

        case MPU_STATE_RECOVERING:  
            ret = -EBUSY;
            goto err_dec_users;
            
        case MPU_STATE_INIT:         break;
        case MPU_STATE_RUNNING:      break; 
        default:                    
            ret = -EINVAL;
            goto err_dec_users;
    }

    if (mutex_lock_interruptible(&dev->lock)) {
        ret = -ERESTARTSYS;
        goto err_dec_users;
    }

    if (dev->state != MPU_STATE_RUNNING && 
        dev->state != MPU_STATE_INIT) {
        dev_dbg(&client->dev, 
                "Open rejected: hardwawre is fault/recovering\n");
        ret = -EIO;
        goto err_unlock;
    }

    if (user_count == 1) {
        ret = mpu6050_hw_init(dev);
        if (ret) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to initialization (err: %d)\n", 
                    ret);
            ret = -EIO;
            goto err_unlock;
        }

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_SIGNAL_PATH_RESET, 
                                        MPU6050_SIGNAL_RESET_ALL);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Reset SIGNAL_PATH (err: %d)\n", 
                    ret);
            ret = -EIO;
            goto err_unlock;
        }          

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_PWR_MGMT_1, 
                                        MPU6050_PWR_WAKEUP |
                                        MPU6050_PWR_CLKSEL_X_GYRO);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Wakeup (err: %d)\n", 
                    ret);
            ret = -EIO;
            goto err_unlock;
        }

        msleep(20);

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_USER_CTRL, 
                                        MPU6050_USER_CTRL_FIFO_DIS);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Disable FIFO (err: %d)\n", 
                    ret);
            goto err_hw_sleep;
        }

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_USER_CTRL, 
                                        MPU6050_USER_CTRL_FIFO_RESET);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Reset FIFO (err: %d)\n", 
                    ret);
            goto err_hw_sleep;
        }

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_FIFO_EN, 
                                        dev->config.fifo_en_mask);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Set FIFO_EN_MASK (err: %d)\n", 
                    ret);
            goto err_hw_sleep;
        }

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_USER_CTRL, 
                                        MPU6050_USER_CTRL_FIFO_EN);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to EN FIFO (err: %d)\n", 
                    ret);
            goto err_hw_sleep;
        }

        ret = i2c_smbus_write_byte_data(client, 
                                        MPU6050_REG_INT_ENABLE, 
                                        dev->config.int_en_mask);
        if (ret < 0) {
            dev_err(&client->dev, 
                    "Open rejected: Failed to Set INT_EN_MASK (err: %d)\n", 
                    ret);
            goto err_hw_sleep;
        }

        dev->state = MPU_STATE_RUNNING;
        set_bit(MPU6050_TS_RESYNC, &dev->ts_sync.sync_requests);
        kfifo_reset(&dev->data_fifo);
        /* mutex lock ---> mutex irq_lock  */
        mpu6050_safe_enable_irq(dev);
        schedule_delayed_work(&dev->monitor_work, msecs_to_jiffies(5000));
    } else {
        dev_dbg(&client->dev, 
                "Device is exclusively opened by another process.\n");
        ret = -EBUSY;
        goto err_unlock;
    }
    
    filp->private_data = dev;
    dev_dbg(&client->dev, 
            "Device opened successfully. users: %d\n", 
            user_count);
    mutex_unlock(&dev->lock);
    return 0;

err_hw_sleep:
    i2c_smbus_write_byte_data(client, 
                              MPU6050_REG_PWR_MGMT_1, 
                              MPU6050_PWR_SLEEP); 

err_unlock:
    mutex_unlock(&dev->lock);

err_dec_users:
    atomic_dec(&dev->users);

    return ret;
}

static ssize_t mpu6050_read(struct file *filp, 
                            char __user *buf, 
                            size_t count, 
                            loff_t *off)
{
    struct mpu6050_dev *dev = filp->private_data;
    size_t frame_size = sizeof(struct mpu6050_frame);
    int ret;
    unsigned int copied;

    if (count < frame_size)     
        return -EINVAL;

    if (kfifo_is_empty(&dev->data_fifo) && (filp->f_flags & O_NONBLOCK))
        return -EAGAIN;

    ret = wait_event_interruptible(dev->read_queue,
                                   !kfifo_is_empty(&dev->data_fifo) ||
                                   READ_ONCE(dev->state) == MPU_STATE_FAULT);
    if (ret)    
        return ret;

    if (READ_ONCE(dev->state) == MPU_STATE_FAULT) {
        dev_dbg(&dev->client->dev, 
                "Read aborted: hardware is in FAULT state.\n");
        return -EIO;
    }

    count = (count / frame_size) * frame_size;

    ret = kfifo_to_user(&dev->data_fifo, buf, count, &copied);
    if (ret)    
        return -EFAULT;

    return copied;
}

static int mpu6050_release(struct inode *inode, struct file *filp)
{
    struct mpu6050_dev *dev = filp->private_data;
    struct i2c_client *client = dev->client;
    int user_count;

    user_count = atomic_dec_return(&dev->users);

    if (user_count == 0) {
        atomic_set(&dev->exiting, 1);
        cancel_delayed_work_sync(&dev->monitor_work);

        mutex_lock(&dev->lock);
        dev->state = MPU_STATE_INIT;
        mpu6050_safe_disable_irq_nosync(dev);
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_INT_ENABLE, 
                                  MPU6050_INT_DISABLE);
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_USER_CTRL, 
                                  MPU6050_USER_CTRL_FIFO_DIS);
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_FIFO_EN, 
                                  MPU6050_FIFO_DIS_ALL_SENSORS);
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_PWR_MGMT_1, 
                                  MPU6050_PWR_SLEEP);
        mutex_unlock(&dev->lock);
        kfifo_reset(&dev->data_fifo);
        atomic_set(&dev->exiting, 0);
    }

    dev_dbg(&client->dev, "Device released successfully.\n");

    return 0;
}

/* Not yet achieved */
static long mpu6050_unlocked_ioctl(struct file *filp, 
                                   unsigned int cmd, 
                                   unsigned long arg)
{
    struct mpu6050_dev *dev = filp->private_data;
    int val;
    int ret = 0;
    u8 cfg;

    if (_IOC_TYPE(cmd) != MPU6050_IOC_MAGIC)
        return -ENOTTY;

    mutex_lock(&dev->lock);

    switch (cmd) {
        case MPU6050_IOC_SET_INT_DATA_RDY:
            if (val) {
                cfg = MPU6050_INT_ACTIVE_LOW | MPU6050_INT_LATCH_EN;
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_PIN_CFG, 
                                          cfg);
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_ENABLE, 
                                          MPU6050_INT_DATA_RDY_EN);
            } else {
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_ENABLE,  
                                          MPU6050_INT_DISABLE);
            }
            break;
        case MPU6050_IOC_SET_ACCEL_RANGE:
            if (val < 0 || val > 3) {
                ret = -EINVAL;
                break;
            }
            i2c_smbus_write_byte_data(dev->client, 
                                      MPU6050_REG_ACCEL_CONFIG, 
                                      (val << 3));
            break;
        case MPU6050_IOC_SET_SMPLRT_DIV:
            if (val < 0 || val > 255) {
                ret = -EINVAL;
                break;
            }
            i2c_smbus_write_byte_data(dev->client, 
                                      MPU6050_REG_SMPLRT_DIV , 
                                      val);
            break;
        case MPU6050_IOC_SET_SMPLRT_RATE:
            if (val <= 0 || val > MPU6050_GYRO_BASE_RATE_1K) {
                ret = -EINVAL;
                break;
            }
            cfg = (MPU6050_GYRO_BASE_RATE_1K / val) - 1;
            i2c_smbus_write_byte_data(dev->client, 
                                      MPU6050_REG_SMPLRT_DIV , 
                                      cfg);
            break;
        case MPU6050_IOC_SET_MOTION_DET:
            if (val > 0) {
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_MOT_THR, 
                                          (u8)val);
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_MOT_DUR, 
                                          MPU6050_MOT_DUR_DEFAULT);
                cfg = MPU6050_INT_ACTIVE_LOW | MPU6050_INT_LATCH_EN;
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_PIN_CFG, cfg);
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_ENABLE, 
                                          MPU6050_INT_MOT_EN);

            } else {
                i2c_smbus_write_byte_data(dev->client, 
                                          MPU6050_REG_INT_ENABLE, 
                                          MPU6050_INT_DATA_RDY_EN);
            }
            break;
        default:
            ret = -ENOTTY;
            break;
    }

    mutex_unlock(&dev->lock);
    return ret;
}

static const struct file_operations mpu6050_fops = {
    .owner = THIS_MODULE,
    .open = mpu6050_open,
    .read = mpu6050_read,
    .release = mpu6050_release,
    .unlocked_ioctl = mpu6050_unlocked_ioctl,
};

/**
 * mpu6050_monitor_fn - Periodic watchdog and self-healing state machine
 * @work: Pointer to the delayed work struct embedding this task
 *
 * Runs periodically (e.g., every 5 seconds) to monitor hardware health.
 * In the RUNNING state, it polls key registers (WHO_AM_I and CONFIG fingerprint)
 * to detect silent hardware resets, power glitches, or I2C bus lockups. 
 * If an anomaly is detected, it forces a FAULT state and suspends IRQs.
 * If a FAULT is active, it transitions to RECOVERING, attempts to re-initialize 
 * the hardware from the cached config, and if successful, seamlessly resumes 
 * operation and requests a timestamp resynchronization.
 */
static void mpu6050_monitor_fn(struct work_struct *work)
{
    int ret;
    struct mpu6050_dev *dev = container_of(work, 
                                           struct mpu6050_dev, 
                                           monitor_work.work);

    mutex_lock(&dev->lock);

    switch (dev->state) {
        case MPU_STATE_RUNNING:
            ret = i2c_smbus_read_byte_data(dev->client, 
                                           MPU6050_REG_CONFIG);
            if (ret < 0 || ret != dev->config.fingerprint) {
                dev_err(&dev->client->dev, 
                        "Monitor: Fingerprint lost (read: 0x%02x).\n",
                        ret);
                dev->state = MPU_STATE_FAULT;
                /* mutex lock ---> mutex irq_lock  */
                mpu6050_safe_disable_irq_nosync(dev);
                break;
            }

            ret = i2c_smbus_read_byte_data(dev->client, 
                                           MPU6050_REG_WHO_AM_I);
            if (ret < 0 || (u8)ret != dev->config.chip_id) {
                dev_err(&dev->client->dev, 
                        "Monitor: Error MPU6050_ID: 0x%02x\n", 
                        ret);
                dev->state = MPU_STATE_FAULT;
                /* mutex lock ---> mutex irq_lock  */
                mpu6050_safe_disable_irq_nosync(dev);
                break;
            }

            break;

        case MPU_STATE_FAULT:
            dev->state = MPU_STATE_RECOVERING;
            dev_info(&dev->client->dev, 
                     "Monitor: Attempting hardware recovery.\n");

            ret = mpu6050_recover(dev);
            if (ret == 0) {
                dev->state = MPU_STATE_RUNNING;
                set_bit(MPU6050_TS_RESYNC, &dev->ts_sync.sync_requests);
                /* mutex lock ---> mutex irq_lock  */
                mpu6050_safe_enable_irq(dev);
                dev_info(&dev->client->dev,
                    "Monitor: Recovery successful, Hardware is back online.\n");
            } else {
                dev_err(&dev->client->dev, 
                        "Monitor: Recovery failed (err: %d).\n", 
                        ret);
                dev->state = MPU_STATE_FAULT;
            }
            break;

        case MPU_STATE_RECOVERING:
        case MPU_STATE_INIT:
        default:
            break;
    }

    mutex_unlock(&dev->lock);
    schedule_delayed_work(&dev->monitor_work, msecs_to_jiffies(5000));
}

static irqreturn_t mpu6050_irq_top_half(int irq, void *dev_id)
{
    struct mpu6050_dev *dev = dev_id;

    dev->ts_sync.hardirq_ns = ktime_get_boottime_ns();

    return IRQ_WAKE_THREAD;
}

/**
 * mpu6050_calc_timestamps - Estimate and smooth sensor frame timestamps
 * @ts:           Time synchronization context
 * @frames_count: Number of discrete frames extracted from the hardware FIFO
 *
 * This function eliminates hardirq jitter by maintaining an Exponential 
 * Moving Average (EMA) of the hardware's true sampling period. It ensures
 * that timestamps presented to user space are monotonically increasing 
 * and evenly spaced, while slowly tracking long-term oscillator drift.
 */
static void mpu6050_calc_timestamps(struct mpu6050_ts_sync *ts, 
                                    int frames_count)
{
    /* Atomically fetch and clear any pending manual resync requests */
    bool async_resync = test_and_clear_bit(MPU6050_TS_RESYNC, 
                                           &ts->sync_requests);
    s64 expected_elapsed;
    s64 error;

    /*
     * Hard Reset Condition:
     * If parameters changed (async_resync), it's the very first frame 
     * (last_ts == 0), or we lost data (fifo_overflow), the time continuum
     * is broken.
     * We must snap to the raw IRQ time and restart the learning process.
     */
    if (async_resync || ts->last_ts == 0 || ts->fifo_overflow) {
        ts->last_ts = ts->hardirq_ns;
        ts->chip_period = ts->nominal_period_ns;
        ts->fifo_overflow = false;
        return;
    }

    /* Calculate ideal time elapsed based on our smoothed period estimate */    
    expected_elapsed = frames_count * ts->chip_period;
    /* 
     * Phase Error: Difference between actual IRQ arrival and expected 
     * arrival 
     */
    error = ts->hardirq_ns - (ts->last_ts + expected_elapsed);

    /*
     * Jitter Rejection and Period Learning:
     * Only update the period estimate if the error is within the learn threshold.
     * Huge errors mean the CPU scheduling was delayed, not the hardware clock.
     */
    if (abs(error) < ts->learn_threshold_ns) {
        /* 
         * EMA filter: Slowly steer the estimated period towards the measured 
         * reality 
         */    
        ts->chip_period += div_s64(error, ts->filter_coeff * frames_count);        
        /* 
         * Clamp the period to prevent the filter from drifting away due to 
         * anomalies 
         */
        if (ts->chip_period > (ts->nominal_period_ns + ts->max_drift_ns))
            ts->chip_period = ts->nominal_period_ns + ts->max_drift_ns;
        if (ts->chip_period < (ts->nominal_period_ns - ts->max_drift_ns))
            ts->chip_period = ts->nominal_period_ns - ts->max_drift_ns;
    } else {
        /* 
         * Outlier detected (e.g., severe I2C bus delay or CPU load spike).
         * Ignore this IRQ timestamp for learning, rely entirely on the 
         * historically smoothed chip_period. 
         */        
    }
    /*
     * Advance the virtual timeline:
     * Do NOT use hardirq_ns here. We advance the time by the mathematically
     * smoothed elapsed time. This guarantees zero-jitter spacing between frames.
     */
    ts->last_ts += expected_elapsed;
}

static irqreturn_t mpu6050_irq_thread_fn(int irq, void *dev_id)
{
    struct mpu6050_dev *dev = dev_id;
    struct i2c_client *client = dev->client;
    struct i2c_msg msgs[2];
    struct mpu6050_frame frame = {0};
    u8 fifo_count_buf[2];
    u8 int_status, *raw_buf;
    u16 fifo_count;
    int frames_count, i, ret; 
    int bytes_count;
    int retries = MPU6050_I2C_RETRIES;
    
   /*
    * Check the state of the state machine. Only when it is in the "running" 
    * state can it proceed.
    */
    if (READ_ONCE(dev->state) == MPU_STATE_RECOVERING) 
        return IRQ_HANDLED;

    mutex_lock(&dev->lock);

    if (dev->state == MPU_STATE_FAULT) {
        mod_delayed_work(system_wq, &dev->monitor_work, 0);
        goto unlock_out;
    } 

    if (dev->state != MPU_STATE_RUNNING)
        goto unlock_out;


    /* Check the interrupt flag and handle it. */

    while (retries--) {
        ret = i2c_smbus_read_byte_data(client, MPU6050_REG_INT_STATUS);
        if (ret >= 0)   break;
        usleep_range(1000, 2000);
    }
    if (ret < 0)    goto fail_read;

    retries = MPU6050_I2C_RETRIES;
    int_status = (u8)ret;

    if (int_status & MPU6050_INT_STATUS_FIFO_OFLOW) {
        dev_warn_ratelimited(&client->dev, 
                             "Hareware FIFO overflow! Resetting.\n" );
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_USER_CTRL, 
                                  MPU6050_USER_CTRL_FIFO_EN_RESET);
        dev->ts_sync.fifo_overflow = true;
        goto unlock_out;
    }

    if (int_status & MPU6050_INT_STATUS_MOT) {
        // mpu6050_handler_motion_event(dev);
        goto unlock_out;
    }

    if (int_status & MPU6050_INT_STATUS_I2C_MST) {
        // mpu6050_handler_i2c_master_event(dev);
        goto unlock_out;
    }

    if (!(int_status & MPU6050_INT_STATUS_DATA_RDY)) 
        goto unlock_out;

    while (retries--) {
        ret = i2c_smbus_read_i2c_block_data(client, 
                                            MPU6050_REG_FIFO_COUNTH, 2, 
                                            fifo_count_buf);
        if (ret >= 0)   break;
        usleep_range(1000, 2000);
    }
    if (ret < 0)    goto fail_read;

    fifo_count = get_unaligned_be16(fifo_count_buf);

    if (fifo_count < MPU6050_SENSOR_DATA_SIZE)    
        goto unlock_out;

    if (fifo_count >= MPU6050_HW_FIFO_SIZE) {
        dev_warn_ratelimited(&client->dev, 
                             "Hareware FIFO overflow! Resetting.\n" );
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_USER_CTRL, 
                                  MPU6050_USER_CTRL_FIFO_EN_RESET);
        dev->ts_sync.fifo_overflow = true;
        goto unlock_out;
    }

    frames_count = fifo_count / MPU6050_SENSOR_DATA_SIZE;
    bytes_count  = frames_count * MPU6050_SENSOR_DATA_SIZE;

    msgs[0].addr  = client->addr,
    msgs[0].flags = 0,
    msgs[0].len   = 1,
    msgs[0].buf   = &dev->config.reg_fifo_rw,

    msgs[1].addr  = client->addr,
    msgs[1].flags = I2C_M_RD,
    msgs[1].len   = bytes_count,
    msgs[1].buf   = dev->hw_rx_buf,

    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs)) {
        dev_warn_ratelimited(&client->dev, 
                             "I2C transfer failed (err: %d)\n", 
                             ret);
        i2c_smbus_write_byte_data(client, 
                                  MPU6050_REG_USER_CTRL, 
                                  MPU6050_USER_CTRL_FIFO_EN_RESET);
        goto unlock_out;
    }

    mpu6050_calc_timestamps(&dev->ts_sync, frames_count);

    /* Insert the timestamp and put it into the kfifo. */

    for (i = 0; i < frames_count; i++) {
        raw_buf = &dev->hw_rx_buf[i * MPU6050_SENSOR_DATA_SIZE];

        frame.data.accel_x = (s16)get_unaligned_be16(&raw_buf[0]);
        frame.data.accel_y = (s16)get_unaligned_be16(&raw_buf[2]);
        frame.data.accel_z = (s16)get_unaligned_be16(&raw_buf[4]);
        frame.data.temp    = (s16)get_unaligned_be16(&raw_buf[6]);
        frame.data.gyro_x  = (s16)get_unaligned_be16(&raw_buf[8]);
        frame.data.gyro_y  = (s16)get_unaligned_be16(&raw_buf[10]);
        frame.data.gyro_z  = (s16)get_unaligned_be16(&raw_buf[12]);

        frame.timestamp = dev->ts_sync.last_ts - 
                          (u64)(frames_count - 1 - i) * 
                          dev->ts_sync.chip_period;

        if (kfifo_is_full(&dev->data_fifo)) { 
            struct mpu6050_frame dummy_frame;
            dev_warn_ratelimited(&client->dev, 
                                 "Kfifo is full, Drop old frame.\n");

            if(!kfifo_get(&dev->data_fifo, &dummy_frame))
                dev_warn_ratelimited(&client->dev, 
                                     "Failed to Drop old frame.\n");
        }

        kfifo_put(&dev->data_fifo, frame);
    }

    if (frames_count > 0)
        wake_up_interruptible(&dev->read_queue);
    goto unlock_out;

fail_read:
    dev_err(&client->dev, 
            "I2C persistent failure, initiating recovery. (err: %d)\n", 
            ret);
    dev->state = MPU_STATE_FAULT;
    mpu6050_safe_disable_irq_nosync(dev);
    mod_delayed_work(system_wq, &dev->monitor_work, 0);
    wake_up_interruptible(&dev->read_queue);

unlock_out:
    mutex_unlock(&dev->lock);

    return IRQ_HANDLED;
}

static int mpu6050_probe(struct i2c_client *client)
{
    struct mpu6050_dev *dev;
    int ret;

    dev_dbg(&client->dev, "Probing MPU6050\n");

    dev = kzalloc(sizeof(struct mpu6050_dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    mutex_lock(&minor_lock);
    if (mpu6050_minor_cnt >= MPU6050_MAX_DEVS) {
        mutex_unlock(&minor_lock);
        kfree(dev);
        return -ENOSPC;
    }

    dev->minor = mpu6050_minor_cnt++;
    mutex_unlock(&minor_lock);

    dev->devid = MKDEV(MAJOR(mpu6050_dev_base), dev->minor);
    dev->client = client;
    dev->state = MPU_STATE_INIT;
    mutex_init(&dev->lock);
    mutex_init(&dev->irq_ctrl.irq_lock);
    atomic_set(&dev->users, 0);
    atomic_set(&dev->exiting, 0);
    init_waitqueue_head(&dev->read_queue);
    INIT_DELAYED_WORK(&dev->monitor_work, mpu6050_monitor_fn);

    ret = kfifo_alloc(&dev->data_fifo, MPU6050_KFIFO_FRAMES, GFP_KERNEL);
    if (ret)    
        goto fail_kfifo;

    dev->hw_rx_buf = kzalloc(MPU6050_HW_FIFO_SIZE, GFP_KERNEL);
    if (!dev->hw_rx_buf) {
        ret = -ENOMEM;
        goto fail_buf;
    }

    if (client->irq <=0) {
        ret = -EINVAL;
        goto fail_irq;
    }

    irq_set_status_flags(client->irq, IRQ_NOAUTOEN);
    dev->irq_ctrl.enabled = false;
    ret = request_threaded_irq(client->irq,
                               mpu6050_irq_top_half,
                               mpu6050_irq_thread_fn,
                               IRQF_ONESHOT,
                               "mpu6050_irq",
                               dev);

    if (ret) {
        dev_err(&client->dev, 
                "Failed to request IRQ %d (err: %d)\n", 
                client->irq, 
                ret);
        goto fail_irq;
    }

    mpu6050_fill_shadow_config(dev);

    /*
	 * ARCHITECTURAL NOTE: Hardware initialization is intentionally deferred
	 * from the probe phase to support dynamic hot-plugging and bus recovery.
	 *
	 * Executing mpu6050_hw_init() during probe would trigger a fatal failure
	 * (-ENODEV/-ETIMEDOUT) if the sensor is physically detached at boot time,
	 * erroneously preventing the character device node from being registered.
	 *
	 * To guarantee robust hot-swap resilience, we bypass synchronous hardware
	 * validation here. Register provisioning and identity verification (WHO_AM_I)
	 * are offloaded to the open() lifecycle and the asynchronous monitoring
	 * state machine.
	 */

    cdev_init(&dev->cdev, &mpu6050_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devid, 1);
    if (ret)    
        goto fail_cdev_init;
    
    dev->device = device_create(mpu6050_class, NULL, 
                                dev->devid, NULL, 
                                "mpu6050_%d", dev->minor);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto fail_device;
    }

    i2c_set_clientdata(client, dev);
    dev_info(&client->dev, "MPU6050: probe ok\n");

    return 0;

fail_device:
    cdev_del(&dev->cdev);
fail_cdev_init:
    free_irq(client->irq, dev);
fail_irq:
    kfree(dev->hw_rx_buf);
fail_buf:
    kfifo_free(&dev->data_fifo);
fail_kfifo:
    mutex_lock(&minor_lock);
    mpu6050_minor_cnt--;
    mutex_unlock(&minor_lock);
    kfree(dev);

    return ret;
}

static void mpu6050_remove(struct i2c_client *client)
{
    struct mpu6050_dev *dev = i2c_get_clientdata(client);

    cancel_delayed_work_sync(&dev->monitor_work);

    mutex_lock(&dev->lock);
    i2c_smbus_write_byte_data(client, 
                              MPU6050_REG_PWR_MGMT_1, 
                              MPU6050_PWR_SLEEP);
    mutex_unlock(&dev->lock);

    device_destroy(mpu6050_class, dev->devid);
    cdev_del(&dev->cdev);

    free_irq(client->irq, dev);
    kfree(dev->hw_rx_buf);
    kfifo_free(&dev->data_fifo);

    mutex_lock(&minor_lock);
    mpu6050_minor_cnt--;
    mutex_unlock(&minor_lock);

    kfree(dev);
    
    dev_info(&client->dev, "MPU6050: remove ok\n");
}

static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "invensense,mpu6050" },
    { /* Sentienl */ }
};

static struct i2c_driver mpu6050_driver = {
    .driver = {
        .name = "mpu6050",
        .of_match_table = mpu6050_of_match,
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
};

//module_i2c_driver(mpu6050_driver);

static int __init mpu6050_drv_init(void)
{
    int ret;
    ret = alloc_chrdev_region(&mpu6050_dev_base, 0, 
                              MPU6050_MAX_DEVS, "mpu6050_base");
    if (ret < 0) return ret;

    mpu6050_class = class_create("mpu6050_class");
    if (IS_ERR(mpu6050_class)) {
        pr_err("MPU6050: failed to create class\n");
        unregister_chrdev_region(mpu6050_dev_base, MPU6050_MAX_DEVS);
        return PTR_ERR(mpu6050_class);
    }

    return i2c_add_driver(&mpu6050_driver);
}

static void __exit mpu6050_drv_exit(void)
{
    i2c_del_driver(&mpu6050_driver);
    class_destroy(mpu6050_class);
    unregister_chrdev_region(mpu6050_dev_base, MPU6050_MAX_DEVS);
}

module_init(mpu6050_drv_init);
module_exit(mpu6050_drv_exit);

MODULE_AUTHOR("JAX");
MODULE_DESCRIPTION("I2C Character Driver for MPU6050");
MODULE_LICENSE("GPL");