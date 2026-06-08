动作信号 ------------> ai 期望结果
右手：
无效信号 invalid_signal          ----> 0 			
正圆 circular_signal               ----> 1
逆圆 inverse_circular_signal   ----> 2
正三角 equilateral_triangle    ----> 3
逆三角 inverted_triangle        ----> 4
横线  line_signal	            ----> 5 
闪电 flash_signal                   ----> 6 
无穷 infinity_signal                ----> 7 

plot_raw_gesture.py                        原始数据画图工具针对   .csv

clean_and_merge.py                       手动确认数据，只负责根据算法找到的波峰检测动作，手动确认是否是数据。

auto_stride_invalid_slicer.py             解决样本不够问题，利用相位偏移，用于自动切 无效波形，自动算最大步长，切出500个不同的样本 把 .csv 变成 .npy

bbox_perfect_slicer.py                     解决样本不够问题，利用相位偏移，用于标注数据，会根据指定文件夹，把文件夹下的所有文件进行标注，
                                                      提取每个波形，人工标记最大左偏/右偏可偏移量，根据总偏移量算出最大步长，切出500个样本。把 .csv变成 .npy

view_npy.py	                      将 .npy（清理后的数据）文件画成图，检测切的样本是否有错误。

view_raw_vs_normalized_data.py     对比查看清洗后的数据和预处理之后的数据

train_gesture_1dcnn.py                   使用 1dcnn 训练模型

export_to_c.py                                将 .pty 模型转为 .h c语言权重头文件，在板子上调用
 


