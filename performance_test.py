#!/usr/bin/env python3
"""
NetBox 性能测试脚本
测试日志优化前后的性能差异
"""

import socket
import time
import threading
import statistics
from dataclasses import dataclass
from typing import List
import argparse


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


class PerformanceTester:
    """性能测试器"""

    def __init__(self, host: str, port: int, protocol: str = "redis"):
        self.host = host
        self.port = port
        self.protocol = protocol
        self.latencies: List[float] = []
        self.lock = threading.Lock()
        self.successful_connections = 0
        self.successful_requests = 0
        self.failed_requests = 0

    def create_connection(self) -> socket.socket:
        """创建连接"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((self.host, self.port))
        return sock

    def send_redis_command(self, sock: socket.socket, command: str) -> bool:
        """发送Redis命令（RESP格式）"""
        try:
            start_time = time.time()
            # 发送RESP格式的PING命令: *1\r\n$4\r\nPING\r\n
            resp_cmd = b"*1\r\n$4\r\nPING\r\n"
            sock.sendall(resp_cmd)

            # 设置接收超时
            sock.settimeout(0.5)

            # 尝试接收响应
            response = b""
            try:
                response = sock.recv(4096)
            except socket.timeout:
                # 超时也算失败
                with self.lock:
                    self.failed_requests += 1
                return False

            if response and len(response) > 0:
                latency = (time.time() - start_time) * 1000  # 转换为毫秒
                with self.lock:
                    self.latencies.append(latency)
                    self.successful_requests += 1
                return True
            else:
                with self.lock:
                    self.failed_requests += 1
                return False

        except Exception as e:
            with self.lock:
                self.failed_requests += 1
            return False

    def send_echo_message(self, sock: socket.socket, message: str) -> bool:
        """发送Echo消息"""
        try:
            start_time = time.time()
            sock.sendall(message.encode())

            # 设置接收超时
            sock.settimeout(0.5)

            # 尝试接收响应
            response = b""
            try:
                response = sock.recv(4096)
            except socket.timeout:
                # 超时也算失败
                with self.lock:
                    self.failed_requests += 1
                return False

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

        except Exception as e:
            with self.lock:
                self.failed_requests += 1
            return False

    def worker_thread(self, num_requests: int):
        """工作线程"""
        sock = None
        try:
            sock = self.create_connection()
            with self.lock:
                self.successful_connections += 1

            for i in range(num_requests):
                if self.protocol == "redis":
                    self.send_redis_command(sock, f"PING message_{i}")
                else:
                    self.send_echo_message(sock, f"Hello {i}\n")

                # 小延迟避免过快
                time.sleep(0.001)

        except Exception as e:
            # 连接失败
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
        print(f"协议: {self.protocol.upper()}")
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

            # 每10个连接打印进度
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


def compare_results(before: TestResult, after: TestResult):
    """对比测试结果"""
    print(f"\n{'='*60}")
    print("性能对比分析")
    print(f"{'='*60}")

    qps_improvement = ((after.qps - before.qps) / before.qps * 100) if before.qps > 0 else 0
    latency_improvement = ((before.avg_latency - after.avg_latency) / before.avg_latency * 100) if before.avg_latency > 0 else 0

    print(f"\nQPS 对比:")
    print(f"  优化前: {before.qps:.2f}")
    print(f"  优化后: {after.qps:.2f}")
    print(f"  提升:   {qps_improvement:+.2f}%")

    print(f"\n平均延迟对比:")
    print(f"  优化前: {before.avg_latency:.2f} ms")
    print(f"  优化后: {after.avg_latency:.2f} ms")
    print(f"  降低:   {latency_improvement:+.2f}%")

    print(f"\nP95 延迟对比:")
    print(f"  优化前: {before.p95_latency:.2f} ms")
    print(f"  优化后: {after.p95_latency:.2f} ms")
    print(f"  降低:   {((before.p95_latency - after.p95_latency) / before.p95_latency * 100):+.2f}%")

    print(f"\n总结:")
    if qps_improvement > 0:
        print(f"  ✅ QPS 提升了 {qps_improvement:.1f}%")
    else:
        print(f"  ⚠️  QPS 下降了 {abs(qps_improvement):.1f}%")

    if latency_improvement > 0:
        print(f"  ✅ 延迟降低了 {latency_improvement:.1f}%")
    else:
        print(f"  ⚠️  延迟增加了 {abs(latency_improvement):.1f}%")

    print(f"{'='*60}\n")


def run_benchmark_suite(host: str, port: int, protocol: str):
    """运行完整的基准测试套件"""
    tester = PerformanceTester(host, port, protocol)

    test_cases = [
        (100, 10, "轻负载测试 (100连接 × 10请求)"),
        (500, 10, "中负载测试 (500连接 × 10请求)"),
        (1000, 10, "高负载测试 (1000连接 × 10请求)"),
    ]

    results = []

    for num_conn, num_req, desc in test_cases:
        print(f"\n{'#'*60}")
        print(f"# {desc}")
        print(f"{'#'*60}")

        result = tester.run_test(num_conn, num_req)
        print_result(result, desc)
        results.append((desc, result))

        # 测试间隔
        time.sleep(2)

    # 打印汇总
    print(f"\n{'='*60}")
    print("测试汇总")
    print(f"{'='*60}")
    print(f"{'测试场景':<30} {'QPS':<12} {'平均延迟':<12} {'成功率'}")
    print(f"{'-'*60}")
    for desc, result in results:
        success_rate = result.successful_requests / result.total_requests * 100
        print(f"{desc:<30} {result.qps:<12.2f} {result.avg_latency:<12.2f} {success_rate:.1f}%")
    print(f"{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(description='NetBox 性能测试工具')
    parser.add_argument('--host', default='127.0.0.1', help='服务器地址')
    parser.add_argument('--port', type=int, default=6380, help='服务器端口')
    parser.add_argument('--protocol', choices=['redis', 'echo'], default='redis', help='测试协议')
    parser.add_argument('--connections', type=int, default=1000, help='并发连接数')
    parser.add_argument('--requests', type=int, default=10, help='每连接请求数')
    parser.add_argument('--benchmark', action='store_true', help='运行完整基准测试')

    args = parser.parse_args()

    print(f"""
╔══════════════════════════════════════════════════════════╗
║          NetBox 性能测试工具 v1.0                        ║
║          Performance Testing Tool                        ║
╚══════════════════════════════════════════════════════════╝
    """)

    if args.benchmark:
        run_benchmark_suite(args.host, args.port, args.protocol)
    else:
        tester = PerformanceTester(args.host, args.port, args.protocol)
        result = tester.run_test(args.connections, args.requests)
        print_result(result)


if __name__ == "__main__":
    main()
