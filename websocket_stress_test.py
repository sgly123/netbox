#!/usr/bin/env python3
"""
WebSocket 压力测试脚本
支持多种压测场景：连接数、消息频率、消息大小、持续时间
"""

import asyncio
import websockets
import json
import time
import argparse
import statistics
from dataclasses import dataclass, field
from typing import List, Dict
from datetime import datetime
import sys

@dataclass
class TestMetrics:
    """测试指标统计"""
    total_connections: int = 0
    successful_connections: int = 0
    failed_connections: int = 0
    total_messages_sent: int = 0
    total_messages_received: int = 0
    total_bytes_sent: int = 0
    total_bytes_received: int = 0
    connection_times: List[float] = field(default_factory=list)
    message_latencies: List[float] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    start_time: float = 0
    end_time: float = 0

class WebSocketStressTest:
    def __init__(self, uri: str, num_clients: int, messages_per_client: int, 
                 message_size: int, duration: int, interval: float, 
                 connection_rate: int = 10, connect_timeout: int = 10, max_retries: int = 3):
        self.uri = uri
        self.num_clients = num_clients
        self.messages_per_client = messages_per_client
        self.message_size = message_size
        self.duration = duration
        self.interval = interval
        self.connection_rate = connection_rate  # 每秒建立的连接数
        self.connect_timeout = connect_timeout  # 连接超时时间（秒）
        self.max_retries = max_retries  # 最大重试次数
        self.metrics = TestMetrics()
        self.running = True
        self.active_connections = 0
        self.connection_lock = asyncio.Lock()
        
    async def client_worker(self, client_id: int):
        """单个客户端工作协程"""
        websocket = None
        retry_count = 0
        
        # 连接重试逻辑
        while retry_count <= self.max_retries:
            try:
                # 测量连接时间
                connect_start = time.time()
                websocket = await asyncio.wait_for(
                    websockets.connect(self.uri),
                    timeout=self.connect_timeout
                )
                connect_time = time.time() - connect_start
                
                async with self.connection_lock:
                    self.metrics.connection_times.append(connect_time)
                    self.metrics.successful_connections += 1
                    self.active_connections += 1
                
                print(f"[客户端 {client_id}] ✅ 连接成功 (耗时: {connect_time*1000:.2f}ms, 活跃: {self.active_connections})")
                break  # 连接成功，跳出重试循环
                
            except asyncio.TimeoutError:
                retry_count += 1
                if retry_count <= self.max_retries:
                    wait_time = retry_count * 2  # 指数退避
                    print(f"[客户端 {client_id}] ⚠️  连接超时，{wait_time}秒后重试 ({retry_count}/{self.max_retries})")
                    await asyncio.sleep(wait_time)
                else:
                    async with self.connection_lock:
                        self.metrics.failed_connections += 1
                        self.metrics.errors.append(f"客户端 {client_id} 错误: 连接超时（已重试{self.max_retries}次）")
                    print(f"[客户端 {client_id}] ❌ 连接失败: 超时")
                    return
            except Exception as e:
                retry_count += 1
                if retry_count <= self.max_retries:
                    wait_time = retry_count * 2
                    print(f"[客户端 {client_id}] ⚠️  连接错误: {e}，{wait_time}秒后重试 ({retry_count}/{self.max_retries})")
                    await asyncio.sleep(wait_time)
                else:
                    async with self.connection_lock:
                        self.metrics.failed_connections += 1
                        self.metrics.errors.append(f"客户端 {client_id} 错误: {str(e)}")
                    print(f"[客户端 {client_id}] ❌ 连接失败: {e}")
                    return
        
        if not websocket:
            return
        
        try:
            
            # 发送消息
            message_count = 0
            start_time = time.time()
            
            while self.running:
                # 检查是否达到消息数量限制
                if self.messages_per_client > 0 and message_count >= self.messages_per_client:
                    break
                    
                # 检查是否超过持续时间
                if self.duration > 0 and (time.time() - start_time) >= self.duration:
                    break
                
                # 构造测试消息
                payload = "X" * self.message_size
                message = json.dumps({
                    "client_id": client_id,
                    "seq": message_count,
                    "timestamp": time.time(),
                    "payload": payload
                })
                
                # 发送消息并测量延迟
                send_time = time.time()
                await websocket.send(message)
                
                async with self.connection_lock:
                    self.metrics.total_messages_sent += 1
                    self.metrics.total_bytes_sent += len(message)
                
                # 接收响应
                try:
                    response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
                    recv_time = time.time()
                    latency = (recv_time - send_time) * 1000  # 转换为毫秒
                    
                    async with self.connection_lock:
                        self.metrics.message_latencies.append(latency)
                        self.metrics.total_messages_received += 1
                        self.metrics.total_bytes_received += len(response)
                    
                    if message_count % 100 == 0:
                        print(f"[客户端 {client_id}] 发送 {message_count} 条消息, 延迟: {latency:.2f}ms")
                    
                except asyncio.TimeoutError:
                    async with self.connection_lock:
                        self.metrics.errors.append(f"客户端 {client_id} 接收超时")
                    print(f"[客户端 {client_id}] ⚠️  接收超时")
                
                message_count += 1
                
                # 控制发送频率
                if self.interval > 0:
                    await asyncio.sleep(self.interval)
            
            print(f"[客户端 {client_id}] 完成测试，共发送 {message_count} 条消息")
            
        except Exception as e:
            async with self.connection_lock:
                self.metrics.errors.append(f"客户端 {client_id} 运行错误: {str(e)}")
            print(f"[客户端 {client_id}] ❌ 运行错误: {e}")
        finally:
            if websocket:
                try:
                    await websocket.close()
                except:
                    pass
            async with self.connection_lock:
                self.active_connections -= 1
    
    async def run_test(self):
        """运行压力测试"""
        print(f"\n{'='*60}")
        print(f"🚀 WebSocket 压力测试")
        print(f"{'='*60}")
        print(f"目标地址: {self.uri}")
        print(f"并发客户端: {self.num_clients}")
        print(f"每客户端消息数: {self.messages_per_client if self.messages_per_client > 0 else '无限制'}")
        print(f"消息大小: {self.message_size} 字节")
        print(f"测试时长: {self.duration if self.duration > 0 else '无限制'} 秒")
        print(f"发送间隔: {self.interval} 秒")
        print(f"连接速率: {self.connection_rate} 连接/秒")
        print(f"连接超时: {self.connect_timeout} 秒")
        print(f"最大重试: {self.max_retries} 次")
        print(f"{'='*60}\n")
        
        self.metrics.start_time = time.time()
        self.metrics.total_connections = self.num_clients
        
        # 创建所有客户端任务，控制连接速率
        tasks = []
        
        if self.connection_rate > 0:
            # 有速率限制：分批启动
            batch_size = self.connection_rate
            delay_between_batches = 1.0  # 每批之间延迟1秒
            
            for i in range(self.num_clients):
                task = asyncio.create_task(self.client_worker(i))
                tasks.append(task)
                
                # 每批连接后暂停
                if (i + 1) % batch_size == 0 and i < self.num_clients - 1:
                    print(f"📊 已启动 {i + 1}/{self.num_clients} 个客户端，暂停 {delay_between_batches}秒...")
                    await asyncio.sleep(delay_between_batches)
        else:
            # 无速率限制：全部同时启动
            print(f"🚀 同时启动所有 {self.num_clients} 个客户端...")
            for i in range(self.num_clients):
                task = asyncio.create_task(self.client_worker(i))
                tasks.append(task)
        
        # 等待所有任务完成
        try:
            await asyncio.gather(*tasks)
        except KeyboardInterrupt:
            print("\n⚠️  测试被用户中断")
            self.running = False
            # 等待所有任务清理
            await asyncio.gather(*tasks, return_exceptions=True)
        
        self.metrics.end_time = time.time()
    
    def print_report(self):
        """打印测试报告"""
        duration = self.metrics.end_time - self.metrics.start_time
        
        print(f"\n{'='*60}")
        print(f"📊 测试报告")
        print(f"{'='*60}")
        print(f"测试时间: {datetime.fromtimestamp(self.metrics.start_time).strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"总耗时: {duration:.2f} 秒")
        print()
        
        # 连接统计
        print(f"【连接统计】")
        print(f"  总连接数: {self.metrics.total_connections}")
        print(f"  成功连接: {self.metrics.successful_connections}")
        print(f"  失败连接: {self.metrics.failed_connections}")
        success_rate = (self.metrics.successful_connections / self.metrics.total_connections * 100) if self.metrics.total_connections > 0 else 0
        print(f"  成功率: {success_rate:.2f}%")
        
        if self.metrics.connection_times:
            print(f"  平均连接时间: {statistics.mean(self.metrics.connection_times)*1000:.2f}ms")
            print(f"  最小连接时间: {min(self.metrics.connection_times)*1000:.2f}ms")
            print(f"  最大连接时间: {max(self.metrics.connection_times)*1000:.2f}ms")
        print()
        
        # 消息统计
        print(f"【消息统计】")
        print(f"  发送消息数: {self.metrics.total_messages_sent}")
        print(f"  接收消息数: {self.metrics.total_messages_received}")
        print(f"  发送字节数: {self.metrics.total_bytes_sent:,} ({self.metrics.total_bytes_sent/1024/1024:.2f} MB)")
        print(f"  接收字节数: {self.metrics.total_bytes_received:,} ({self.metrics.total_bytes_received/1024/1024:.2f} MB)")
        
        if duration > 0:
            qps = self.metrics.total_messages_sent / duration
            throughput_mb = (self.metrics.total_bytes_sent / 1024 / 1024) / duration
            print(f"  QPS (每秒消息数): {qps:.2f}")
            print(f"  吞吐量: {throughput_mb:.2f} MB/s")
        print()
        
        # 延迟统计
        if self.metrics.message_latencies:
            print(f"【延迟统计】")
            print(f"  平均延迟: {statistics.mean(self.metrics.message_latencies):.2f}ms")
            print(f"  最小延迟: {min(self.metrics.message_latencies):.2f}ms")
            print(f"  最大延迟: {max(self.metrics.message_latencies):.2f}ms")
            print(f"  中位数延迟: {statistics.median(self.metrics.message_latencies):.2f}ms")
            
            # 计算百分位数
            sorted_latencies = sorted(self.metrics.message_latencies)
            p95_index = int(len(sorted_latencies) * 0.95)
            p99_index = int(len(sorted_latencies) * 0.99)
            print(f"  P95 延迟: {sorted_latencies[p95_index]:.2f}ms")
            print(f"  P99 延迟: {sorted_latencies[p99_index]:.2f}ms")
            
            if len(self.metrics.message_latencies) > 1:
                print(f"  延迟标准差: {statistics.stdev(self.metrics.message_latencies):.2f}ms")
        print()
        
        # 错误统计
        if self.metrics.errors:
            print(f"【错误统计】")
            print(f"  错误总数: {len(self.metrics.errors)}")
            print(f"  前10个错误:")
            for i, error in enumerate(self.metrics.errors[:10], 1):
                print(f"    {i}. {error}")
            if len(self.metrics.errors) > 10:
                print(f"    ... 还有 {len(self.metrics.errors) - 10} 个错误")
        
        print(f"{'='*60}\n")
        
        # 性能评估
        if self.metrics.message_latencies:
            avg_latency = statistics.mean(self.metrics.message_latencies)
            if avg_latency < 1:
                print("✅ 性能评估: 优秀 (平均延迟 < 1ms)")
            elif avg_latency < 10:
                print("✅ 性能评估: 良好 (平均延迟 < 10ms)")
            elif avg_latency < 50:
                print("⚠️  性能评估: 一般 (平均延迟 < 50ms)")
            else:
                print("❌ 性能评估: 较差 (平均延迟 >= 50ms)")

def main():
    parser = argparse.ArgumentParser(description='WebSocket 压力测试工具')
    parser.add_argument('--uri', default='ws://localhost:8001', 
                        help='WebSocket 服务器地址 (默认: ws://localhost:8001)')
    parser.add_argument('-c', '--clients', type=int, default=100,
                        help='并发客户端数量 (默认: 100)')
    parser.add_argument('-n', '--messages', type=int, default=100,
                        help='每个客户端发送的消息数 (0表示无限制, 默认: 100)')
    parser.add_argument('-s', '--size', type=int, default=100,
                        help='消息大小(字节) (默认: 100)')
    parser.add_argument('-d', '--duration', type=int, default=0,
                        help='测试持续时间(秒) (0表示无限制, 默认: 0)')
    parser.add_argument('-i', '--interval', type=float, default=0,
                        help='消息发送间隔(秒) (默认: 0)')
    parser.add_argument('-r', '--rate', type=int, default=10,
                        help='连接速率(连接/秒) (0表示无限制, 默认: 10)')
    parser.add_argument('-t', '--timeout', type=int, default=10,
                        help='连接超时时间(秒) (默认: 10)')
    parser.add_argument('--retries', type=int, default=3,
                        help='连接失败最大重试次数 (默认: 3)')
    
    args = parser.parse_args()
    
    # 创建测试实例
    test = WebSocketStressTest(
        uri=args.uri,
        num_clients=args.clients,
        messages_per_client=args.messages,
        message_size=args.size,
        duration=args.duration,
        interval=args.interval,
        connection_rate=args.rate,
        connect_timeout=args.timeout,
        max_retries=args.retries
    )
    
    # 运行测试
    try:
        asyncio.run(test.run_test())
    except KeyboardInterrupt:
        print("\n测试被中断")
    
    # 打印报告
    test.print_report()

if __name__ == "__main__":
    main()
