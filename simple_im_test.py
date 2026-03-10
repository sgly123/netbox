#!/usr/bin/env python3
import socket
import struct
import json
import time

PROTOCOL_MAGIC = 0x494D4348
REGISTER_REQUEST = 0x0105
REGISTER_RESPONSE = 0x0106

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 9000))
print("Connected")

# Send register request
payload = json.dumps({"username": "test123", "password": "123", "nickname": "Test"})
packet = struct.pack('!IHI', PROTOCOL_MAGIC, REGISTER_REQUEST, len(payload)) + payload.encode()
sock.sendall(packet)
print(f"Sent {len(packet)} bytes")

# Receive response
time.sleep(0.5)
data = sock.recv(4096)
print(f"Received {len(data)} bytes: {data.hex()}")

if len(data) >= 10:
    magic, msg_type, length = struct.unpack('!IHI', data[:10])
    print(f"Magic: 0x{magic:08X}")
    print(f"Type: 0x{msg_type:04X}")
    print(f"Length: {length}")
    
    if magic == PROTOCOL_MAGIC and msg_type == REGISTER_RESPONSE:
        payload = data[10:10+length].decode()
        print(f"Payload: {payload}")
        print("SUCCESS!")
    else:
        print("FAILED - Wrong magic or type")
else:
    print("FAILED - Not enough data")

sock.close()
