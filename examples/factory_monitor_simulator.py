#!/usr/bin/env python3
"""
智能工厂设备模拟器

模拟3条生产线的设备：
1. 注塑机 - 端口5021
2. 包装机 - 端口5022
3. 质检设备 - 端口5023
"""

import socket
import struct
import time
import random
import threading
import paho.mqtt.client as mqtt
from datetime import datetime

class ModbusDeviceSimulator:
    """Modbus TCP设备模拟器"""
    
    def __init__(self, name, port, registers):
        self.name = name
        self.port = port
        self.registers = registers  # {地址: 初始值}
        self.running = False
        self.server_socket = None
        self.update_thread = None
    
    def start(self):
        """启动模拟器"""
        self.running = True
        
        # 启动TCP服务器
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('0.0.0.0', self.port))
        self.server_socket.listen(5)
        
        print(f"✅ [{self.name}] Modbus模拟器启动: 端口{self.port}")
        
        # 启动数据更新线程
        self.update_thread = threading.Thread(target=self.update_data)
        self.update_thread.daemon = True
        self.update_thread.start()
        
        # 处理客户端连接
        while self.running:
            try:
                client_socket, addr = self.server_socket.accept()
                threading.Thread(target=self.handle_client, args=(client_socket,)).start()
            except Exception as e:
                if self.running:
                    print(f"❌ [{self.name}] 接受连接错误: {e}")
    
    def update_data(self):
        """定期更新寄存器数据（模拟真实设备）"""
        while self.running:
            # 温度：添加±2°C波动
            if 0 in self.registers:
                base_temp = 650  # 65.0°C
                self.registers[0] = int(base_temp + random.uniform(-20, 20))
            
            # 压力：添加±5kPa波动
            if 1 in self.registers:
                base_pressure = 1200  # 120.0kPa
                self.registers[1] = int(base_pressure + random.uniform(-50, 50))
            
            # 电机状态：偶尔停止
            if 2 in self.registers:
                if random.random() < 0.05:  # 5%概率停止
                    self.registers[2] = 0
                else:
                    self.registers[2] = 1
            
            # 转速：1000-1500 RPM
            if 3 in self.registers:
                self.registers[3] = random.randint(1000, 1500)
            
            # 计数器：递增
            if 4 in self.registers:
                self.registers[4] = (self.registers[4] + 1) % 10000
            
            time.sleep(1)
    
    def handle_client(self, client_socket):
        """处理客户端Modbus请求"""
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                
                # 解析MBAP头
                transaction_id = struct.unpack('>H', data[0:2])[0]
                protocol_id = struct.unpack('>H', data[2:4])[0]
                unit_id = data[6]
                function_code = data[7]
                
                # 处理读保持寄存器请求 (0x03)
                if function_code == 0x03:
                    start_addr = struct.unpack('>H', data[8:10])[0]
                    quantity = struct.unpack('>H', data[10:12])[0]
                    
                    # 构建响应
                    response = bytearray()
                    response.extend(struct.pack('>H', transaction_id))
                    response.extend(struct.pack('>H', protocol_id))
                    response.extend(struct.pack('>H', 3 + quantity * 2))
                    response.append(unit_id)
                    response.append(function_code)
                    response.append(quantity * 2)
                    
                    # 添加寄存器值
                    for i in range(quantity):
                        addr = start_addr + i
                        value = self.registers.get(addr, 0)
                        response.extend(struct.pack('>H', value))
                    
                    client_socket.send(response)
                    
                    # 打印日志
                    timestamp = datetime.now().strftime("%H:%M:%S")
                    values = [self.registers.get(start_addr + i, 0) for i in range(quantity)]
                    print(f"[{timestamp}] [{self.name}] 读取寄存器 {start_addr}-{start_addr+quantity-1}: {values}")
                
        except Exception as e:
            pass
        finally:
            client_socket.close()
    
    def stop(self):
        """停止模拟器"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()


class MQTTMonitor:
    """MQTT数据监控"""
    
    def __init__(self, broker='localhost', port=1883):
        self.broker = broker
        self.port = port
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.data_count = {}
    
    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"\n✅ MQTT监控已连接: {self.broker}:{self.port}")
            client.subscribe("factory/production/#")
            print("📡 订阅主题: factory/production/#\n")
            print("=" * 80)
        else:
            print(f"❌ MQTT连接失败，返回码: {rc}")
    
    def on_message(self, client, userdata, msg):
        """接收并显示MQTT消息"""
        import json
        
        try:
            # 解析设备ID
            topic_parts = msg.topic.split('/')
            device_id = topic_parts[2] if len(topic_parts) > 2 else "unknown"
            
            # 统计消息数
            self.data_count[device_id] = self.data_count.get(device_id, 0) + 1
            
            # 解析数据
            data = json.loads(msg.payload.decode())
            
            # 格式化显示
            timestamp = datetime.now().strftime("%H:%M:%S")
            print(f"\n[{timestamp}] 📊 设备: {device_id} (第{self.data_count[device_id]}次)")
            print("-" * 80)
            
            for item in data:
                name = item['name']
                value = item['value']
                unit = item['unit']
                quality = item['quality']
                
                # 根据数据类型添加图标
                icon = "🌡️" if "temperature" in name else \
                       "💨" if "pressure" in name else \
                       "⚙️" if "motor" in name else \
                       "🔄" if "speed" in name else \
                       "📦" if "count" in name else "📈"
                
                # 状态显示
                status = ""
                if "motor_status" in name:
                    status = " [运行]" if value == 1 else " [停止]"
                elif "temperature" in name and value > 80:
                    status = " ⚠️ 温度过高!"
                elif "pressure" in name and value > 150:
                    status = " ⚠️ 压力异常!"
                
                print(f"  {icon} {name:20s}: {value:8.1f} {unit:6s} [{quality}]{status}")
            
            print("=" * 80)
            
        except Exception as e:
            print(f"❌ 解析消息错误: {e}")
    
    def start(self):
        """启动监控"""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_forever()
        except Exception as e:
            print(f"❌ MQTT监控错误: {e}")


def main():
    print("=" * 80)
    print("🏭 智能工厂设备监控系统 - 设备模拟器")
    print("=" * 80)
    print()
    
    # 创建3个设备模拟器
    devices = [
        # 1号线 - 注塑机
        ModbusDeviceSimulator(
            name="1号线注塑机",
            port=5021,
            registers={
                0: 650,    # 温度 65.0°C
                1: 1200,   # 压力 120.0kPa
                2: 1,      # 电机状态 运行
                3: 1200,   # 转速 1200 RPM
                4: 0,      # 循环次数
                5: 0       # 报警代码
            }
        ),
        
        # 2号线 - 包装机
        ModbusDeviceSimulator(
            name="2号线包装机",
            port=5022,
            registers={
                0: 450,    # 温度 45.0°C
                1: 550,    # 湿度 55.0%
                2: 1,      # 电机状态 运行
                3: 800,    # 速度 800 件/分钟
                4: 0       # 产品计数
            }
        ),
        
        # 3号线 - 质检设备
        ModbusDeviceSimulator(
            name="3号线质检设备",
            port=5023,
            registers={
                0: 250,    # 温度 25.0°C
                1: 1,      # 电机状态 运行
                2: 0,      # 合格数
                3: 0       # 不合格数
            }
        )
    ]
    
    # 启动所有设备
    for device in devices:
        thread = threading.Thread(target=device.start)
        thread.daemon = True
        thread.start()
        time.sleep(0.5)
    
    print()
    print("=" * 80)
    print("📡 所有设备已启动，等待网关连接...")
    print("=" * 80)
    print()
    
    # 等待一下让设备启动
    time.sleep(2)
    
    # 启动MQTT监控
    print("启动MQTT数据监控...\n")
    
    # Docker环境使用 mosquitto-broker，本地使用 localhost
    import os
    mqtt_broker = os.environ.get('MQTT_BROKER', 'mosquitto-broker')
    monitor = MQTTMonitor(broker=mqtt_broker, port=1883)
    
    try:
        monitor.start()
    except KeyboardInterrupt:
        print("\n\n🛑 程序退出")
        for device in devices:
            device.stop()


if __name__ == '__main__':
    main()
