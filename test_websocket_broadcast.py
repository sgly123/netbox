#!/usr/bin/env python3
"""
WebSocket广播测试脚本
用于测试WebSocket服务器的广播功能
"""

import asyncio
import websockets
import json
import time
import threading

class WebSocketBroadcastTester:
    def __init__(self, uri="ws://localhost:8001"):
        self.uri = uri
        self.clients = []
        self.messages_received = {}
        self.test_completed = False
        
    async def client_handler(self, client_id, messages_to_send=3):
        """单个WebSocket客户端处理函数"""
        try:
            async with websockets.connect(self.uri) as websocket:
                self.clients.append(websocket)
                self.messages_received[client_id] = []
                
                print(f"客户端 {client_id} 已连接")
                
                # 发送测试消息
                for i in range(messages_to_send):
                    message = json.dumps({
                        "client_id": client_id,
                        "message": f"测试消息 {i+1}",
                        "timestamp": time.time()
                    })
                    
                    await websocket.send(message)
                    print(f"客户端 {client_id} 发送: {message}")
                    
                    # 等待接收广播的消息
                    try:
                        response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
                        self.messages_received[client_id].append(response)
                        print(f"客户端 {client_id} 收到广播: {response}")
                    except asyncio.TimeoutError:
                        print(f"客户端 {client_id} 接收消息超时")
                        
                    await asyncio.sleep(1)
                
                # 等待一段时间接收其他客户端的广播消息
                await asyncio.sleep(3)
                
        except Exception as e:
            print(f"客户端 {client_id} 错误: {e}")
        finally:
            print(f"客户端 {client_id} 断开连接")
    
    def run_test(self, num_clients=3):
        """运行广播测试"""
        print(f"开始WebSocket广播测试 - 连接 {num_clients} 个客户端到 {self.uri}")
        
        async def run_all_clients():
            tasks = []
            for i in range(num_clients):
                task = asyncio.create_task(self.client_handler(i))
                tasks.append(task)
            
            await asyncio.gather(*tasks)
            self.test_completed = True
        
        try:
            asyncio.run(run_all_clients())
        except KeyboardInterrupt:
            print("测试被用户中断")
        
        # 分析测试结果
        self.analyze_results()
    
    def analyze_results(self):
        """分析测试结果"""
        print("\n=== 测试结果分析 ===")
        
        total_expected_messages = 0
        total_received_messages = 0
        
        for client_id, messages in self.messages_received.items():
            expected = 3  # 每个客户端预期收到3条自己的消息 + 其他客户端的消息
            received = len(messages)
            total_expected_messages += expected
            total_received_messages += received
            
            print(f"客户端 {client_id}: 预期 {expected} 条消息, 实际收到 {received} 条消息")
            
            if messages:
                print(f"  收到的消息:")
                for i, msg in enumerate(messages):
                    try:
                        parsed = json.loads(msg)
                        print(f"    {i+1}. 来自客户端 {parsed.get('client_id', 'unknown')}: {parsed.get('message', 'unknown')}")
                    except:
                        print(f"    {i+1}. {msg}")
        
        success_rate = (total_received_messages / total_expected_messages * 100) if total_expected_messages > 0 else 0
        print(f"\n总体结果: 预期 {total_expected_messages} 条消息, 实际收到 {total_received_messages} 条消息")
        print(f"成功率: {success_rate:.1f}%")
        
        if success_rate < 100:
            print("⚠️  广播功能存在问题！")
        else:
            print("✅ 广播功能正常！")

if __name__ == "__main__":
    # 测试配置
    WEBSOCKET_URI = "ws://localhost:8001"  # 根据实际情况修改
    NUM_CLIENTS = 10
    
    tester = WebSocketBroadcastTester(WEBSOCKET_URI)
    tester.run_test(NUM_CLIENTS)