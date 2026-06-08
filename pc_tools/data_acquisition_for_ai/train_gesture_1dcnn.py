import os
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

# ========================================================
# 🎯 超参数与配置中心
# ========================================================
CLEAN_DIR = "./clean_data"
TRAIN_DIR = "./clean_data"
ADD_DIR   =  "add"           
TEST_DIR = "./clean_data/for_test"
BATCH_SIZE = 64
EPOCHS = 40
LEARNING_RATE = 0.001
FIFO_SIZE = 256          # 严格对齐全志 H3 的滑窗尺寸
GESTURE_CHANNELS = 6    # 6 轴数据
NUM_CLASSES = 8         # 7 个标准动作 + 1 个背景杂波 = 8 类

# 设置随机种子，保证每次运行结果可复现
torch.manual_seed(42)
np.random.seed(42)

# ========================================================
# 🛠️ 核心：像素级对齐全志 H3 C 语言的标准化函数
# ========================================================
def align_z_score_normalize(X):
    """
    X 的输入形状: [N, 256, 6] (样本数, 时间步, 6轴)
    严格复刻 C 语言逻辑：
    每个样本的每个通道独立计算 256 个点内的 mean 和 std_dev
    """
    # 1. 计算当前通道在 256 个点里的均值 (对应 C 的第一个循环)
    mean = np.mean(X, axis=1, keepdims=True)  # 形状: [N, 1, 6]
    
    # 2. 计算方差 (对应 C 的第二个循环，ddof=0 代表分母除以 FIFO_SIZE)
    variance = np.var(X, axis=1, keepdims=True)  # 形状: [N, 1, 6]
    
    # 3. 方差开根号得到标准差 (对应 C 的 sqrtf)
    std_dev = np.sqrt(variance)  # 形状: [N, 1, 6]
    
    # 4. Z-Score 变换，分母死死对齐 C 语言的 1e-6f 安全垫
    epsilon = 1e-6
    X_normalized = (X - mean) / (std_dev + epsilon)
    
    return X_normalized
    
# 修改后的预处理函数替代标准化函数
def align_perfect_scale(X):
    """
    X 形状: [N, 256, 6]
    完美兼顾：去重力零偏 + 强力保留物理比例 + 压制到 [-1, 1]
    """
    # 1. 减去当前窗口的均值（第一步：去重力直流分量，去传感器硬件零偏）
    X_centered = X - np.mean(X, axis=1, keepdims=True)
    
    # 2. 全局固定比例缩放（第二步：让数据躺在 [-1, 1] 之间，但绝不盲目放大噪声）
    # 前 3 轴（加速计）动态最大值约为 1.5g，我们除以 1.5
    X_centered[:, :, :3] = X_centered[:, :, :3] / 1.5
    
    # 后 3 轴（陀螺仪）动态最大值约为 180°/s，我们除以 180.0
    X_centered[:, :, 3:] = X_centered[:, :, 3:] / 90.0
    
    return X_centered

# ========================================================
# 📦 独立数据装载器（彻底杜绝数据泄露）
# ========================================================
def load_single_directory(target_dir):
    """ 核心：只读取指定文件夹下的数据，打上对应标签 """
    X_list = []
    y_list = []
    
    for file_name in os.listdir(target_dir):
        # 确保只读当前层级的 .npy 文件，不误入子文件夹
        if file_name.endswith(".npy") and "_clean" in file_name:
            label = int(file_name.split("_")[1])
            data = np.load(os.path.join(target_dir, file_name)) 
            X_list.append(data)
            y_list.append(np.full((len(data),), label))
    # 第二步：自动化深度搜索各个子目录，把 `add` 里面的新增快慢包捞出来
    print(" 🔄 正在深度扫描各个 add 扩容子目录...")
    for root, dirs, files in os.walk(target_dir):
        # 只要发现路径里带有 /add 或 \\add
        if os.path.basename(root) == ADD_DIR:
            for file_name in files:
                if file_name.endswith(".npy") and "_clean" in file_name:
                    label = int(file_name.split("_")[1])
                    data = np.load(os.path.join(root, file_name))
                    X_list.append(data)
                    y_list.append(np.full((len(data),), label))
                    print(f" 🔥 [火箭补强] 已融合 add 扩容包: {file_name} -> 样本数: {len(data)}")
                    
    if not X_list:
        raise FileNotFoundError(f"❌ 错误：在 {target_dir} 下没找到任何 _clean.npy 文件！")
        
    X_all = np.concatenate(X_list, axis=0) 
    y_all = np.concatenate(y_list, axis=0) 
    
    # 执行板子同款物理缩放
    X_all = align_perfect_scale(X_all)
    
    # 轴对换：从 [N, 256, 6] 变为 Conv1d 期望的 [N, 6, 256]
    X_all = np.transpose(X_all, (0, 2, 1)) 
    
    return X_all, y_all

def load_test_directory(target_dir):
    """ 测试集纯净加载器，只读当前的单层目录，确保绝对不泄露 """
    X_list = []
    y_list = []
    for file_name in os.listdir(target_dir):
        if file_name.endswith(".npy") and "_clean" in file_name:
            label = int(file_name.split("_")[1])
            data = np.load(os.path.join(target_dir, file_name)) 
            X_list.append(data)
            y_list.append(np.full((len(data),), label))
            
    if not X_list:
        raise FileNotFoundError(f"❌ 错误：在测试集目录 {target_dir} 下没找到任何考卷文件！")
        
    X_all = np.concatenate(X_list, axis=0) 
    y_all = np.concatenate(y_list, axis=0) 
    X_all = align_perfect_scale(X_all)
    X_all = np.transpose(X_all, (0, 2, 1)) 
    return X_all, y_all

# ========================================================
# 📦 数据装载器与高效 Train/Test 纯数据切分
# ========================================================
def load_and_split_dataset(clean_dir, train_ratio=0.8):
    X_list = []
    y_list = []
    
    # 自动扫描加载 8 个类别的 .npy 二进制包
    for file_name in os.listdir(clean_dir):
        if file_name.endswith(".npy") and "_clean" in file_name:
            label = int(file_name.split("_")[1])
            data = np.load(os.path.join(clean_dir, file_name)) # [500, 256, 6]
            X_list.append(data)
            y_list.append(np.full((len(data),), label))
            
    if not X_list:
        raise FileNotFoundError(f"❌ 错误：在 {clean_dir} 下没找到任何清洗好的 _clean.npy 文件！")
        
    X_all = np.concatenate(X_list, axis=0) # [4000, 256, 6]
    y_all = np.concatenate(y_list, axis=0) # [4000]
    
    # 🔥 核心对齐：在切分前，让所有数据经受板子同款 Z-Score 的洗礼！
    print("✨ 正在执行板子对齐级 Z-Score 标准化变换...")
    X_all = align_perfect_scale(X_all)
    
    # 🔑 极重要：PyTorch 的 Conv1d 期望的输入形状是 [Batch, Channels, Length]
    # 也就是 [N, 6, 256]，而我们的矩阵是 [N, 256, 6]。这里必须进行轴对换！
    X_all = np.transpose(X_all, (0, 2, 1)) # 形状完美变为 [N, 6, 256]
    
    # 纯 NumPy 随机洗牌切分，零依赖，绝不产生偏科
    indices = np.random.permutation(len(X_all))
    split_point = int(len(X_all) * train_ratio)
    
    train_idx, test_idx = indices[:split_point], indices[split_point:]
    
    return X_all[train_idx], y_all[train_idx], X_all[test_idx], y_all[test_idx]

class GestureDataset(Dataset):
    def __init__(self, X, y):
        self.X = torch.tensor(X, dtype=torch.float32)
        self.y = torch.tensor(y, dtype=torch.long)
    def __len__(self):
        return len(self.X)
    def __getitem__(self, idx):
        return self.X[idx], self.y[idx]

# ========================================================
# 🧠 经典端侧轻量化 1D-CNN 神经网络模型
# ========================================================
class Gesture1DCNN(nn.Module):
    def __init__(self, in_channels, num_classes):
        super(Gesture1DCNN, self).__init__()
        # 第一层卷积：抽取局部时序微特征。输入 [6, 256] -> 输出 [32, 128]
        self.conv1 = nn.Sequential(
            nn.Conv1d(in_channels, 32, kernel_size=5, stride=1, padding=2),
            nn.BatchNorm1d(32),
            nn.ReLU(),
            nn.MaxPool1d(kernel_size=2, stride=2)
        )
        # 第二层卷积：组合更高级的几何特征。输入 [32, 128] -> 输出 [64, 64]
        self.conv2 = nn.Sequential(
            nn.Conv1d(32, 64, kernel_size=5, stride=1, padding=2),
            nn.BatchNorm1d(64),
            nn.ReLU(),
            nn.MaxPool1d(kernel_size=2, stride=2)
        )
        # 第三层卷积：锁定全局整体宏观走势。输入 [64, 64] -> 输出 [128, 32]
        self.conv3 = nn.Sequential(
            nn.Conv1d(64, 128, kernel_size=3, stride=1, padding=1),
            nn.BatchNorm1d(128),
            nn.ReLU(),
            nn.MaxPool1d(kernel_size=2, stride=2)
        )
        # 全连接层分类器
        self.fc = nn.Sequential(
            nn.Linear(128 * 32, 256),
            nn.ReLU(),
            nn.Dropout(0.6), # 防止过拟合的保护伞
            nn.Linear(256, num_classes) # 输出 8 个类的置信度 logits
        )

    def forward(self, x):
        x = self.conv1(x)
        x = self.conv2(x)
        x = self.conv3(x)
        x = x.view(x.size(0), -1) # 展平矩阵
        x = self.fc(x)
        return x

# ========================================================
# 🎯 炼丹主循环：激情训练与期末测试
# ========================================================
def train_pipeline():

    print(f"🔄 正在从 {TRAIN_DIR} 加载老数据作为训练集...")
    X_train, y_train = load_single_directory(TRAIN_DIR)
    
    print(f"🔄 正在从 {TEST_DIR} 加载新数据作为冷酷考官...")
    X_test, y_test = load_test_directory(TEST_DIR)

    train_indices = np.random.permutation(len(X_train))
    X_train, y_train = X_train[train_indices], y_train[train_indices]
    
    # 1. 准备数据集
    #X_train, y_train, X_test, y_test = load_and_split_dataset(CLEAN_DIR)
    print(f"📊 数据就绪 -> 🟢 训练集(刷题用): {len(X_train)}个 | 🔴 测试集(模考用): {len(X_test)}个")
    
    train_loader = DataLoader(GestureDataset(X_train, y_train), batch_size=BATCH_SIZE, shuffle=True)
    test_loader = DataLoader(GestureDataset(X_test, y_test), batch_size=BATCH_SIZE, shuffle=False)
    
    # 2. 实例网络与环境检查
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"💻 炼丹炉核心硬件锁定为: 【 {device} 】")
    
    model = Gesture1DCNN(in_channels=GESTURE_CHANNELS, num_classes=NUM_CLASSES).to(device)
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.AdamW(model.parameters(), lr=LEARNING_RATE, weight_decay=0.01)
    
    # 3. 开启 Epoch 狂飙
    best_acc = 0.0
    print("\n🔥 开始炼丹...")
    for epoch in range(EPOCHS):
        model.train()
        train_loss, train_correct = 0.0, 0
        for data, target in train_loader:
            data, target = data.to(device), target.to(device)
            optimizer.zero_grad()
            output = model(data)
            loss = criterion(output, target)
            loss.backward()
            optimizer.step()
            
            train_loss += loss.item() * data.size(0)
            pred = output.argmax(dim=1, keepdim=True)
            train_correct += pred.eq(target.view_as(pred)).sum().item()
            
        train_loss /= len(train_loader.dataset)
        train_acc = train_correct / len(train_loader.dataset)
        
        # 4. 🔴 严格的期末模拟考：在 Test 集上验证鲁棒性
        model.eval()
        test_loss, test_correct = 0.0, 0
        with torch.no_grad():
            for data, target in test_loader:
                data, target = data.to(device), target.to(device)
                output = model(data)
                test_loss += criterion(output, target).item() * data.size(0)
                pred = output.argmax(dim=1, keepdim=True)
                test_correct += pred.eq(target.view_as(pred)).sum().item()
                
        test_loss /= len(test_loader.dataset)
        test_acc = test_correct / len(test_loader.dataset)
        
        print(f"Epoch [{epoch+1:03d}/{EPOCHS}] -> 🟢 Train Acc: {train_acc*100:.2f}% (Loss: {train_loss:.4f}) | 🔴 Test Acc: {test_acc*100:.2f}% (Loss: {test_loss:.4f})")
        
        # 权重封存：只存考试成绩最好的那个神级模型
        if test_acc > best_acc:
            best_acc = test_acc
            save_dir = "./mode"
            os.makedirs(save_dir, exist_ok=True)
            torch.save(model.state_dict(), os.path.join(save_dir, "gesture_model.pth"))
            
    print(f"\n🎉 炼丹大圆满！模考最高准确率: 【 {best_acc*100:.2f}% 】 权重已妥善封存至 best_gesture_model.pth！\n")

if __name__ == "__main__":
    train_pipeline()