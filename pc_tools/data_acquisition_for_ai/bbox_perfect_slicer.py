import os
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.signal import find_peaks

# ========================================================
# 🎯 动态视界两阶段清算配平中心（纯键盘盲打完全体）
# ========================================================
RAW_DATA_DIR = "./infinity_signal"  # 👈 当前清洗的动作文件夹
CLASS_LABEL = 7                     # 👈 对应 AI 标签
WINDOW_SIZE = 256                   # 256点口袋规格
TARGET_COUNT = 500                  # 配平总目标数

CLEAN_DIR = "./clean_data"
os.makedirs(CLEAN_DIR, exist_ok=True)

# 强行治好 Matplotlib 中文盲
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False 

def interactive_two_stage_slicer():
    # 🪐 终极全局大池账本
    # 存储结构: (df_dataframe, 初始左S, 初始右E, 标注左极点S_min, 标注右极点S_max, file_name)
    approved_zones_pool = []

    if not os.path.exists(RAW_DATA_DIR):
        print(f"❌ 找不到文件夹: {RAW_DATA_DIR}")
        return
    csv_files = [f for f in os.listdir(RAW_DATA_DIR) if f.endswith(".csv")]
    if not csv_files:
        print(f"⚠️ 在 【{RAW_DATA_DIR}】 下没有找到任何 .csv 文件！")
        return

    print(" =====================================================")
    print(" 终极两阶段状态机系统启动！全程焦点锁图，禁绝终端交互！")
    print(" 【第一阶段-验票】: [Y]能用，记录初始边界并解锁滚动 / [N]废波直接丢弃")
    print(" 【第二阶段-圈地】: [A]/[D]滚动视界 | [1]锁左极点 | [2]锁右极点 | [Y]最终存盘")
    print(" =====================================================")

    for file_name in csv_files:
        file_path = os.path.join(RAW_DATA_DIR, file_name)
        try:
            df = pd.read_csv(file_path, header=None)
        except Exception:
            continue

        if len(df) < WINDOW_SIZE + 40:
            continue

        # 初步波峰位置侦测
        gyro_energy = np.abs(df[3]) + np.abs(df[4]) + np.abs(df[5])
        dynamic_height = gyro_energy.mean() + 1.8 * gyro_energy.std()
        peaks, _ = find_peaks(gyro_energy, distance=80, height=dynamic_height)

        for idx, p in enumerate(peaks):
            # 状态机：初始进入“CONFIRM”状态
            state = ["CONFIRM"] 
            
            # 窗口实时起始点 S
            current_S = [max(0, p - WINDOW_SIZE // 2)]
            
            # 账本核心四大参数（闭包变量）
            initial_L = [None]
            initial_R = [None]
            s_min_record = [None]
            s_max_record = [None]
            
            user_action = ["none"]

            # 经典豪华三连屏布局
            fig = plt.figure(figsize=(13, 8))
            ax_global = plt.subplot2grid((3, 1), (0, 0))
            ax_loc_acc = plt.subplot2grid((3, 1), (1, 0))
            ax_loc_gyro = plt.subplot2grid((3, 1), (2, 0))

            def redraw():
                ax_global.clear()
                ax_loc_acc.clear()
                ax_loc_gyro.clear()
                
                s = current_S[0]
                e = s + WINDOW_SIZE
                
                # 动态宽视野，前后多看 150 个点排查前后重叠
                view_start = max(0, s - 150)
                view_end = min(len(df), e + 150)
                
                view_df = df.iloc[view_start:view_end]
                view_idx = view_df.index.values
                
                # 1. 全局全景大地图
                ax_global.plot(gyro_energy, color='gray', alpha=0.4, label='全局动作能量流')
                ax_global.scatter(peaks, gyro_energy[peaks], color='blue', s=20)
                ax_global.scatter([p], [gyro_energy[p]], color='red', marker='*', s=100)
                ax_global.axvspan(s, e, color='red', alpha=0.25, label='当前 256点 快门框')
                
                if s_min_record[0] is not None:
                    ax_global.axvline(s_min_record[0], color='gold', linestyle='-', linewidth=2)
                if s_max_record[0] is not None:
                    ax_global.axvline(s_max_record[0] + WINDOW_SIZE, color='limegreen', linestyle='-', linewidth=2)
                ax_global.set_xlim(0, len(df))
                ax_global.grid(True, linestyle=':', alpha=0.5)
                
                # 2. 局部加速度（纯实线加粗，反向联动滚动）
                ax_loc_acc.plot(view_idx, view_df[0], color='red', linewidth=1.5, label='Acc X')
                ax_loc_acc.plot(view_idx, view_df[1], color='green', linewidth=1.5, label='Acc Y')
                ax_loc_acc.plot(view_idx, view_df[2], color='blue', linewidth=1.5, label='Acc Z')
                ax_loc_acc.axvspan(s, e, color='red', alpha=0.12)
                if s_min_record[0] is not None:
                    ax_loc_acc.axvline(s_min_record[0], color='gold', linestyle='-', linewidth=2.5, label=f'左滑极限起点 ({s_min_record[0]})')
                if s_max_record[0] is not None:
                    ax_loc_acc.axvline(s_max_record[0] + WINDOW_SIZE, color='limegreen', linestyle='-', linewidth=2.5, label=f'右滑极限终点 ({s_max_record[0] + WINDOW_SIZE})')
                ax_loc_acc.set_xlim(view_start, view_end)
                ax_loc_acc.grid(True, linestyle='--', alpha=0.5)
                ax_loc_acc.legend(loc='upper right')
                
                # 3. 局部陀螺仪（纯实线加粗，反向联动滚动）
                ax_loc_gyro.plot(view_idx, view_df[3], color='darkorange', linewidth=1.5, label='Gyro X')
                ax_loc_gyro.plot(view_idx, view_df[4], color='purple', linewidth=1.5, label='Gyro Y')
                ax_loc_gyro.plot(view_idx, view_df[5], color='teal', linewidth=1.5, label='Gyro Z')
                ax_loc_gyro.axvspan(s, e, color='red', alpha=0.12)
                if s_min_record[0] is not None:
                    ax_loc_gyro.axvline(s_min_record[0], color='gold', linestyle='-', linewidth=2.5)
                if s_max_record[0] is not None:
                    ax_loc_gyro.axvline(s_max_record[0] + WINDOW_SIZE, color='limegreen', linestyle='-', linewidth=2.5)
                ax_loc_gyro.set_xlim(view_start, view_end)
                ax_loc_gyro.grid(True, linestyle='--', alpha=0.5)
                ax_loc_gyro.legend(loc='upper right')
                
                # 💡 核心修复：剔除所有彩虹表情包，换成干净的纯文本
                title_str = f"文件: {file_name} ({idx+1}/{len(peaks)}) | 状态: [{state[0]}]\n"
                if state[0] == "CONFIRM":
                    title_str += " -> 审查：此波形能否录用？ 能用请按 [Y] 确认 | 废波/断残请按 [N] 跳过"
                    ax_global.set_xlabel("[阶段一] [Y] 能用并锁定初始位置 | [N] 直接丢弃换下一张", color='blue', fontsize=12, fontweight='bold')
                elif state[0] == "MARKING":
                    title_str += f" -> 初始左: {initial_L[0]}, 初始右: {initial_R[0]} | [左极点]: {s_min_record[0]} | [右极点]: {s_max_record[0]}"
                    ax_global.set_xlabel("[阶段二] [A]/[D] 滚动视界排查重叠 | [1] 锁左极点 | [2] 锁右极点 | [Y] 最终大完结存盘", color='darkgreen', fontsize=12, fontweight='bold')
                
                ax_global.set_title(title_str, fontsize=11, fontweight='bold')
                plt.tight_layout()
                fig.canvas.draw()
            redraw()

            def on_key(event):
                key = event.key
                
                # --- 🪐 第一阶段：确认波形质量 ---
                if state[0] == "CONFIRM":
                    if key in ['y', 'Y']:
                        # 核心动作：瞬间吃下此刻默认红框的绝对物理位置
                        initial_L[0] = current_S[0]
                        initial_R[0] = current_S[0] + WINDOW_SIZE
                        print(f"   [波形录用] 成功封存初始参数 -> 初始左: {initial_L[0]}, 初始右: {initial_R[0]}")
                        # 状态无缝推向阶段二
                        state[0] = "MARKING"
                        redraw()
                    elif key in ['n', 'N', 'escape']:
                        user_action[0] = "skip"
                        plt.close()
                
                # --- 🪐 第二阶段：允许滑动圈定极点 ---
                elif state[0] == "MARKING":
                    if key in ['a', 'A']:
                        current_S[0] = max(0, current_S[0] - 2)
                        redraw()
                    elif key in ['d', 'D']:
                        current_S[0] = min(len(df) - WINDOW_SIZE, current_S[0] + 2)
                        redraw()
                    elif key == '1':
                        s_min_record[0] = current_S[0]
                        print(f"   成功钉下标注左窗口极点位置: {current_S[0]}")
                        redraw()
                    elif key == '2':
                        s_max_record[0] = current_S[0]
                        print(f"   成功钉下标注右窗口极点位置: {current_S[0]}")
                        redraw()
                    elif key in ['y', 'Y']:
                        if s_min_record[0] is None or s_max_record[0] is None:
                            print(" 警告：你还没有用 [1] 和 [2] 键锁死标注的左右极点！无法存盘。")
                            return
                        if s_min_record[0] > s_max_record[0]:
                            print(" 错误：标注左极点起点不能大于右极点起点！请重新对齐。")
                            return
                        user_action[0] = "save"
                        plt.close()
                    elif key in ['n', 'N']:
                        # 阶段二反悔丢弃流
                        user_action[0] = "skip"
                        plt.close()

            fig.canvas.mpl_connect('key_press_event', on_key)
            plt.show(block=True)

            if user_action[0] == "save":
                # 四大参数打包进大池账本，功德圆满
                approved_zones_pool.append((df, initial_L[0], initial_R[0], s_min_record[0], s_max_record[0], file_name))
                print(f" [当前波形大完结] 初始:[{initial_L[0]}->{initial_R[0]}], 标注极点:[{s_min_record[0]}->{s_max_record[0]}]. 大池账本累计: {len(approved_zones_pool)}个")
            else:
                print("   [SKIP] 废弃/跳过当前波形")

    # ========================================================
    # 🪐 全局大清算阶段：整个文件夹所有文件筛选完，开始倒推最优步长
    # ========================================================
    num_seeds = len(approved_zones_pool)
    if num_seeds == 0:
        print("\n❌ 结束：整个文件夹未成功收录任何有效区间，退出。\n")
        return

    print(f"\n-----------------------------------------------------")
    print(f"进入【全局大池清算阶段】！正在依据标注极点倒推最优步长...")
    print(f"------------------------------------------------------")

    chosen_stride = 1
    is_satisfied = False

    # 严格死卡：从大步长 5 开始向下逼近清算
    for stride in [5, 4, 3, 2, 1]:
        total_unique_windows = 0
        for _, _, _, s_min, s_max, _ in approved_zones_pool:
            total_unique_windows += len(range(s_min, s_max + 1, stride))
        
        print(f" 📊 测试步长(Stride) = {stride} 个点 -> 整个大池累计可产出: {total_unique_windows} 个原生切片")
        
        if total_unique_windows >= TARGET_COUNT:
            chosen_stride = stride
            is_satisfied = True
            break  # 一旦满足 500 个，立刻锁定最大步长，决不再向下压榨

    print(f"\n终审判决：全自动锁定的切片最优步长为: 【 {chosen_stride} 】 个点！")

    # 执行纯天然切片组装
    all_generated_windows = []
    for df_data, init_l, init_r, s_min, s_max, f_name in approved_zones_pool:
        for s in range(s_min, s_max + 1, chosen_stride):
            window = df_data.iloc[s:s+WINDOW_SIZE, :6].values
            all_generated_windows.append(window)

    final_matrix = np.array(all_generated_windows)

    # 严格配平控制
    if is_satisfied:
        # 如果超量了，随机打乱抽取前 500 个，保证多大类之间绝对公正不偏科
        shuffle_idx = np.random.permutation(len(final_matrix))[:TARGET_COUNT]
        final_matrix = final_matrix[shuffle_idx]
        print(f"完美爆发配平！从全量去重池中随机精选抽取了 {TARGET_COUNT} 个黄金原生样本。")
    else:
        print(f"\n⚠️ ⚠️ ⚠️ 【核心警告】 ⚠️ ⚠️ ⚠️")
        print(f"即便把步长榨干到 1 个点，当前录用池产出的裸矩阵也只有 {len(final_matrix)} 个！")
        print(f"未达到 {TARGET_COUNT} 个的目标上限！请在脚本运行结束后，去板子上再多录制点数据扔进来！\n")

    # 最终存盘
    clean_save_path = f"{CLEAN_DIR}/class_{CLASS_LABEL}_clean.npy"
    np.save(clean_save_path, final_matrix)
    print(f"【全天然无阶跃大闭环】 最终保存路径 -> {clean_save_path}")
    print(f"最终输出二进制矩阵形状: {final_matrix.shape}\n")

if __name__ == "__main__":
    interactive_two_stage_slicer()