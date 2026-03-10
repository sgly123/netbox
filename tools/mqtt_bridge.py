#!/usr/bin/env python3
"""
MQTT桥接服务

功能：
1. 监听TCP端口接收网关数据
2. 将数据转发到MQTT Broker
3. 作为C++程序和MQTT之间的桥梁

使用场景：
- 当系统没有安装Paho MQTT C++库时
- 作为轻量级MQTT客户端
"""

import socket
import json
import time
import threading
import argparse
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    print("⚠️  警告: paho-mqtt未安装，请运行: pip3 install paho-mqtt")
    MQTT_AVAILABLE = False


class MQTTBridge:
    """MQTT桥接服务"""
    
    def __init__(self, tcp_host='0.0.0.0', tcp_port=8801, 
                 mqtt_broker='localhost', mqtt_port=1883):
        self.tcp_host = tcp_host
        self.tcp_port = tcp_port
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        
        self.running = False
        self.mqtt_client = None
        self.mqtt_connected = False
        
        self.message_count = 0
        self.error_count = 0
    
    def init_mqtt(self):
        """初始化MQTT客户端"""
        if not MQTT_AVAILABLE:
            print("❌ MQTT库不可用")
            return False
        
        try:
            self.mqtt_client = mqtt.Client(client_id="netbox_mqtt_bridge")
            self.mqtt_client.on_connect = self.on_mqtt_connect
            self.mqtt_client.on_disconnect = self.on_mqtt_disconnect
            
            print(f"🔌 连接到MQTT Broker: {self.mqtt_broker}:{self.mqtt_port}")
            self.mqtt_client.connect(self.mqtt_broker, self.mqtt_port, 60)
            self.mqtt_client.loop_start()
            
            # 等待连接
            for _ in range(10):
                if self.mqtt_connected:
                    return True
                time.sleep(0.5)
            
            print("❌ MQTT连接超时")
            return False
            
        except Exception as e:
            print(f"❌ MQTT初始化失败: {e}")
            return False
    
    def on_mqtt_connect(self, client, userdata, flags, rc):
        """MQTT连接回调"""
        if rc == 0:
            self.mqtt_connected = True
            print(f"✅ 已连接到MQTT Broker")
        else:
            print(f"❌ MQTT连接失败，返回码: {rc}")
    
    def on_mqtt_disconnect(self, client, userdata, rc):
        """MQTT断开回调"""
        self.mqtt_connected = False
        print(f"⚠️  MQTT连接断开，返回码: {rc}")
    
    def publish_to_mqtt(self, topic, payload):
        """发布消息到MQTT"""
        if not self.mqtt_connected:
            print("⚠️  MQTT未连接，消息丢弃")
            self.error_count += 1
            return False
        
        try:
            result = self.mqtt_client.publish(topic, payload, qos=1)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                self.message_count += 1
                return True
            else:
                print(f"❌ MQTT发布失败: {result.rc}")
                self.error_count += 1
                return False
        except Exception as e:
            print(f"❌ MQTT发布异常: {e}")
            self.error_count += 1
            return False
    
    def handle_client(self, client_socket, addr):
        """处理TCP客户端连接"""
        print(f"📡 客户端连接: {addr}")
        
        try:
            while self.running:
                # 接收数据（简单协议：4字节长度 + JSON数据）
                length_data = client_socket.recv(4)
                if not length_data:
                    break
                
                data_length = int.from_bytes(length_data, byteorder='big')
                
                # 接收完整数据
                data = b''
                while len(data) < data_length:
                    chunk = client_socket.recv(data_length - len(data))
                    if not chunk:
                        break
                    data += chunk
                
                if len(data) != data_length:
                    print(f"⚠️  数据不完整: 期望{data_length}字节，收到{len(data)}字节")
                    continue
                
                # 解析JSON
                try:
                    message = json.loads(data.decode('utf-8'))
                    topic = message.get('topic', 'iot/devices/unknown')
                    payload = json.dumps(message.get('data', {}))
                    
                    # 发布到MQTT
                    if self.publish_to_mqtt(topic, payload):
                        timestamp = datetime.now().strftime("%H:%M:%S")
                        print(f"[{timestamp}] ✅ 转发消息 #{self.message_count}: {topic}")
                    
                except json.JSONDecodeError as e:
                    print(f"❌ JSON解析错误: {e}")
                    self.error_count += 1
                
        except Exception as e:
            print(f"❌ 处理客户端错误: {e}")
        finally:
            client_socket.close()
            print(f"🔌 客户端断开: {addr}")
    
    def start(self):
        """启动桥接服务"""
        print("=" * 60)
        print("MQTT桥接服务")
        print("=" * 60)
        
        # 初始化MQTT
        if not self.init_mqtt():
            print("❌ 无法启动MQTT桥接服务")
            return
        
        # 启动TCP服务器
        self.running = True
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            server_socket.bind((self.tcp_host, self.tcp_port))
            server_socket.listen(5)
            print(f"✅ TCP服务器启动: {self.tcp_host}:{self.tcp_port}")
            print(f"✅ MQTT Broker: {self.mqtt_broker}:{self.mqtt_port}")
            print("=" * 60)
            print("等待网关连接...")
            print("按 Ctrl+C 退出")
            print("=" * 60)
            
            while self.running:
                try:
                    server_socket.settimeout(1.0)
                    client_socket, addr = server_socket.accept()
                    threading.Thread(target=self.handle_client, 
                                   args=(client_socket, addr)).start()
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"❌ 接受连接错误: {e}")
                        
        except Exception as e:
            print(f"❌ TCP服务器错误: {e}")
        finally:
            server_socket.close()
            if self.mqtt_client:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
            
            print("\n" + "=" * 60)
            print("服务统计:")
            print(f"  - 成功转发: {self.message_count} 条消息")
            print(f"  - 错误次数: {self.error_count}")
            print("=" * 60)
            print("🛑 MQTT桥接服务已停止")
    
    def stop(self):
        """停止服务"""
        self.running = False


def main():
    parser = argparse.ArgumentParser(description='MQTT桥接服务')
    parser.add_argument('--tcp-host', default='0.0.0.0', help='TCP监听地址')
    parser.add_argument('--tcp-port', type=int, default=8801, help='TCP监听端口')
    parser.add_argument('--mqtt-broker', default='localhost', help='MQTT Broker地址')
    parser.add_argument('--mqtt-port', type=int, default=1883, help='MQTT Broker端口')
    
    args = parser.parse_args()
    
    bridge = MQTTBridge(
        tcp_host=args.tcp_host,
        tcp_port=args.tcp_port,
        mqtt_broker=args.mqtt_broker,
        mqtt_port=args.mqtt_port
    )
    
    try:
        bridge.start()
    except KeyboardInterrupt:
        print("\n\n收到退出信号...")
        bridge.stop()


if __name__ == '__main__':
    main()
