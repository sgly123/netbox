#!/usr/bin/env python3
"""
IoT网关演示程序

功能：
1. 模拟Modbus设备
2. 订阅MQTT主题查看数据
"""

import socket
import struct
import time
import random
import threading
import paho.mqtt.client as mqtt

class ModbusSimulator:
    """Modbus TCP设备模拟器"""
    
    def __init__(self, host='0.0.0.0', port=502):
        self.host = host
        self.port = port
        self.running = False
        self.server_socket = None
        
        # 模拟寄存器数据
        self.registers = {
            0: 250,   # 温度 25.0°C
            1: 650,   # 湿度 65.0%
            2: 1013,  # 压力 101.3kPa
            3: 150    # 流量 15.0L/min
        }
    
    def start(self):
        """启动模拟器"""
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        
        print(f"✅ Modbus模拟器启动: {self.host}:{self.port}")
        
        while self.running:
            try:
                client_socket, addr = self.server_socket.accept()
                print(f"📡 客户端连接: {addr}")
                threading.Thread(target=self.handle_client, args=(client_socket,)).start()
            except Exception as e:
                if self.running:
                    print(f"❌ 接受连接错误: {e}")
    
    def handle_client(self, client_socket):
        """处理客户端请求"""
        try:
            while self.running:
                # 接收Modbus TCP请求
                data = client_socket.recv(1024)
                if not data:
                    break
                
                # 解析MBAP头
                transaction_id = struct.unpack('>H', data[0:2])[0]
                protocol_id = struct.unpack('>H', data[2:4])[0]
                length = struct.unpack('>H', data[4:6])[0]
                unit_id = data[6]
                function_code = data[7]
                
                print(f"📥 收到请求: 功能码={function_code:02X}")
                
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
                    response.append(quantity * 2)  # 字节数
                    
                    # 添加寄存器值（模拟数据 + 随机波动）
                    for i in range(quantity):
                        addr = start_addr + i
                        base_value = self.registers.get(addr, 0)
                        # 添加±5%的随机波动
                        value = int(base_value * (1 + random.uniform(-0.05, 0.05)))
                        response.extend(struct.pack('>H', value))
                    
                    client_socket.send(response)
                    print(f"📤 发送响应: {quantity}个寄存器")
                
        except Exception as e:
            print(f"❌ 处理客户端错误: {e}")
        finally:
            client_socket.close()
    
    def stop(self):
        """停止模拟器"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()
        print("🛑 Modbus模拟器已停止")


class MQTTSubscriber:
    """MQTT订阅者"""
    
    def __init__(self, broker='localhost', port=1883, topic='iot/devices/#'):
        self.broker = broker
        self.port = port
        self.topic = topic
        self.client = mqtt.Client()
        
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
    
    def on_connect(self, client, userdata, flags, rc):
        """连接回调"""
        if rc == 0:
            print(f"✅ 已连接到MQTT Broker: {self.broker}:{self.port}")
            client.subscribe(self.topic)
            print(f"📡 订阅主题: {self.topic}")
        else:
            print(f"❌ 连接失败，返回码: {rc}")
    
    def on_message(self, client, userdata, msg):
        """消息回调"""
        print(f"\n📨 收到MQTT消息:")
        print(f"   主题: {msg.topic}")
        print(f"   数据: {msg.payload.decode()}")
    
    def start(self):
        """启动订阅"""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_forever()
        except Exception as e:
            print(f"❌ MQTT连接错误: {e}")


def main():
    print("=" * 60)
    print("IoT网关演示程序")
    print("=" * 60)
    
    # 启动Modbus模拟器
    modbus_sim = ModbusSimulator(host='0.0.0.0', port=502)
    modbus_thread = threading.Thread(target=modbus_sim.start)
    modbus_thread.daemon = True
    modbus_thread.start()
    
    time.sleep(1)
    
    # 启动MQTT订阅者
    print("\n启动MQTT订阅者...")
    mqtt_sub = MQTTSubscriber(broker='localhost', port=1883, topic='iot/devices/#')
    
    try:
        mqtt_sub.start()
    except KeyboardInterrupt:
        print("\n\n🛑 程序退出")
        modbus_sim.stop()


if __name__ == '__main__':
    main()
