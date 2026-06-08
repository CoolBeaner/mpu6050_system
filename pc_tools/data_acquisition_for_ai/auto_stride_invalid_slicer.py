import os
import numpy as np
import pandas as pd

# ========================================================
# 🎯 杂波大池全自动步长清算控制中心
# ========================================================
RAW_DATA_DIR = "./invalid_signal"   # 👈 你的杂波长文件文件夹
CLASS_LABEL = 0                     # 👈 杂波死锁标签 0
WINDOW_SIZE = 256                   # 256点口袋规格
TARGET_COUNT = 500                  # 严格配平总目标

CLEAN_DIR = "./clean_data"
os.makedirs(CLEAN_DIR, exist_ok=True)

def auto_stride_slice_invalid():
    if not os.path.exists(RAW_DATA_DIR):
        print(f"❌ 找不到文件夹: {RAW_DATA_DIR}，请检查路径！")
        return
        
    csv_files = [f for f in os.listdir(RAW_DATA_DIR) if f.endswith(".csv")]
    if not csv_files:
        print(f"⚠️ 在 【{RAW_DATA_DIR}】 文件夹下没有找到任何 .csv 杂波文件！")
        return

    # 🪐 步骤 1：全量预加载，盘点最大原生总容量
    loaded_dfs = []
    total_max_capacity_stride_1 = 0
    
    for file_name in csv_files:
        file_path = os.path.join(RAW_DATA_DIR, file_name)
        try:
            df = pd.read_csv(file_path, header=None)
            if len(df) >= WINDOW_SIZE:
                loaded_dfs.append((df, file_name))
                # Stride = 1 时，单个文件最大可切出的不重复窗口数
                total_max_capacity_stride_1 += (len(df) - WINDOW_SIZE + 1)
        except Exception as e:
            print(f"⚠️ 读取文件失败 {file_name}: {e}")

    if not loaded_dfs:
        print(f"❌ 大池盘点失败：没有找到任何长度大于 {WINDOW_SIZE} 点的有效 CSV 文件！")
        return

    print("🚀 =====================================================")
    print("🚀 杂波大池全自动步长清算系统启动！")
    print(f"🚀 盘点总账：在 Stride=1 极限压榨下，全文件夹累计总样本库容量为: 【 {total_max_capacity_stride_1} 】 个")
    print("🚀 =====================================================")

    # 🪐 步骤 2：依据老哥算法，倒推算能切出 500 个样本的最大步长
    chosen_stride = 1
    is_satisfied = False

    if total_max_capacity_stride_1 < TARGET_COUNT:
        # 如果极限容量连 500 都不够，最大步长只能被迫锁死为 1
        chosen_stride = 1
        is_satisfied = False
    else:
        # 如果够用，我们从数学理论上的最大可能步长开始向下试探，寻找临界点
        theoretical_max_stride = (total_max_capacity_stride_1 // TARGET_COUNT) + 5
        
        for stride in range(theoretical_max_stride, 0, -1):
            total_windows_at_stride = 0
            for df_data, _ in loaded_dfs:
                # 计算当前大文件在当前测试步长下能切出多少个窗口
                total_windows_at_stride += ((len(df_data) - WINDOW_SIZE) // stride + 1)
            
            # 只要第一次跨过 500 大关，说明这就是能满足条件的最大步长！
            if total_windows_at_stride >= TARGET_COUNT:
                chosen_stride = stride
                is_satisfied = True
                print(f"📊 步长推演测试：当 Stride = {stride} 时，大池预计可产出 {total_windows_at_stride} 个样本")
                break

    print(f"\n🏁 终审判决：为达成 {TARGET_COUNT} 目标，系统锁定的最大最优切片步长为: 【 {chosen_stride} 】 个点！")

    # 🪐 步骤 3：开始按照计算好的最大步长执行纯天然切片
    print(f"✂️ 正在以 Stride = {chosen_stride} 的空间跨度进行全自动无重复流水线切片...")
    all_generated_windows = []
    
    for df_data, file_name in loaded_dfs:
        file_cut_count = 0
        start = 0
        while start + WINDOW_SIZE <= len(df_data):
            window = df_data.iloc[start:start+WINDOW_SIZE, :6].values
            all_generated_windows.append(window)
            file_cut_count += 1
            start += chosen_stride
        print(f"   🤖 文件: {file_name} | 成功切出: {file_cut_count} 个原生杂波")

    final_matrix = np.array(all_generated_windows)

    # 🪐 步骤 4：多文件总容量严格打乱配平
    if is_satisfied:
        # 如果总数超了，随机大洗牌，精准截取前 500 个，消灭时序依赖
        shuffle_idx = np.random.permutation(len(final_matrix))[:TARGET_COUNT]
        final_matrix = final_matrix[shuffle_idx]
        print(f"✅ 完美爆发配平！已从总去重池中随机抽选了 {TARGET_COUNT} 个黄金原生杂波样本。")
    else:
        # 如果把步长缩到 1 都不够 500，有多少吐多少，并发出硬核警告
        print(f"\n⚠️ ⚠️ ⚠️ 【核心警告】 ⚠️ ⚠️ ⚠️")
        print(f"老哥！即便把步长压缩到 1 个点，当前杂波大池里榨出来的最大总数也只有 {len(final_matrix)} 个！")
        print(f"未达到 {TARGET_COUNT} 个的目标配平上限！请去板子上再多录制 1~2 分钟的杂波长文件扔进来！\n")

    # 最终存盘
    clean_save_path = f"{CLEAN_DIR}/class_{CLASS_LABEL}_clean.npy"
    np.save(clean_save_path, final_matrix)
    print(f"💾 🎉 【全自动杂波大闭环】 最终矩阵已封存 -> {clean_save_path}")
    print(f"📊 输出二进制矩阵最终形状: {final_matrix.shape}\n")

if __name__ == "__main__":
    auto_stride_slice_invalid()