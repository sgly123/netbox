#!/usr/bin/env python3
"""调试RESP编码"""

def encode_resp_command(*args):
    """将命令编码为RESP协议格式"""
    parts = [f"*{len(args)}\r\n".encode()]
    for arg in args:
        arg_bytes = str(arg).encode()
        parts.append(f"${len(arg_bytes)}\r\n".encode())
        parts.append(arg_bytes)
        parts.append(b"\r\n")
    return b"".join(parts)

# 测试编码
commands = [
    ("PING",),
    ("SET", "foo", "bar"),
    ("GET", "foo"),
]

for cmd in commands:
    encoded = encode_resp_command(*cmd)
    print(f"\n命令: {cmd}")
    print(f"编码: {encoded}")
    print(f"十六进制: {encoded.hex()}")
    print(f"可读: {repr(encoded)}")
