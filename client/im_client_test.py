#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
即时通讯客户端测试程序
支持登录、注册、发送消息、添加好友等功能
"""

import socket
import struct
import json
import threading
import time
from enum import IntEnum

class MessageType(IntEnum):
    """消息类型枚举"""
    LOGIN_REQUEST = 0x0101
    LOGIN_RESPONSE = 0x0102
    LOGOUT_REQUEST = 0x0103
    LOGOUT_RESPONSE = 0x0104
    REGISTER_REQUEST = 0x0105
    REGISTER_RESPONSE = 0x0106
    
    FRIEND_LIST_REQUEST = 0x0201
    FRIEND_LIST_RESPONSE = 0x0202
    ADD_FRIEND_REQUEST = 0x0203
    ADD_FRIEND_RESPONSE = 0x0204
    
    CHAT_MESSAGE = 0x0301
    CHAT_MESSAGE_ACK = 0x0302
    
    CREATE_GROUP_REQUEST = 0x0401
    CREATE_GROUP_RESPONSE = 0x0402
    GROUP_MESSAGE = 0x0405
    GROUP_MESSAGE_ACK = 0x0406
    
    FILE_UPLOAD_REQUEST = 0x0501
    FILE_UPLOAD_RESPONSE = 0x0502
    
    HEARTBEAT = 0x0601
    USER_STATUS_CHANGE = 0x0602
    
    ERROR_RESPONSE = 0xFFFF

class IMClient:
    """即时通讯客户端"""
    
    PROTOCOL_MAGIC = 0x494D4348  # "IMCH"
    HEARTBEAT_MAGIC = 0xFAFBFCFD  # 心跳包魔数
    
    def __init__(self, host='127.0.0.1', port=9000):
        self.host = host
        self.port = port
        self.sock = None
        self.running = False
        self.user_id = None
        self.username = None
        self.receive_thread = None
        
    def connect(self):
        """连接到服务器"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            self.running = True
            
            # 启动接收线程
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            
            print(f"✓ 连接到服务器 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"✗ 连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
        print("✓ 已断开连接")
    
    def _pack_message(self, msg_type, payload):
        """打包消息"""
        payload_bytes = payload.encode('utf-8') if isinstance(payload, str) else payload
        
        # 协议格式: protocol_id(4) + msg_type(2) + length(4) + payload
        header = struct.pack('!IHI', 
                           self.PROTOCOL_MAGIC,  # Protocol ID
                           msg_type,              # Message Type
                           len(payload_bytes))    # Payload Length
        
        return header + payload_bytes
    
    def _unpack_message(self, data):
        """解包消息"""
        if len(data) < 10:
            return None, None, None
        
        magic, msg_type, length = struct.unpack('!IHI', data[:10])
        
        if magic != self.PROTOCOL_MAGIC:
            print(f"✗ 协议魔数错误: 0x{magic:08X}")
            return None, None, None
        
        payload = data[10:10+length]
        return msg_type, length, payload
    
    def _send_message(self, msg_type, payload_dict):
        """发送消息"""
        try:
            payload = json.dumps(payload_dict)
            packet = self._pack_message(msg_type, payload)
            self.sock.sendall(packet)
            return True
        except Exception as e:
            print(f"✗ 发送失败: {e}")
            return False
    
    def _receive_loop(self):
        """接收消息循环"""
        buffer = b''
        
        while self.running:
            try:
                data = self.sock.recv(4096)
                if not data:
                    print("✗ 服务器断开连接")
                    self.running = False
                    break
                
                buffer += data
                # print(f"[DEBUG] 收到 {len(data)} 字节，缓冲区总长度: {len(buffer)}")
                
                # 处理完整的消息包
                while len(buffer) >= 4:
                    # 先检查是否是心跳包（只有4字节）
                    if len(buffer) >= 4:
                        magic_peek = struct.unpack('!I', buffer[:4])[0]
                        if magic_peek == self.HEARTBEAT_MAGIC:
                            buffer = buffer[4:]
                            continue
                    
                    # 检查是否有完整的协议头
                    if len(buffer) < 10:
                        break
                    
                    magic, msg_type, length = struct.unpack('!IHI', buffer[:10])
                    
                    # print(f"[DEBUG] 解析头: Magic=0x{magic:08X}, Type=0x{msg_type:04X}, Length={length}")
                    
                    if magic != self.PROTOCOL_MAGIC:
                        print(f"✗ 协议错误，清空缓冲区")
                        print(f"[DEBUG] 缓冲区前32字节: {buffer[:32].hex()}")
                        buffer = b''
                        break
                    
                    total_len = 10 + length
                    if len(buffer) < total_len:
                        # 数据不完整，等待更多数据
                        print(f"[DEBUG] 数据不完整，需要 {total_len} 字节，当前 {len(buffer)} 字节")
                        break
                    
                    # 提取完整包
                    packet = buffer[:total_len]
                    buffer = buffer[total_len:]
                    
                    # print(f"[DEBUG] 处理完整包，剩余缓冲区: {len(buffer)} 字节")
                    
                    # 处理消息
                    self._handle_message(msg_type, packet[10:10+length])
                    
            except Exception as e:
                if self.running:
                    print(f"✗ 接收错误: {e}")
                    import traceback
                    traceback.print_exc()
                break
    
    def _handle_message(self, msg_type, payload):
        """处理接收到的消息"""
        try:
            data = json.loads(payload.decode('utf-8'))
            
            if msg_type == MessageType.LOGIN_RESPONSE:
                if data.get('success'):
                    self.user_id = data['user_id']
                    self.username = data['username']
                    print(f"✓ 登录成功! 用户ID: {self.user_id}, 用户名: {self.username}")
                else:
                    print(f"✗ 登录失败: {data.get('message')}")
            
            elif msg_type == MessageType.REGISTER_RESPONSE:
                if data.get('success'):
                    print(f"注册成功! 用户ID: {data['user_id']}")
                else:
                    print(f"注册失败: {data.get('message')}")
            
            elif msg_type == MessageType.FRIEND_LIST_RESPONSE:
                friends = data.get('friends', [])
                print(f"\n=== 好友列表 ({len(friends)}人) ===")
                for friend in friends:
                    status = "在线" if friend['online'] else "离线"
                    print(f"  [{friend['user_id']}] {friend['nickname']} (@{friend['username']}) - {status}")
            
            elif msg_type == MessageType.ADD_FRIEND_RESPONSE:
                if data.get('success'):
                    print(f"✓ 添加好友成功: {data['nickname']} (@{data['username']})")
                else:
                    print(f"✗ 添加好友失败: {data.get('message')}")
            
            elif msg_type == MessageType.CHAT_MESSAGE:
                print(f"\n[消息] {data['from_username']}: {data['content']}")
            
            elif msg_type == MessageType.CHAT_MESSAGE_ACK:
                if data.get('delivered'):
                    print("✓ 消息已送达")
                else:
                    print(f"⚠ 消息已存储: {data.get('message')}")
            
            elif msg_type == MessageType.GROUP_MESSAGE:
                print(f"\n[群{data['group_id']}] {data['from_username']}: {data['content']}")
            
            elif msg_type == MessageType.CREATE_GROUP_RESPONSE:
                if data.get('success'):
                    print(f"✓ 创建群组成功: {data['group_name']} (ID: {data['group_id']})")
            
            elif msg_type == MessageType.USER_STATUS_CHANGE:
                status = "上线" if data['status'] == 'online' else "下线"
                print(f"\n[状态] 用户 {data['user_id']} {status}")
            
            elif msg_type == MessageType.HEARTBEAT:
                pass  # 心跳响应，不打印
            
            elif msg_type == MessageType.ERROR_RESPONSE:
                print(f"✗ 错误: {data.get('message')}")
            
            else:
                print(f"收到未知消息类型: 0x{msg_type:04X}")
                
        except Exception as e:
            print(f"✗ 消息处理失败: {e}")
    
    def register(self, username, password, nickname=None):
        """注册新用户"""
        payload = {
            'username': username,
            'password': password,
            'nickname': nickname or username
        }
        return self._send_message(MessageType.REGISTER_REQUEST, payload)
    
    def login(self, username, password):
        """登录"""
        payload = {
            'username': username,
            'password': password
        }
        return self._send_message(MessageType.LOGIN_REQUEST, payload)
    
    def logout(self):
        """登出"""
        return self._send_message(MessageType.LOGOUT_REQUEST, {})
    
    def get_friend_list(self):
        """获取好友列表"""
        return self._send_message(MessageType.FRIEND_LIST_REQUEST, {})
    
    def add_friend(self, username):
        """添加好友"""
        payload = {'username': username}
        return self._send_message(MessageType.ADD_FRIEND_REQUEST, payload)
    
    def send_message(self, to_user_id, content):
        """发送消息"""
        payload = {
            'to_user_id': to_user_id,
            'content': content
        }
        return self._send_message(MessageType.CHAT_MESSAGE, payload)
    
    def create_group(self, group_name):
        """创建群组"""
        payload = {'group_name': group_name}
        return self._send_message(MessageType.CREATE_GROUP_REQUEST, payload)
    
    def send_group_message(self, group_id, content):
        """发送群消息"""
        payload = {
            'group_id': group_id,
            'content': content
        }
        return self._send_message(MessageType.GROUP_MESSAGE, payload)

def print_menu():
    """打印菜单"""
    print("\n" + "="*50)
    print("NetBox 即时通讯客户端")
    print("="*50)
    print("1. 注册新用户")
    print("2. 登录")
    print("3. 获取好友列表")
    print("4. 添加好友")
    print("5. 发送消息")
    print("6. 创建群组")
    print("7. 发送群消息")
    print("8. 登出")
    print("9. 退出")
    print("="*50)

def main():
    """主函数"""
    client = IMClient()
    
    if not client.connect():
        return
    
    try:
        while True:
            print_menu()
            choice = input("请选择操作 (1-9): ").strip()
            
            if choice == '1':
                username = input("用户名: ").strip()
                password = input("密码: ").strip()
                nickname = input("昵称 (可选): ").strip()
                client.register(username, password, nickname or None)
                time.sleep(0.5)
                
            elif choice == '2':
                username = input("用户名: ").strip()
                password = input("密码: ").strip()
                client.login(username, password)
                time.sleep(0.5)
                
            elif choice == '3':
                client.get_friend_list()
                time.sleep(0.5)
                
            elif choice == '4':
                username = input("好友用户名: ").strip()
                client.add_friend(username)
                time.sleep(0.5)
                
            elif choice == '5':
                to_user_id = int(input("对方用户ID: ").strip())
                content = input("消息内容: ").strip()
                client.send_message(to_user_id, content)
                time.sleep(0.5)
                
            elif choice == '6':
                group_name = input("群组名称: ").strip()
                client.create_group(group_name)
                time.sleep(0.5)
                
            elif choice == '7':
                group_id = int(input("群组ID: ").strip())
                content = input("消息内容: ").strip()
                client.send_group_message(group_id, content)
                time.sleep(0.5)
                
            elif choice == '8':
                client.logout()
                time.sleep(0.5)
                
            elif choice == '9':
                print("退出程序...")
                break
                
            else:
                print("无效选择，请重试")
                
    except KeyboardInterrupt:
        print("\n\n程序被中断")
    finally:
        client.disconnect()

if __name__ == '__main__':
    main()
