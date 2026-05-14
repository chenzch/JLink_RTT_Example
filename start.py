#!/usr/bin/env python3
"""
TCP 19021 端口客户端，连接 localhost:19021 并接收数据。
按 2 位十六进制（大写）输出，每行 16 个字节，字节之间用空格分隔。
按 Ctrl+C 退出。
连接后立即发送固定字符串 $$SEGGER_TELNET_ConfigStr=RTTCh;0;SetRTTAddr;0x20400080;$$
"""

import socket
import sys
import signal
import argparse

HOST = '127.0.0.1'  # 连接本地服务
PORT = 19021
BYTES_PER_LINE = 16

def format_hex_byte(b: int) -> str:
    """将单字节转换为大写的两位十六进制字符串"""
    return f"{b:02X}"

def main():
    # 处理命令行参数
    fixed_bytes = f"$$SEGGER_TELNET_ConfigStr=RTTCh;0;SetRTTAddr;0$$".encode("utf-8")

    # 捕获 SIGINT (Ctrl+C) 以优雅退出
    signal.signal(signal.SIGINT, lambda signum, frame: sys.exit(0))

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.connect((HOST, PORT))
            print(f"[*] 已连接到 {HOST}:{PORT}")

            # 连接后立即发送固定字符串
            sock.sendall(fixed_bytes)
            print("[*] 已发送固定字符串数据包")
        except ConnectionRefusedError:
            print(f"[!] 无法连接到 {HOST}:{PORT}，请确保服务已启动。")
            return

        try:
            recv_count = 0
            while recv_count < 2:
                data = sock.recv(1024)  # 最多接收1KB
                if not data:
                    print("[*] 连接已关闭")
                    break
                
                recv_count += 1
                print(f"[*] 收到第 {recv_count} 次数据:")

                # 输出接收到的数据
                for i in range(0, len(data), BYTES_PER_LINE):
                    line_bytes = data[i:i + BYTES_PER_LINE]
                    line_hex = " ".join(format_hex_byte(b) for b in line_bytes)
                    print(line_hex)
            
            if recv_count >= 2:
                print("[*] 已接收 2 次数据，程序退出。")
        except KeyboardInterrupt:
            print("\n[*] 程序已退出。")
        except Exception as e:
            print(f"[!] 异常: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
