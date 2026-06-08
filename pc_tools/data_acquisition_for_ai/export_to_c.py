import torch
import numpy as np
# 已经锁死带 1d 的正确名字
from train_gesture_1dcnn import Gesture1DCNN 

def export_fused_model_to_c():
    device = torch.device('cpu')
    # 1. 初始化网络并载入你刚刚炼好的神级权重
    model = Gesture1DCNN(in_channels=6, num_classes=8)
    model.load_state_dict(torch.load("./mode/gesture_model.pth", map_location=device))
    model.eval()

    print("正在进行工业级 Conv + BN 算子融合...")

    # 2. 将 BatchNorm 的均值、方差、缩放系数直接熔断进卷积层的权重和偏置中
    def fuse_conv_bn(conv, bn):
        w = conv.weight.detach().numpy()
        b = conv.bias.detach().numpy() if conv.bias is not None else np.zeros(conv.out_channels)
        
        rm = bn.running_mean.detach().numpy()
        rv = bn.running_var.detach().numpy()
        gamma = bn.weight.detach().numpy()
        beta = bn.bias.detach().numpy()
        eps = bn.eps

        scale = gamma / np.sqrt(rv + eps)
        w_fused = w * scale[:, None, None]
        b_fused = (b - rm) * scale + beta
        return w_fused, b_fused

    # 融合三层卷积
    w1, b1 = fuse_conv_bn(model.conv1[0], model.conv1[1])
    w2, b2 = fuse_conv_bn(model.conv2[0], model.conv2[1])
    w3, b3 = fuse_conv_bn(model.conv3[0], model.conv3[1])

    # 提取全连接层参数
    fc1_w = model.fc[0].weight.detach().numpy()
    fc1_b = model.fc[0].bias.detach().numpy()
    fc2_w = model.fc[3].weight.detach().numpy()
    fc2_b = model.fc[3].bias.detach().numpy()

    # 3. 开始写出到 C 语言头文件
    with open("./mode_to_board/gesture_model_weights.h", "w", encoding="utf-8") as f:
        f.write("/* ======================================================== */\n")
        f.write("/*    全志 H3 魔杖专属：纯 C 语言 1D-CNN 融合权重头文件     */\n")
        f.write("/* ======================================================== */\n\n")
        f.write("#ifndef GESTURE_MODEL_WEIGHTS_H\n#define GESTURE_MODEL_WEIGHTS_H\n\n")

        # 优化：3D数组由于最内层只有3或5个点，本身就很短，保持原样
        def write_array_3d(name, arr):
            f.write(f"static const float {name}[{arr.shape[0]}][{arr.shape[1]}][{arr.shape[2]}] = {{\n")
            for i in range(arr.shape[0]):
                f.write("    {\n")
                for j in range(arr.shape[1]):
                    line = ", ".join([f"{x:.7f}f" for x in arr[i, j]])
                    f.write(f"        {{ {line} }},\n")
                f.write("    },\n")
            f.write("};\n\n")

        # 核心重构：2D全连接层大矩阵（FC1_W 是 256 行，每行 4096 个点）
        def write_array_2d(name, arr):
            f.write(f"static const float {name}[{arr.shape[0]}][{arr.shape[1]}] = {{\n")
            for i in range(arr.shape[0]):
                f.write("    {\n")
                # 先把这一行的 4096 个浮点数全部格式化出来
                row_elements = [f"{x:.7f}f" for x in arr[i]]
                
                # 绝杀：每 10 个数字强行切成一排，单独换行写入！
                chunk_size = 10
                for k in range(0, len(row_elements), chunk_size):
                    chunk = row_elements[k:k+chunk_size]
                    f.write("        " + ", ".join(chunk) + ",\n")
                    
                f.write("    },\n")
            f.write("};\n\n")

        # 核心重构：1D 偏置数组同样采取每 10 个数字自动换行的优雅排版
        def write_array_1d(name, arr):
            f.write(f"static const float {name}[{arr.shape[0]}] = {{\n")
            elements = [f"{x:.7f}f" for x in arr]
            
            chunk_size = 10
            for k in range(0, len(elements), chunk_size):
                chunk = elements[k:k+chunk_size]
                f.write("    " + ", ".join(chunk) + ",\n")
                
            f.write("};\n\n")

        # 吐出所有 C 格式数组
        write_array_3d("CONV1_W", w1) # [32, 6, 5]
        write_array_1d("CONV1_B", b1) # [32]
        
        write_array_3d("CONV2_W", w2) # [64, 32, 5]
        write_array_1d("CONV2_B", b2) # [64]
        
        write_array_3d("CONV3_W", w3) # [128, 64, 3]
        write_array_1d("CONV3_B", b3) # [128]
        
        write_array_2d("FC1_W", fc1_w)   # [256, 4096] 👈 就是这个家伙！
        write_array_1d("FC1_B", fc1_b)   # [256]
        
        write_array_2d("FC2_W", fc2_w)   # [8, 256]
        write_array_1d("FC2_B", fc2_b)   # [8]

        f.write("#endif // GESTURE_MODEL_WEIGHTS_H\n")

    print("[完美排版] 转换大功告成！已全自动对长文本换行，生成纯 C 头文件: gesture_model_weights.h ")

if __name__ == "__main__":
    export_fused_model_to_c()