#!/usr/bin/env python3
"""
Mini-Redis 压力测试脚本
测试场景：SET/GET/DEL命令的并发性能
"""

import asyncio
import time
import argparse
import statistics
from dataclasses import dataclass, field
from typing import List
from datetime import datetime
import random
import string

@dataclass
class TestMetrics:
    """测试指标统计"""
    total_operations: int = 0
    successful_operations: int = 0
    failed_operations: int = 0
    set_count: int = 0
    get_count: int = 0
    del_count: int = 0
    operation_latencies: List[float] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    start_time: float = 0
    end_time: float = 0

class RedisStressTest:
    def __init__(self, host: str, port: int, num_clients: int, 
                 operations_per_client: int, key_size: int, value_size: int,
                 read_ratio: float = 0.7):
        self.host = host
        self.port = port
        self.num_clients = num_clients
        self.operations_per_client = operations_per_client
        self.key_size = key_size
        self.value_size = value_size
        self.read_ratio = read_ratio  # 读操作占比
        self.metrics = TestMetrics()
        
    def generate_random_string(self, length: int) -> str:
        """生成随机字符串"""
        return ''.join(random.choices(string.ascii_letters + string.digits, k=length))
    
    def encode_resp_command(self, *args) -> bytes:
        """将命令编码为RESP协议格式"""
        # RESP数组格式：*<参数个数>\r\n$<参数1长度>\r\n<参数1>\r\n...
        parts = [f"*{len(args)}\r\n".encode()]
        for arg in args:
            arg_bytes = str(arg).encode()
            parts.append(f"${len(arg_bytes)}\r\n".encode())
            parts.append(arg_bytes)
            parts.append(b"\r\n")
        return b"".join(parts)
    
    async def send_command(self, reader, writer, *args) -> str:
        """发送Redis命令并接收响应"""
        try:
            # 编码并发送命令
            command = self.encode_resp_command(*args)
            # print(f"[DEBUG] 发送命令: {args}, 编码: {command[:50]}")  # 调试用
            writer.write(command)
            await writer.drain()
            
            # 接收响应（读取第一行判断类型）
            response = await asyncio.wait_for(reader.readline(), timeout=5.0)
            # print(f"[DEBUG] 收到响应: {response[:50]}")  # 调试用
            
            if not response:
                raise Exception("连接已关闭或无响应")
            
            response_str = response.decode().strip()
            
            # 根据RESP协议解析响应
            if not response_str:
                raise Exception("收到空响应")
            
            first_char = response_str[0]
            if first_char == '+':  # 简单字符串
                return response_str[1:]
            elif first_char == '-':  # 错误
                raise Exception(f"Redis错误: {response_str[1:]}")
            elif first_char == ':':  # 整数
                return response_str[1:]
            elif first_char == '$':  # 批量字符串
                length = int(response_str[1:])
                if length == -1:
                    return "nil"  # nil
                data = await reader.readexactly(length + 2)  # +2 for \r\n
                return data[:-2].decode()
            elif first_char == '*':  # 数组
                return response_str  # 简化处理
            else:
                raise Exception(f"未知响应类型: {response_str[:20]}")
                
        except asyncio.TimeoutError:
            raise Exception("响应超时")
        except Exception as e:
            raise Exception(f"{type(e).__name__}: {str(e)}")
    
    async def client_worker(self, client_id: int):
        """单个客户端工作协程"""
        reader = None
        writer = None
        
        try:
            # 连接到Redis服务器
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=10.0
            )
            print(f"[客户端 {client_id}] ✅ 连接成功")
            
            # 预先生成一些key用于读取
            keys = [f"key_{client_id}_{i}" for i in range(100)]
            
            # 先执行一些SET操作，确保有数据可读
            for i in range(min(10, len(keys))):
                try:
                    key = keys[i]
                    value = self.generate_random_string(self.value_size)
                    await self.send_command(reader, writer, "SET", key, value)
                except Exception as e:
                    print(f"[客户端 {client_id}] ⚠️  初始化SET失败: {e}")
            
            # 执行操作
            for op_num in range(self.operations_per_client):
                operation_type = random.random()
                start_time = time.time()
                
                try:
                    if operation_type < self.read_ratio:
                        # GET操作
                        key = random.choice(keys)
                        response = await self.send_command(reader, writer, "GET", key)
                        self.metrics.get_count += 1
                    elif operation_type < self.read_ratio + 0.25:
                        # SET操作
                        key = random.choice(keys)
                        value = self.generate_random_string(self.value_size)
                        response = await self.send_command(reader, writer, "SET", key, value)
                        self.metrics.set_count += 1
                    else:
                        # DEL操作
                        key = random.choice(keys)
                        response = await self.send_command(reader, writer, "DEL", key)
                        self.metrics.del_count += 1
                    
                    latency = (time.time() - start_time) * 1000  # 转换为毫秒
                    self.metrics.operation_latencies.append(latency)
                    self.metrics.successful_operations += 1
                    
                    if op_num % 1000 == 0 and op_num > 0:
                        print(f"[客户端 {client_id}] 完成 {op_num} 个操作")
                
                except Exception as e:
                    self.metrics.failed_operations += 1
                    error_msg = f"客户端 {client_id} 操作失败: {str(e)}"
                    self.metrics.errors.append(error_msg)
                    if op_num < 3:  # 打印前3次错误
                        print(f"[客户端 {client_id}] ❌ {error_msg}")
            
            print(f"[客户端 {client_id}] 完成所有操作")
            
        except asyncio.TimeoutError:
            self.metrics.errors.append(f"客户端 {client_id} 连接超时")
            print(f"[客户端 {client_id}] ❌ 连接超时")
        except Exception as e:
            self.metrics.errors.append(f"客户端 {client_id} 错误: {str(e)}")
            print(f"[客户端 {client_id}] ❌ 错误: {e}")
        finally:
            if writer:
                writer.close()
                await writer.wait_closed()
    
    async def run_test(self):
        """运行压力测试"""
        print(f"\n{'='*60}")
        print(f"🚀 Mini-Redis 压力测试")
        print(f"{'='*60}")
        print(f"目标地址: {self.host}:{self.port}")
        print(f"并发客户端: {self.num_clients}")
        print(f"每客户端操作数: {self.operations_per_client}")
        print(f"Key大小: {self.key_size} 字节")
        print(f"Value大小: {self.value_size} 字节")
        print(f"读操作占比: {self.read_ratio*100:.0f}%")
        print(f"{'='*60}\n")
        
        self.metrics.start_time = time.time()
        self.metrics.total_operations = self.num_clients * self.operations_per_client
        
        # 创建所有客户端任务
        tasks = []
        for i in range(self.num_clients):
            task = asyncio.create_task(self.client_worker(i))
            tasks.append(task)
        
        # 等待所有任务完成
        try:
            await asyncio.gather(*tasks)
        except KeyboardInterrupt:
            print("\n⚠️  测试被用户中断")
        finally:
            # 确保end_time被设置
            if self.metrics.end_time == 0:
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
        
        # 操作统计
        print(f"【操作统计】")
        print(f"  总操作数: {self.metrics.total_operations}")
        print(f"  成功操作: {self.metrics.successful_operations}")
        print(f"  失败操作: {self.metrics.failed_operations}")
        success_rate = (self.metrics.successful_operations / self.metrics.total_operations * 100) if self.metrics.total_operations > 0 else 0
        print(f"  成功率: {success_rate:.2f}%")
        print(f"  SET操作: {self.metrics.set_count}")
        print(f"  GET操作: {self.metrics.get_count}")
        print(f"  DEL操作: {self.metrics.del_count}")
        print()
        
        # 性能指标
        if duration > 0:
            qps = self.metrics.successful_operations / duration
            print(f"【性能指标】")
            print(f"  QPS (每秒操作数): {qps:.2f}")
            print()
        
        # 延迟统计
        if self.metrics.operation_latencies:
            print(f"【延迟统计】")
            print(f"  平均延迟: {statistics.mean(self.metrics.operation_latencies):.2f}ms")
            print(f"  最小延迟: {min(self.metrics.operation_latencies):.2f}ms")
            print(f"  最大延迟: {max(self.metrics.operation_latencies):.2f}ms")
            print(f"  中位数延迟: {statistics.median(self.metrics.operation_latencies):.2f}ms")
            
            # 计算百分位数
            sorted_latencies = sorted(self.metrics.operation_latencies)
            p95_index = int(len(sorted_latencies) * 0.95)
            p99_index = int(len(sorted_latencies) * 0.99)
            print(f"  P95 延迟: {sorted_latencies[p95_index]:.2f}ms")
            print(f"  P99 延迟: {sorted_latencies[p99_index]:.2f}ms")
            
            if len(self.metrics.operation_latencies) > 1:
                print(f"  延迟标准差: {statistics.stdev(self.metrics.operation_latencies):.2f}ms")
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
        if self.metrics.operation_latencies:
            avg_latency = statistics.mean(self.metrics.operation_latencies)
            if avg_latency < 1:
                print("✅ 性能评估: 优秀 (平均延迟 < 1ms)")
            elif avg_latency < 5:
                print("✅ 性能评估: 良好 (平均延迟 < 5ms)")
            elif avg_latency < 10:
                print("⚠️  性能评估: 一般 (平均延迟 < 10ms)")
            else:
                print("❌ 性能评估: 较差 (平均延迟 >= 10ms)")

def main():
    parser = argparse.ArgumentParser(description='Mini-Redis 压力测试工具')
    parser.add_argument('--host', default='localhost',
                        help='Redis 服务器地址 (默认: localhost)')
    parser.add_argument('--port', type=int, default=6380,
                        help='Redis 服务器端口 (默认: 6379)')
    parser.add_argument('-c', '--clients', type=int, default=50,
                        help='并发客户端数量 (默认: 50)')
    parser.add_argument('-n', '--operations', type=int, default=1000,
                        help='每个客户端的操作数 (默认: 1000)')
    parser.add_argument('--key-size', type=int, default=10,
                        help='Key大小(字节) (默认: 10)')
    parser.add_argument('--value-size', type=int, default=100,
                        help='Value大小(字节) (默认: 100)')
    parser.add_argument('--read-ratio', type=float, default=0.7,
                        help='读操作占比 (0.0-1.0, 默认: 0.7)')
    
    args = parser.parse_args()
    
    # 创建测试实例
    test = RedisStressTest(
        host=args.host,
        port=args.port,
        num_clients=args.clients,
        operations_per_client=args.operations,
        key_size=args.key_size,
        value_size=args.value_size,
        read_ratio=args.read_ratio
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
