#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Simple Modbus TCP Simulator
Simulates 3 industrial devices for IoT Gateway testing
"""

import socket
import struct
import threading
import time
import random

class ModbusDevice:
    def __init__(self, name, port, register_count=6):
        self.name = name
        self.port = port
        self.register_count = register_count
        self.registers = [0] * register_count
        self.running = False
        self.server_socket = None
        
        # 初始化为合理的工业参数值
        # 模拟真实设备的稳定运行状态
        self.target_values = []  # 目标值（设定值）
        for i in range(register_count):
            # 初始值在正常工作范围内
            base_value = random.randint(180, 220)
            self.registers[i] = base_value
            self.target_values.append(base_value)
    
    def handle_modbus_request(self, request):
        """Handle Modbus TCP request"""
        if len(request) < 12:
            return None
        
        # Parse MBAP header
        trans_id = struct.unpack('>H', request[0:2])[0]
        proto_id = struct.unpack('>H', request[2:4])[0]
        length = struct.unpack('>H', request[4:6])[0]
        unit_id = request[6]
        func_code = request[7]
        
        if func_code == 0x03:  # Read Holding Registers
            start_addr = struct.unpack('>H', request[8:10])[0]
            reg_count = struct.unpack('>H', request[10:12])[0]
            
            # Build response
            response = bytearray()
            response.extend(struct.pack('>H', trans_id))  # Transaction ID
            response.extend(struct.pack('>H', proto_id))  # Protocol ID
            response.extend(struct.pack('>H', 3 + reg_count * 2))  # Length
            response.append(unit_id)  # Unit ID
            response.append(func_code)  # Function code
            response.append(reg_count * 2)  # Byte count
            
            # Add register values
            for i in range(reg_count):
                idx = start_addr + i
                if idx < len(self.registers):
                    response.extend(struct.pack('>H', self.registers[idx]))
                else:
                    response.extend(struct.pack('>H', 0))
            
            return bytes(response)
        
        elif func_code == 0x06:  # Write Single Register
            reg_addr = struct.unpack('>H', request[8:10])[0]
            reg_value = struct.unpack('>H', request[10:12])[0]
            
            # Write register
            if reg_addr < len(self.registers):
                self.registers[reg_addr] = reg_value
            
            # Echo back the request as response
            return request
        
        return None
    
    def handle_client(self, client_socket):
        """Handle client connection"""
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                
                response = self.handle_modbus_request(data)
                if response:
                    client_socket.send(response)
        except:
            pass
        finally:
            client_socket.close()
    
    def start(self):
        """Start Modbus server"""
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.server_socket.bind(('0.0.0.0', self.port))
            self.server_socket.listen(5)
            print(f"[{self.name}] Started on port {self.port}")
            
            while self.running:
                try:
                    self.server_socket.settimeout(1.0)
                    client_socket, addr = self.server_socket.accept()
                    client_thread = threading.Thread(target=self.handle_client, args=(client_socket,))
                    client_thread.daemon = True
                    client_thread.start()
                except socket.timeout:
                    continue
                except:
                    break
        except Exception as e:
            print(f"[{self.name}] Error: {e}")
        finally:
            if self.server_socket:
                self.server_socket.close()
    
    def stop(self):
        """Stop Modbus server"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()
    
    def update_registers(self):
        """模拟真实工业设备的数据变化特征"""
        while self.running:
            time.sleep(2)  # 工业标准：2秒采集周期
            
            # 模拟真实的工业参数变化
            for i in range(self.register_count):
                current = self.registers[i]
                target = self.target_values[i]
                
                # 1. 缓慢趋向目标值（模拟温控过程）
                if abs(current - target) > 5:
                    # 距离目标较远，以1-2的速度接近
                    step = 2 if abs(current - target) > 20 else 1
                    if current < target:
                        self.registers[i] = min(current + step, target)
                    else:
                        self.registers[i] = max(current - step, target)
                
                # 2. 在目标值附近小幅波动（±1，模拟测量噪声）
                elif random.random() > 0.7:  # 30%概率有小波动
                    noise = random.choice([-1, 0, 1])
                    self.registers[i] = current + noise
                
                # 3. 偶尔改变目标值（模拟工艺调整）
                if random.random() > 0.95:  # 5%概率调整设定值
                    # 小幅度调整目标值（±5-10）
                    adjustment = random.randint(-10, 10)
                    self.target_values[i] = max(150, min(250, target + adjustment))
                
                # 限制在合理范围
                self.registers[i] = max(0, min(1000, self.registers[i]))

def main():
    print("="*60)
    print("   Modbus TCP Device Simulator")
    print("="*60)
    
    # Create 3 devices
    devices = [
        ModbusDevice("Device-001", 5021, 6),  # Injection Machine
        ModbusDevice("Device-002", 5022, 5),  # Packaging Machine
        ModbusDevice("Device-003", 5023, 4),  # QC Device
    ]
    
    # Start all devices
    threads = []
    for device in devices:
        # Server thread
        server_thread = threading.Thread(target=device.start)
        server_thread.daemon = True
        server_thread.start()
        threads.append(server_thread)
        
        # Register update thread
        update_thread = threading.Thread(target=device.update_registers)
        update_thread.daemon = True
        update_thread.start()
        threads.append(update_thread)
        
        time.sleep(0.1)
    
    print("\nAll devices running. Press Ctrl+C to stop.")
    print("="*60)
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping devices...")
        for device in devices:
            device.stop()
        print("Done.")

if __name__ == "__main__":
    main()

