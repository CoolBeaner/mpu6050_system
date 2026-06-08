import socket
import struct
import time

# 配置与板子对应的参数 (需与 common.h 严格一致)
UDP_IP = "0.0.0.0"  # 监听所有网卡
UDP_PORT = 9000     # 对应 network.c 中的 TELEMETRY_PORT

# 转发给 VOFA+ 的配置
VOFA_IP = "127.0.0.1"      # VOFA+ 和 Python 在同一台电脑上
VOFA_PORT = 9500           # 转发给 VOFA+ 的端口

# 定义解析格式 (与 TelemetryPkg_t 对应)
# < : 小端序
# I : uint32 (seq)
# d : double (dt)
# f : float  (sensor_temp)
# fff : 3*float (pitch, roll, yaw)
# fff : 3*float (raw_accel)
# fff : 3*float (sys_status: cpu, mem, temp)
# B : uint8 (mode)
# H : uint16 (checksum)
PKG_FORMAT = "<Id f fff fff fff B H"
PKG_SIZE = struct.calcsize(PKG_FORMAT)

# 创建发送给 VOFA+ 的 UDP Socket
vofa_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

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

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    # 设置超时时间为 1.0 秒
    sock.settimeout(1.0)
    print(f"等待来自板子的 UDP 数据包 (端口 {UDP_PORT})...")

    try:
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) == PKG_SIZE:
                    # 校验 CRC
                    received_crc = struct.unpack("<H", data[-2:])[0]
                    calculated_crc = calculate_crc16(data[:-2])
                    
                    if received_crc != calculated_crc:
                        print("CRC 校验失败，数据包已损坏")
                        continue

                    # 解析数据
                    vals = struct.unpack(PKG_FORMAT, data)
                    seq = vals[0]
                    dt = vals[1]
                    sensor_temp = vals[2]
                    pitch = vals[3]
                    roll  = vals[4]
                    yaw   = vals[5]
                    raw_accel_x = vals[6]
                    raw_accel_y = vals[7]
                    raw_accel_z = vals[8]
                    cpu_usage  = vals[9]
                    mem_usage = vals[10]
                    soc_temp  = vals[11]
                    mode = vals[12]    
                    '''
                    # 打印
                    print(f"Seq: {seq:5} | Mode: {mode} | DT: {dt*1000:.2f}ms | "
                          f"P: {pitch:6.2f}, R: {roll:6.2f}, Y: {yaw:6.2f} | "
                          f"AX: {raw_accel_x:6.2f}, AY: {raw_accel_y:6.2f}, AZ: {raw_accel_z:6.2f} | "
                          f"CPU: {cpu_usage:4.1f}% | Temp: {soc_temp:.1f}'C | mem_usage: {mem_usage:.1f} |"
                          f"sensor_temp: {sensor_temp:.1f}'C ")
                    '''

                    # 核心：包装成 VOFA+ 的 FireWater 协议格式
                    # 格式要求：数据1,数据2,数据3... \n
                    vofa_packet = (
                        f"{dt * 1000:.2f},{sensor_temp:.2f},"
                        f"{pitch:.2f},{roll:.2f},{yaw:.2f},"
                        f"{raw_accel_x:.3f},{raw_accel_y:.3f},{raw_accel_z:.3f},"
                        f"{cpu_usage:.1f},{mem_usage:.1f},{soc_temp:.1f}\n"
                    )
                    # 发给 VOFA+
                    vofa_sock.sendto(vofa_packet.encode('utf-8'), (VOFA_IP, VOFA_PORT))
                else:
                    print(f"收到错误长度的包: {len(data)}")
                    
            except socket.timeout:
                # 如果 1 秒内没收到数据，会触发这个异常
                continue
                
    except KeyboardInterrupt:
        print("\n停止监听。")
    finally:
        sock.close()
        vofa_sock.close()

if __name__ == "__main__":
    main()