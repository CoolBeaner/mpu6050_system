# 自己移植的busybox手写的root不需要指定解析器脚本
# 且解析器不是这个
# 如果是这个请打开
# #!/bin/bash

# USB 鼠标硬件初始化脚本

# 节点防重入卫语句检查
if [ -e /dev/hidg0 ]; then
    echo "USB 提示 /dev/hidg0 与 /dev/hidg1 节点已存在"
    exit 0
fi

mount -t configfs none /sys/kernel/config
# 在 ConfigFS 中创建一只虚拟 USB 设备实例
cd /sys/kernel/config/usb_gadget/
mkdir g1
cd g1

# 写入标准的厂商信息（这里伪装成罗技鼠标，防止被电脑拦截）
echo 0x046d > idVendor  # Logitech Vendor ID
echo 0xc077 > idProduct # Logitech Mouse Product ID
echo 0x0100 > bcdDevice # 设备版本号
echo 0x0200 > bcdUSB    # USB 2.0 协议

# 创建语言并写入描述文本
mkdir -p strings/0x409
echo "1234567890" > strings/0x409/serialnumber
echo "Logitech" > strings/0x409/manufacturer
echo "Air Mouse Gold" > strings/0x409/product

# 创建一个具有 HID 鼠标功能的实例
mkdir -p functions/hid.usb0
echo 1 > functions/hid.usb0/protocol      # 1 代表鼠标协议
echo 1 > functions/hid.usb0/subclass      # 1 代表启动设备
echo 4 > functions/hid.usb0/report_length # 报告长度

# 核心：写入 鼠标 3键+相对坐标 报告描述符（十六进制字节流）

printf '\x05\x01\x09\x02\xa1\x01\x09\x01\xa1\x00' > /tmp/mouse.desc
printf '\x05\x09\x19\x01\x29\x03\x15\x00\x25\x01' >> /tmp/mouse.desc
printf '\x95\x03\x75\x01\x81\x02\x95\x01\x75\x05\x81\x03' >> /tmp/mouse.desc
printf '\x05\x01\x09\x30\x09\x31\x15\x81\x25\x7f\x75\x08\x95\x02\x81\x06' >> /tmp/mouse.desc
printf '\x05\x01\x09\x38\x15\x81\x25\x7f\x75\x08\x95\x01\x81\x06\xc0\xc0' >> /tmp/mouse.desc

cat /tmp/mouse.desc > functions/hid.usb0/report_desc
rm /tmp/mouse.desc

# 把这个功能绑定到当前的 USB 配置单中
mkdir -p configs/c.1/strings/0x409
echo "Config 1: Mouse" > configs/c.1/strings/0x409/configuration
echo 0x80 > configs/c.1/bmAttributes
ln -s functions/hid.usb0 configs/c.1/

# 激活全志 H3 的 OTG 控制器
ls /sys/class/udc/ > UDC

chmod 666 /dev/hidg0
echo "USB 驱动节点 /dev/hidg0 已成功挂载"