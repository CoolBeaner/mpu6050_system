import os
import matplotlib.pyplot as plt
import numpy as np

# ========================================================
# 🎯 配置中心
# ========================================================
NPY_FILE_PATH = "./clean_data/class_7_clean.npy"  # 👈 想检查哪一类就填哪个路径

def inspect_data():
    if not os.path.exists(NPY_FILE_PATH):
        print(f"❌ 找不到文件: {NPY_FILE_PATH}，请检查路径！")
        return

    # 1. 载入二进制矩阵 [N, 256, 6]
    data = np.load(NPY_FILE_PATH)
    num_samples, window_size, num_channels = data.shape
    
    print("=" * 60)
    print(f"📊 数据集全量阅兵开始！文件: {os.path.basename(NPY_FILE_PATH)}")
    print(f"📊 总样本数: {num_samples} 个 | 窗口大小: {window_size} 点 | 通道数: {num_channels} 轴")
    print("=" * 60)

    # ========================================================
    # 🌟 维度一：全量波形堆叠分析图（查看整体一致性）
    # ========================================================
    print("🔄 正在生成【全量重叠密度图】，请观察曲线是否抱团...")
    fig_stack, axes = plt.subplots(2, 1, figsize=(13, 7), sharex=True)
    
    colors_acc = ['red', 'green', 'blue']
    colors_gyro = ['darkred', 'darkgreen', 'darkblue']
    labels_acc = ['Acc X', 'Acc Y', 'Acc Z']
    labels_gyro = ['Gyro X', 'Gyro Y', 'Gyro Z']

    # 遍历所有样本，用半透明线全部强行重叠画在一起
    for i in range(num_samples):
        # 上半图：重叠画所有样本的加速度 3 轴
        for ch in range(3):
            lbl = labels_acc[ch] if i == 0 else "" # 只给第一个样本加图例，防止图例爆炸
            axes[0].plot(data[i, :, ch], color=colors_acc[ch], alpha=0.15, label=lbl)
            
        # 下半图：重叠画所有样本的陀螺仪 3 轴
        for ch in range(3):
            lbl = labels_gyro[ch] if i == 0 else ""
            axes[1].plot(data[i, :, ch+3], color=colors_gyro[ch], alpha=0.15, label=lbl)

    axes[0].set_title(f"All {num_samples} Samples Overlaid - Accelerometer Consistency Check", fontsize=12, fontweight='bold')
    axes[0].grid(True, linestyle=":", alpha=0.6)
    axes[0].legend(loc="upper right")
    
    axes[1].set_title("All Samples Overlaid - Gyroscope Consistency Check", fontsize=12, fontweight='bold')
    axes[1].grid(True, linestyle=":", alpha=0.6)
    axes[1].legend(loc="upper right")
    axes[1].set_xlabel("Time Points (256 pts)", fontsize=11)
    
    plt.tight_layout()
    print("💡 [提示] 查看完【整体重叠大图】后，请关闭该图片窗口，程序将自动进入【逐个细节轮巡模式】...")
    plt.show()

    # ========================================================
    # 🌟 维度二：逐个顺次轮巡模式（查看微观细节）
    # ========================================================
    print("\n📖 进入【逐个微观细节轮巡】模式...")
    for idx in range(num_samples):
        print(f"🔎 当前正在展示第 {idx + 1} / {num_samples} 个样本的独立特写...")
        
        fig_single, (ax_acc, ax_gyro) = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
        
        # 绘制单样本加速度
        ax_acc.plot(data[idx, :, 0], color='red', label='Acc X')
        ax_acc.plot(data[idx, :, 1], color='green', label='Acc Y')
        ax_acc.plot(data[idx, :, 2], color='blue', label='Acc Z')
        ax_acc.set_title(f"Sample Specimen Spec - Index: {idx} / {num_samples - 1}", fontsize=11, fontweight='bold')
        ax_acc.grid(True, linestyle="--", alpha=0.5)
        ax_acc.legend(loc="upper right")
        
        # 绘制单样本陀螺仪
        ax_gyro.plot(data[idx, :, 3], color='darkred', label='Gyro X')
        ax_gyro.plot(data[idx, :, 4], color='darkgreen', label='Gyro Y')
        ax_gyro.plot(data[idx, :, 5], color='darkblue', label='Gyro Z')
        ax_gyro.grid(True, linestyle="--", alpha=0.5)
        ax_gyro.legend(loc="upper right")
        ax_gyro.set_xlabel("Points")
        
        plt.tight_layout()
        plt.show(block=False)
        
        user_cmd = input("👉 【回车】看下一个样本 | 输入 [q] 退出查看: ").strip().lower()
        plt.close()
        
        if user_cmd == 'q':
            print("👋 已安全退出阅兵系统。")
            break
            
    print("🏁 全量样本审计完毕！")

if __name__ == "__main__":
    inspect_data()