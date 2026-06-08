import matplotlib.pyplot as plt
import pandas as pd

# ==========================================
# 🎯 配置区域：在这里修改你的 CSV 文件名
# ==========================================
csv_filename = "./raw/raw_gesture_13.csv"
sampling_rate = 100  # 咱们抽稀后的 AI 采样率是 100Hz
# 1 zhengsanjiao
# 2 daosanjiao
# 3 wuqiongda
def plot_imu_data(filename):
    try:
        # 1. 读取没有表头的 CSV 文件，手动强行赋予 6 轴通道名称
        column_names = [
            "acc_x",
            "acc_y",
            "acc_z",
            "gyro_x",
            "gyro_y",
            "gyro_z",
        ]
        df = pd.read_csv(filename, header=None, names=column_names)

        # 2. 根据 100Hz 采样率，自动生成精确的时间轴（秒）
        total_points = len(df)
        time_axis = [t / sampling_rate for t in range(total_points)]

        print(f"📊 成功加载文件: {filename}")
        print(f"📈 数据总点数: {total_points} 点，总时长: {total_points / sampling_rate:.2f} 秒")

        # 3. 创建画布：2行1列的子图，共享X轴（时间轴），这样放大时可以同步联动
        fig, (ax_acc, ax_gyro) = plt.subplots(2, 1, sharex=True, figsize=(12, 7))

        # 4. 绘制上半部分：3 轴加速度计曲线
        ax_acc.plot(time_axis, df["acc_x"], label="Acc X", color="red", alpha=0.8)
        ax_acc.plot(time_axis, df["acc_y"], label="Acc Y", color="green", alpha=0.8)
        ax_acc.plot(time_axis, df["acc_z"], label="Acc Z", color="blue", alpha=0.8)
        ax_acc.set_title(f"IMU Magic Wand Waveform Analysis ({filename})", fontsize=14)
        ax_acc.set_ylabel("Acceleration (Raw/g)", fontsize=12)
        ax_acc.grid(True, linestyle="--", alpha=0.6)
        ax_acc.legend(loc="upper right")

        # 5. 绘制下半部分：3 轴陀螺仪曲线
        ax_gyro.plot(time_axis, df["gyro_x"], label="Gyro X", color="darkred", alpha=0.8)
        ax_gyro.plot(time_axis, df["gyro_y"], label="Gyro Y", color="darkgreen", alpha=0.8)
        ax_gyro.plot(time_axis, df["gyro_z"], label="Gyro Z", color="darkblue", alpha=0.8)
        ax_gyro.set_xlabel("Time (Seconds)", fontsize=12)
        ax_gyro.set_ylabel("Angular Velocity (Raw/rad/s)", fontsize=12)
        ax_gyro.grid(True, linestyle="--", alpha=0.6)
        ax_gyro.legend(loc="upper right")

        # 6. 自动调整布局防重叠，并展现璀璨的曲线
        plt.tight_layout()
        print("💡 [提示] 弹出的窗口左下角有放大镜工具，可以框选局部放大观察哦！")
        plt.show()

    except Exception as e:
        print(f"❌ 读取或绘制失败，请检查文件名或数据格式！错误原因: {e}")


if __name__ == "__main__":
    plot_imu_data(csv_filename)