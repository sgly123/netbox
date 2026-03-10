#!/usr/bin/env python3
"""
WebSocket 性能测试脚本
测试WebSocket服务器的并发性能
"""

import socket
import time
import threading
import statistics
import hashlib
import base64
from dataclasses import dataclass
from typing import List
import argparse
import struct
import os


@dataclass
class TestResult:
    """测试结果"""
    total_connections: int
    successful_connections: int
    total_requests: int
    successful_requests: int
    total_time: float
    qps: float
    avg_latency: float
    p95_latency: float
    p99_latency: float
    max_latency: float
    failed_requests: int


class WebSocketTester:
    """WebSocket性能测试器"""

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.latencies: List[float] = []
        self.lock = threading.Lock()
        self.successful_connections = 0
        self.successful_requests = 0
        self.failed_requests = 0

    def create_websocket_handshake(self) -> bytes:
        """创建WebSocket握手请求"""
        key = base64.b64encode(os.urandom(16)).decode('utf-8')

        handshake = (
            f"GET / HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n"
            f"\r\n"
        )
        return handshake.encode('utf-8')

    def create_websocket_frame(self, message: str) -> bytes:
        """创建WebSocket数据帧（文本帧，带掩码）"""
        payload = message.encode('utf-8')
        payload_len = len(payload)

        # 第一个字节: FIN=1, opcode=1 (文本帧)
        frame = bytearray([0x81])

        # 第二个字节: MASK=1, payload长度
        if payload_len <= 125:
            frame.append(0x80 | payload_len)
        elif payload_len <= 65535:
            frame.append(0x80 | 126)
            frame.extend(struct.pack('>H', payload_len))
        else:
            frame.append(0x80 | 127)
            frame.extend(struct.pack('>Q', payload_len))

        # 掩码密钥（4字节）
        mask_key = os.urandom(4)
        frame.extend(mask_key)

        # 应用掩码到payload
        masked_payload = bytearray(payload)
        for i in range(len(masked_payload)):
            masked_payload[i] ^= mask_key[i % 4]

        frame.extend(masked_payload)
        return bytes(frame)

    def parse_websocket_frame(self, data: bytes) -> str:
        """解析WebSocket数据帧"""
        if len(data) < 2:
            return ""

        # 跳过第一个字节（FIN + opcode）
        payload_len = data[1] & 0x7F

        offset = 2
        if payload_len == 126:
            if len(data) < 4:
                return ""
            payload_len = struct.unpack('>H', data[2:4])[0]
            offset = 4
        elif payload_len == 127:
            if len(data) < 10:
                return ""
            payload_len = struct.unpack('>Q', data[2:10])[0]
            offset = 10

        # 检查是否有足够的数据
        if len(data) < offset + payload_len:
            return ""

        payload = data[offset:offset + payload_len]
        return payload.decode('utf-8', errors='ignore')

    def websocket_handshake(self, sock: socket.socket) -> bool:
        """执行WebSocket握手"""
        try:
            # 发送握手请求
            handshake = self.create_websocket_handshake()
            sock.sendall(handshake)

            # 接收握手响应
            sock.settimeout(2)
            response = sock.recv(4096)

            # 检查是否升级成功
            if b"101 Switching Protocols" in response or b"101" in response:
                return True
            return False
        except Exception as e:
            return False

    def send_websocket_message(self, sock: socket.socket, message: str) -> bool:
        """发送WebSocket消息并接收响应"""
        try:
            start_time = time.time()

            # 发送WebSocket帧
            frame = self.create_websocket_frame(message)
            sock.sendall(frame)

            # 接收响应
            sock.settimeout(0.5)
            response = sock.recv(4096)

            if response and len(response) > 0:
                latency = (time.time() - start_time) * 1000
                with self.lock:
                    self.latencies.append(latency)
                    self.successful_requests += 1
                return True
            else:
                with self.lock:
                    self.failed_requests += 1
                return False

        except socket.timeout:
            with self.lock:
                self.failed_requests += 1
            return False
        except Exception as e:
            with self.lock:
                self.failed_requests += 1
            return False

    def worker_thread(self, num_requests: int):
        """工作线程"""
        sock = None
        try:
            # 创建连接
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect((self.host, self.port))

            # WebSocket握手
            if not self.websocket_handshake(sock):
                return

            with self.lock:
                self.successful_connections += 1

            # 发送消息
            for i in range(num_requests):
                self.send_websocket_message(sock, f"Hello WebSocket {i}")
                time.sleep(0.001)

        except Exception as e:
            pass
        finally:
            if sock:
                try:
                    sock.close()
                except:
                    pass

    def run_test(self, num_connections: int, requests_per_connection: int) -> TestResult:
        """运行测试"""
        print(f"\n{'='*60}")
        print(f"开始测试: {num_connections} 连接 × {requests_per_connection} 请求")
        print(f"协议: WebSocket")
        print(f"目标: {self.host}:{self.port}")
        print(f"{'='*60}\n")

        # 重置统计
        self.latencies = []
        self.successful_connections = 0
        self.successful_requests = 0
        self.failed_requests = 0

        # 创建线程
        threads = []
        start_time = time.time()

        for i in range(num_connections):
            thread = threading.Thread(
                target=self.worker_thread,
                args=(requests_per_connection,)
            )
            threads.append(thread)
            thread.start()

            if (i + 1) % 10 == 0:
                print(f"已启动 {i + 1}/{num_connections} 个连接...")

        # 等待所有线程完成
        for thread in threads:
            thread.join()

        total_time = time.time() - start_time

        # 计算统计数据
        total_requests = num_connections * requests_per_connection
        qps = self.successful_requests / total_time if total_time > 0 else 0

        if self.latencies:
            avg_latency = statistics.mean(self.latencies)
            sorted_latencies = sorted(self.latencies)
            p95_latency = sorted_latencies[int(len(sorted_latencies) * 0.95)]
            p99_latency = sorted_latencies[int(len(sorted_latencies) * 0.99)]
            max_latency = max(self.latencies)
        else:
            avg_latency = p95_latency = p99_latency = max_latency = 0

        return TestResult(
            total_connections=num_connections,
            successful_connections=self.successful_connections,
            total_requests=total_requests,
            successful_requests=self.successful_requests,
            total_time=total_time,
            qps=qps,
            avg_latency=avg_latency,
            p95_latency=p95_latency,
            p99_latency=p99_latency,
            max_latency=max_latency,
            failed_requests=self.failed_requests
        )


def print_result(result: TestResult, title: str = "测试结果"):
    """打印测试结果"""
    print(f"\n{'='*60}")
    print(f"{title}")
    print(f"{'='*60}")
    print(f"连接统计:")
    print(f"  总连接数:     {result.total_connections}")
    print(f"  成功连接数:   {result.successful_connections}")
    print(f"  连接成功率:   {result.successful_connections/result.total_connections*100:.2f}%")
    print(f"\n请求统计:")
    print(f"  总请求数:     {result.total_requests}")
    print(f"  成功请求数:   {result.successful_requests}")
    print(f"  失败请求数:   {result.failed_requests}")
    print(f"  请求成功率:   {result.successful_requests/result.total_requests*100:.2f}%")
    print(f"\n性能指标:")
    print(f"  总耗时:       {result.total_time:.2f} 秒")
    print(f"  QPS:          {result.qps:.2f}")
    print(f"  平均延迟:     {result.avg_latency:.2f} ms")
    print(f"  P95 延迟:     {result.p95_latency:.2f} ms")
    print(f"  P99 延迟:     {result.p99_latency:.2f} ms")
    print(f"  最大延迟:     {result.max_latency:.2f} ms")
    print(f"{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(description='WebSocket 性能测试工具')
    parser.add_argument('--host', default='127.0.0.1', help='服务器地址')
    parser.add_argument('--port', type=int, default=8001, help='服务器端口')
    parser.add_argument('--connections', type=int, default=1000, help='并发连接数')
    parser.add_argument('--requests', type=int, default=10, help='每连接请求数')

    args = parser.parse_args()

    print(f"""
╔══════════════════════════════════════════════════════════╗
║          WebSocket 性能测试工具 v1.0                     ║
║          Performance Testing Tool                        ║
╚══════════════════════════════════════════════════════════╝
    """)

    tester = WebSocketTester(args.host, args.port)
    result = tester.run_test(args.connections, args.requests)
    print_result(result)


if __name__ == "__main__":
    main()
