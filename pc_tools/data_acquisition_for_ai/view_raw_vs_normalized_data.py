import os
import numpy as np
import matplotlib.pyplot as plt

# ========================================================
# 配置中心
# ========================================================
CLEAN_DIR = "./clean_data"
FIFO_SIZE = 256          
GESTURE_CHANNELS = 6    
SAMPLES_TO_SHOW = 1     # 抽 1 组，拒绝重叠干扰，画面极致清爽

plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'SimSun'] 
plt.rcParams['axes.unicode_minus'] = False

def align_perfect_scale(X):
    """
    X 形状: [N, 256, 6]
    完美兼顾：去重力零偏 + 强力保留物理比例 + 压制到 [-1, 1]
    """
    # 1. 减去当前窗口的均值
    X_centered = X - np.mean(X, axis=1, keepdims=True)
    
    # 2. 全局固定比例缩放（老哥的黄金比例）
    X_centered[:, :, :3] = X_centered[:, :, :3] / 1.5  # 加速度计除以 1.5
    X_centered[:, :, 3:] = X_centered[:, :, 3:] / 90.0   # 陀螺仪除以 90.0
    
    return X_centered

def main():
    # 🎯 工业级大画布：8个类别，每个类别占据一整个大行
    # 每个类别内部切成 2x2 = 4 个子图：
    # [左上: 原始Acc]  [右上: 处理后Acc]
    # [左下: 原始Gyro] [右下: 处理后Gyro]
    fig = plt.figure(figsize=(18, 32))
    
    gesture_names = [
        "Class 0: NONE (杂波/手抖/喝水)",
        "Class 1: CIRCLE (正圈)",
        "Class 2: INV_CIRCLE (逆圈)",
        "Class 3: EQU_TRIANGLE (正三角)",
        "Class 4: INV_TRIANGLE (逆三角)",
        "Class 5: LINE (横线)",
        "Class 6: FLASH (闪电)",
        "Class 7: INFINITY (无穷)"
    ]
    
    print(f"正在提取数据并生成【物理量级对齐】终极对照大图...")
    
    # 使用 GridSpec 强行划分网格，每个手势给一个独立大方块
    outer_grid = fig.add_gridspec(8, 1, hspace=0.4)
    
    for label in range(8):
        file_name = f"class_{label}_clean.npy"
        file_path = os.path.join(CLEAN_DIR, file_name)
        
        # 针对当前类别，切分 2x2 的子图阵列
        inner_grid = outer_grid[label].subgridspec(2, 2, hspace=0.15, wspace=0.15)
        
        ax_raw_acc  = fig.add_subplot(inner_grid[0, 0]) # 左上：原始 Acc
        ax_raw_gyro = fig.add_subplot(inner_grid[1, 0], sharex=ax_raw_acc) # 左下：原始 Gyro
        
        ax_norm_acc = fig.add_subplot(inner_grid[0, 1], sharex=ax_raw_acc) # 右上：处理后 Acc
        ax_norm_gyro = fig.add_subplot(inner_grid[1, 1], sharex=ax_raw_acc) # 右下：处理后 Gyro
        
        if os.path.exists(file_path):
            data = np.load(file_path)
            total_samples = len(data)
            random_idx = np.random.choice(total_samples, SAMPLES_TO_SHOW, replace=False)[0]
            
            sample_raw = data[random_idx] # [256, 6]
            sample_norm = align_perfect_scale(data[random_idx:random_idx+1, :, :])[0] # [256, 6]
            
            # ----------------------------------------------------
            # 1. 绘制左列：原始物理数据（分立双通道，再也压不扁）
            # ----------------------------------------------------
            # 原始加速度（Acc X, Y, Z）
            ax_raw_acc.plot(sample_raw[:, 0], color='red', linewidth=1.2, label='Acc X')
            ax_raw_acc.plot(sample_raw[:, 1], color='orange', linewidth=1.2, label='Acc Y')
            ax_raw_acc.plot(sample_raw[:, 2], color='gold', linewidth=1.2, label='Acc Z')
            
            # 原始陀螺仪（Gyro X, Y, Z）
            ax_raw_gyro.plot(sample_raw[:, 3], color='blue', linewidth=1.2, label='Gyro X')
            ax_raw_gyro.plot(sample_raw[:, 4], color='cyan', linewidth=1.2, label='Gyro Y')
            ax_raw_gyro.plot(sample_raw[:, 5], color='purple', linewidth=1.2, label='Gyro Z')
            
            # ----------------------------------------------------
            # 2. 绘制右列：处理后的完美缩放数据（锁死 Y 轴，看清量级是否同级）
            # ----------------------------------------------------
            # 处理后的加速度
            ax_norm_acc.plot(sample_norm[:, 0], color='red', linewidth=1.2)
            ax_norm_acc.plot(sample_norm[:, 1], color='orange', linewidth=1.2)
            ax_norm_acc.plot(sample_norm[:, 2], color='gold', linewidth=1.2)
            
            # 处理后的陀螺仪
            ax_norm_gyro.plot(sample_norm[:, 3], color='blue', linewidth=1.2)
            ax_norm_gyro.plot(sample_norm[:, 4], color='cyan', linewidth=1.2)
            ax_norm_gyro.plot(sample_norm[:, 5], color='purple', linewidth=1.2)
            
            # ========================================================
            # 🎛️ 终极视觉微调：加标签、锁死右边Y轴
            # ========================================================
            # 主标题（只加在顶部的 Acc 子图上）
            ax_raw_acc.set_title(f"{gesture_names[label]} [RAW 物理值]", fontsize=12, fontweight='bold', color='#1a1a1a')
            ax_norm_acc.set_title(f"{gesture_names[label]} [PREPROCESS 物理对齐]", fontsize=12, fontweight='bold', color='#0066cc')
            
            # 设置 Y 轴标签
            ax_raw_acc.set_ylabel("Acc (g)", fontsize=9, fontweight='bold')
            ax_raw_gyro.set_ylabel("Gyro (°/s)", fontsize=9, fontweight='bold')
            ax_norm_acc.set_ylabel("Scaled Acc", fontsize=9, color='#0066cc')
            ax_norm_gyro.set_ylabel("Scaled Gyro", fontsize=9, color='#0066cc')
            
            # 🔓 左边原始图让它自动缩放，看清真实细节
            ax_raw_acc.grid(True, linestyle=':', alpha=0.5)
            ax_raw_gyro.grid(True, linestyle=':', alpha=0.5)
            
            # 🔒 右边处理后的子图：自动量程缩放
            ax_norm_acc.grid(True, linestyle='--', color='#b3d9ff', alpha=0.4)
            ax_norm_gyro.grid(True, linestyle='--', color='#b3d9ff', alpha=0.4)
            
            ax_norm_acc.autoscale(enable=True, axis='y', tight=False)
            ax_norm_gyro.autoscale(enable=True, axis='y', tight=False)
            
            # 只在最上面一行加 Legend，防止画面太乱
            if label == 0:
                ax_raw_acc.legend(loc='upper right', fontsize=8, edgecolor='none', facecolor='none')
                ax_raw_gyro.legend(loc='upper right', fontsize=8, edgecolor='none', facecolor='none')
                
            # 隐藏不必要的 X 轴刻度，只保留最底部的
            if label < 7:
                plt.setp(ax_raw_acc.get_xticklabels(), visible=False)
                plt.setp(ax_norm_acc.get_xticklabels(), visible=False)
        else:
            ax_raw_acc.text(0.5, 0.5, f"Missing {file_name}", transform=ax_raw_acc.transAxes, ha='center')

    fig.text(0.5, 0.01, "Time Steps (256 滑动窗口点数)", ha='center', fontsize=12, fontweight='bold')
    
    output_image = "./raw_vs_norm/raw_vs_norm_comparison.png"
    plt.savefig(output_image, dpi=130, bbox_inches='tight')
    print(f"\n✨ 真正的【物理量级对撞对照表】已击穿！请查看新图 -> {output_image}")
    
if __name__ == "__main__":
    main()