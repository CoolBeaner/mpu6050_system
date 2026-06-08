# -*- coding: utf-8 -*-
import socket
import struct
import time
import json
import threading
import serial
import re
import queue

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# ==============================================================================
# 核心数据配置区 (需与板子端的 common.h 严格保持一致)
# ==============================================================================
UDP_IP = "0.0.0.0"     # 监听所有网卡
UDP_PORT = 9000        # 接收板子数据的端口
# 声明板子接收指令的端口 (需与你 C 代码中绑定的端口一致)
BOARD_CMD_PORT = 9001  

# 串口核心配置
SERIAL_PORT = 'COM5'   # Linux下写 '/dev/ttyUSB0' 等
SERIAL_BAUD = 115200  # 必须与你板子的 UART 初始化波特率完全一致

# 声明全局变量，用来动态记录板子的物理 IP
board_ip_address = None

# 用来登记所有当前连上网页的手机/电脑的“专属邮箱列表”
connected_serial_queues = []
queues_lock = threading.Lock()  # 保护花名册的锁

# TelemetryPkg_t 结构体二进制解析格式
# < : 小端序 | I : uint32 (seq) | d : double (dt) | f : float (sensor_temp)
# fff : pitch, roll, yaw | fff : raw_accel | fff : cpu, mem, soc_temp | B : mode | H : checksum
PKG_FORMAT = "<Id f fff fff fff B H"
PKG_SIZE = struct.calcsize(PKG_FORMAT)

# 全局共享状态字典 (多线程安全访问)
global_telemetry_data = {
    "seq": 0, "dt": 0.0, "sensor_temp": 0.0,
    "pitch": 0.0, "roll": 0.0, "yaw": 0.0,
    "accel_x": 0.0, "accel_y": 0.0, "accel_z": 0.0,
    "cpu_usage": 0.0, "mem_usage": 0.0, "soc_temp": 0.0,
    "mode": 0, "serial_out": ""
}
data_lock = threading.Lock()

# 模式映射字典
MODE_MAP = {
    0: "空闲模式 (IDLE)",
    1: "空中鼠标 (MOUSE)",
    2: "手势识别 (GESTURE)",
    3: "零偏校准 (CALIB)"
}

# ==============================================================================
# 算法辅助函数 (CRC16 校验)
# ==============================================================================
def calculate_crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

# ==============================================================================
# 多线程高性能网络与 Web 伺服服务器实现
# ==============================================================================
class AdvancedUIRequestHandler(BaseHTTPRequestHandler):
    def handle(self):
        try:
            super().handle()
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass
            
    def log_message(self, format, *args):
        return

    def do_GET(self):
        url_parsed = urlparse(self.path)
        
        if url_parsed.path == '/':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            try:
                with open('web_gui/index.html', 'r', encoding='utf-8') as f:
                    html_content = f.read()
                self.wfile.write(html_content.encode('utf-8'))
            except FileNotFoundError:
                self.wfile.write(b"Error: index.html not found in current directory.")
                
        elif url_parsed.path == '/fluid.html':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            try:
                # 假设你的 new.html 就直接扔在 Python 脚本的同级目录下
                with open('./web_gui/fluid.html', 'r', encoding='utf-8') as f:
                    html_content = f.read()
                self.wfile.write(html_content.encode('utf-8'))
            except FileNotFoundError:
                self.wfile.write(b"Error: fluid.html not found in current directory.")
                
        elif url_parsed.path == '/stream':
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Connection', 'keep-alive')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            my_queue = queue.Queue()
            with queues_lock:
                connected_serial_queues.append(my_queue)
            
            last_seq = -1
            try:
                while True:
                    my_serial_data = ""
                    while not my_queue.empty():
                        my_serial_data += my_queue.get_nowait()
                    if my_serial_data:    
                        my_serial_data = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', my_serial_data)
                    with data_lock:
                        curr_seq = global_telemetry_data["seq"]
                        if curr_seq != last_seq or my_serial_data != "":
                            packet = global_telemetry_data.copy()
                            packet["serial_out"] = my_serial_data
                            json_str = json.dumps(packet)
                            last_seq = curr_seq
                            #global_telemetry_data["serial_out"] = ""
                            self.wfile.write(f"data: {json_str}\n\n".encode('utf-8'))
                            self.wfile.flush()
                    time.sleep(0.01)
            except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
                pass
            finally:
                # 防内存泄漏 一旦手机或电脑关闭了网页
                # 必须把它的专属邮箱从花名册里注销擦除
                with queues_lock:
                    if my_queue in connected_serial_queues:
                        connected_serial_queues.remove(my_queue)
        else:
            self.send_error(404, "File Not Found")

    def do_POST(self):
        global board_ip_address
        url_parsed = urlparse(self.path)
        
        if url_parsed.path == '/write_serial':
            query_params = parse_qs(url_parsed.query)
            cmd_text = query_params.get('cmd', [''])[0]
            
            if ser and ser.is_open and cmd_text:
                if cmd_text == "\x03":
                    ser.write(b'\x03')
                    print("[串口下发] -> [Ctrl+C 中断信号]")
                else:
                    # 将网页发来的文本转成字节，直接打入板子
                    ser.write((cmd_text + "\n").encode('utf-8'))
                    print(f"[串口下发] -> {cmd_text}")
                    
            self.send_response(200)
            self.end_headers()
        
        if url_parsed.path == '/set_mode':
            query_params = parse_qs(url_parsed.query)
            mode_val = int(query_params.get('mode', ['0'])[0])
            
            # 默认定义两个字节的初始值
            cmd_type = 0x00
            cmd_val  = 0x00

            if mode_val in [0, 1, 2]:
                cmd_type = 0x01      # 对应 C 语言的 case 0x01: 切换模式
                cmd_val  = mode_val  # 对应 Mode_e 挂载值 (0, 1, 2)
            elif mode_val == 3:
                cmd_type = 0x02      # 对应 C 语言的 case 0x02: 启动校准
                cmd_val  = 0x00      # 填任意值，C 端没用到 cmd_buffer[1]
                
            # 打包为两个无符号单字节 (Unsigned Char -> uint8_t)
            cmd_packet = struct.pack("BB", cmd_type, cmd_val)    
            
            if board_ip_address is not None:
                try:
                    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    cmd_sock.sendto(cmd_packet, (board_ip_address, BOARD_CMD_PORT))
                    cmd_sock.close()
                    print(f"[下发成功] 目标板子 {board_ip_address}:{BOARD_CMD_PORT} -> 发送字节: {cmd_packet.hex().upper()}")
                except Exception as e:
                    print(f"[下发失败] 发送 UDP 发生系统错误: {e}")
            else:
                print("[发送终止] 尚未收到板子的任何高频遥测包，未知板子 IP，请先开启板子端发送！")
            self.send_response(200)
            self.end_headers()                
        else:
            self.send_error(404)

# ==============================================================================
# 板载 UDP 数据高效无延迟接收接收线程 (50Hz)
# ==============================================================================
def run_udp_receiver():
    global global_telemetry_data, board_ip_address
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.settimeout(1.0)
    print(f"[Thread-UDP] 500Hz/50Hz 板端高频遥测监听器成功绑定端口: {UDP_PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(1024)
            if len(data) == PKG_SIZE:
                # 动态捕捉并实时锁死板子的真实 IP 
                if board_ip_address != addr[0]:
                    board_ip_address = addr[0]
                 # 校验 CRC16   
                received_crc = struct.unpack("<H", data[-2:])[0]
                calculated_crc = calculate_crc16(data[:-2])
                
                if received_crc != calculated_crc:
                    print("[Thread-UDP] 警告：网络包通过 CRC16 校验对比失败，怀疑存在链路空中乱码抖动。")
                    continue
                
                vals = struct.unpack(PKG_FORMAT, data)
                
                with data_lock:
                    global_telemetry_data["seq"]         = vals[0]
                    global_telemetry_data["dt"]          = vals[1]
                    global_telemetry_data["sensor_temp"] = vals[2]
                    global_telemetry_data["pitch"]       = vals[3]
                    global_telemetry_data["roll"]        = vals[4]
                    global_telemetry_data["yaw"]         = vals[5]
                    global_telemetry_data["accel_x"]     = vals[6]
                    global_telemetry_data["accel_y"]     = vals[7]
                    global_telemetry_data["accel_z"]     = vals[8]
                    global_telemetry_data["cpu_usage"]   = vals[9]
                    global_telemetry_data["mem_usage"]   = vals[10]
                    global_telemetry_data["soc_temp"]    = vals[11]
                    global_telemetry_data["mode"]        = vals[12]
            else:
                if len(data) > 0:
                    print(f"[Thread-UDP] 丢弃错误异常长度包，期望尺寸: {PKG_SIZE}, 收到尺寸: {len(data)}")
        except socket.timeout:
            continue
        except Exception as e:
            print(f"[Thread-UDP] 遭遇意外未知底层链路异常: {e}")
            break
    sock.close()

def run_serial_receiver():
    global ser
        # 全局串口句柄
    ser = None
    try:
        ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0.1)
        print(f"[Serial] 成功开启物理串口: {SERIAL_PORT}")
    except Exception as e:
        print(f"[Serial] 警告：物理串口开启失败（检查是否被其他串口助手占用）: {e}")
        
    if ser is None: return
    print("[Thread-Serial] 串口高频监听器已就绪...")
    
    while True:
        try:
            avail = ser.in_waiting
            if avail > 0:
                time.sleep(0.002)
                avail = ser.in_waiting
                # 读取一行板子发过来的数据
                raw_data = ser.read(avail)
                # 转换成标准文本
                text_line = raw_data.decode('utf-8', errors='ignore')
                
                # 把串口文本也作为事件塞进你的全局共享字典，
                # 或者专门开辟一个 global_serial_buffer，让 /stream 顺便带去前端
                if text_line:
                    with queues_lock:
                        for q in connected_serial_queues:
                            q.put(text_line)
            else:
                time.sleep(0.005)
        except Exception as e:
            time.sleep(0.1)

# ==============================================================================
# 系统主程序启动入口
# ==============================================================================
def main():
    udp_thread = threading.Thread(target=run_udp_receiver, daemon=True)
    url_thread = threading.Thread(target=run_serial_receiver, daemon=True)
    udp_thread.start()
    url_thread.start()

    web_port = 8080
    server = ThreadingHTTPServer(('0.0.0.0', web_port), AdvancedUIRequestHandler)
    
    print(f"\n======================================================================")
    print(f">>  IMU 全息空间交互全景控制台系统已成功部署就绪！")
    print(f">>  请打开您的电脑浏览器，并在地址栏输入本地服务地址即可进入可视化大屏：")
    print(f">>  http://localhost:{web_port}")
    print(f"======================================================================\n")  
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[Shutdown] 接收到退出指令，全息中枢正在卸载。")
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
