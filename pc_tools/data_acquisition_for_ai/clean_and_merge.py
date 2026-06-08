import os
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.signal import find_peaks

# ========================================================
# 🎯 配置中心（请确保你的 CSV 文件在这个文件夹里）
# ========================================================
RAW_DATA_DIR = "./raw/invalid_signal/add"  # 👈 对应你截图里的动作文件夹
CLASS_LABEL = 0                             # 👈 动作对应的标签
WINDOW_SIZE = 256                    
PEAK_MULTIPLIER = 0.4                       # 自动找波峰 调低它（比如 0.4 或 0.3）能把矮波峰全部炸出来

CLEAN_DIR = "./clean_data/add"
os.makedirs(CLEAN_DIR, exist_ok=True)

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False  # 正常显示负号

def interactive_clean_and_append():
    all_approved_windows = []

    # 1. 检查目录是否存在
    if not os.path.exists(RAW_DATA_DIR):
        print(f"[错误] 找不到文件夹: {RAW_DATA_DIR}，请确保脚本和你的动作文件夹在同一目录下！")
        return

    csv_files = [f for f in os.listdir(RAW_DATA_DIR) if f.endswith(".csv")]
    if not csv_files:
        print(f"[警告] 在 【{RAW_DATA_DIR}】 文件夹下没有找到任何 .csv 文件！")
        return

    print(f" =====================================================")
    print(f" 全景大地图快门系统启动！目标目录: 【{RAW_DATA_DIR}】")
    print(f"=====================================================")

    for file_name in csv_files:
        file_path = os.path.join(RAW_DATA_DIR, file_name)
        try:
            df = pd.read_csv(file_path, header=None)
        except Exception as e:
            print(f"读取文件失败 {file_name}: {e}")
            continue

        if len(df) < WINDOW_SIZE:
            print(f"文件 {file_name} 太短，跳过")
            continue

        # 计算陀螺仪综合能量作为动作定位器
        gyro_energy = np.abs(df[3]) + np.abs(df[4]) + np.abs(df[5])
        dynamic_height = gyro_energy.mean() + PEAK_MULTIPLIER * gyro_energy.std()
        peaks, _ = find_peaks(gyro_energy, distance=80, height=dynamic_height)
        
        print(f"正在扫描文件: {file_name} | 总长: {len(df)}行 | 发现波峰: {len(peaks)}个")

        for idx, p in enumerate(peaks):
            start = p - (WINDOW_SIZE // 2)
            end = p + (WINDOW_SIZE // 2)

            if start < 0 or end > len(df):
                continue

            window_data = df.iloc[start:end, :6].values  

            # 画布渲染
            fig = plt.figure(figsize=(12, 7))
            
            # 屏幕 1：全局全景导航大图
            ax_global = plt.subplot2grid((3, 2), (0, 0), colspan=2)
            ax_global.plot(gyro_energy, color='gray', alpha=0.5, label='Total Energy')
            ax_global.axvspan(start, end, color='red', alpha=0.3, label='Current Window')
            ax_global.scatter(peaks, gyro_energy[peaks], color='blue', s=25, zorder=5)
            ax_global.scatter([p], [gyro_energy[p]], color='red', s=100, marker='*', zorder=6, label='Target')
            ax_global.set_title(f"Global Navigation - File: {file_name} (Index: {idx+1}/{len(peaks)}) | PRESS [Y] to Keep, [N] to Discard", fontsize=11, fontweight='bold')
            ax_global.set_xlim(0, len(df))
            ax_global.grid(True, linestyle=":", alpha=0.6)

            # 屏幕 2：局部加速度
            ax_loc_acc = plt.subplot2grid((3, 2), (1, 0), colspan=2)
            ax_loc_acc.plot(window_data[:, :3])
            ax_loc_acc.grid(True, linestyle="--", alpha=0.5)

            # 屏幕 3：局部陀螺仪
            ax_loc_gyro = plt.subplot2grid((3, 2), (2, 0), colspan=2)
            ax_loc_gyro.plot(window_data[:, 3:])
            ax_loc_gyro.grid(True, linestyle="--", alpha=0.5)
            plt.tight_layout()

            # 🔑 建立一个闭包变量，用来隔空传递你在图片上的键盘输入
            user_decision = ["n"]

            # 🔑 键盘事件回调函数：直接监听你在图片窗口上的敲击
            def on_key_press(event):
                if event.key in ['y', 'Y']:
                    user_decision[0] = "y"
                    plt.close() # 只要按下任意键，立刻关闭当前图，代码继续往下走
                elif event.key in ['n', 'N', 'escape']: 
                    user_decision[0] = "n"
                    plt.close()  #  只有是 N 或 Esc，才准关窗并跳过
                else:
                    # 按了其他键（或者中文输入法干扰），直接无视，等老哥重新输入
                    pass
            # 将键盘事件绑定到画布上
            fig.canvas.mpl_connect('key_press_event', on_key_press)
            
            # 💡 注意：这里必须改成 block=True，让程序静静等待窗口被按键关闭
            plt.show(block=True) 

            # 判定结果
            if user_decision[0] == "y":
                all_approved_windows.append(window_data)
                print(f"   [OK] 已暂存。目前累计有效样本: {len(all_approved_windows)} 个")
            else:
                print("   [SKIP] 杂波已跳过")
            plt.close()

    # 5. 存储追加流
    if all_approved_windows:
        new_data = np.array(all_approved_windows)
        clean_save_path = f"{CLEAN_DIR}/class_{CLASS_LABEL}_clean.npy"
        if os.path.exists(clean_save_path):
            old_data = np.load(clean_save_path)
            final_data = np.concatenate([old_data, new_data], axis=0)
            print(f"\n追加成功！【class_{CLASS_LABEL}_clean.npy】累计总数: {len(final_data)}")
        else:
            final_data = new_data
            print(f"\n新建黄金包成功！【class_{CLASS_LABEL}_clean.npy】样本数: {len(final_data)}")
        np.save(clean_save_path, final_data)
    else:
        print("\n❌ 结束：本次未收录任何样本。\n")

def export_final_dataset():
    """ 📦 当你把 10 个独立文件夹全部跑完 y 之后，把 main 里的 interactive_clean_and_append() 注释掉，
        解开 export_final_dataset() 的注释跑一次，直接生成 AI 总包 """
    print("\n开始全类别最终大融汇...")
    X_list = []
    y_list = []

    for file_name in os.listdir(CLEAN_DIR):
        if file_name.endswith(".npy") and "_clean" in file_name:
            label = int(file_name.split("_")[1])
            data = np.load(os.path.join(CLEAN_DIR, file_name))
            X_list.append(data)
            y_list.append(np.full((len(data),), label))

    if X_list:
        X_final = np.concatenate(X_list, axis=0)
        y_final = np.concatenate(y_list, axis=0)
        output_npz = "./test_dataset.npz"
        np.savez(output_npz, data=X_final, label=y_final)
        print(f"终大闭环！总母包生成成功 -> {output_npz}")
        print(f"最终总矩阵形状: {X_final.shape}")
    else:
        print("❌ 未能成功融汇，目录为空。")

# ========================================================
# 🔑 补上我罪大恶极漏掉的程序主入口！！！
# ========================================================
if __name__ == "__main__":
    # 执行流一：清洗单类文件夹
    interactive_clean_and_append()
    
    # 执行流二：全洗完后，注释掉上面那行，解开下面这行的注释跑一次，生成喂给 AI 的总包
    # export_final_dataset()