#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sanoTTS 串口语音合成交互工具
用法:
    python3 say.py "你好，欢迎使用涂鸦智能语音。"
    python3 say.py -i                # 进入交互式命令行
    python3 say.py --g2p "测试文本"   # 仅测试板端音素转换 (毫秒级响应)
"""

import sys
import os
import time
import json
import argparse
import serial

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200

def send_say_command(ser, text, req_id, is_g2p_only=False, timeout=180.0):
    cmd_verb = "G2P" if is_g2p_only else "SAY"
    cmd_str = f"{cmd_verb}\t{req_id}\t{text}\n"
    
    ser.reset_input_buffer()
    ser.write(cmd_str.encode("utf-8"))
    print(f">> 发送指令: {cmd_verb} (ID: {req_id}) -> \"{text}\"")

    t0 = time.time()
    while time.time() - t0 < timeout:
        line = ser.readline()
        if not line:
            continue
        try:
            line_str = line.decode("utf-8", errors="replace").strip()
            if not line_str.startswith("@SNT\t"):
                continue
            evt = json.loads(line_str[5:])
            if evt.get("id") != req_id:
                continue

            event_type = evt.get("event")
            if event_type == "accepted":
                print(f"   [接受] 板端已接收请求 (声部: {evt.get('voice')})")
            elif event_type == "phonemes":
                n_ph = evt.get("n_phonemes", 0)
                print(f"   [音素] 板端 G2P 转换完成: 共 {n_ph} 个音素 ID")
            elif event_type == "synthesizing":
                print(f"   [合成] sanoTTS 神经模型推理中 (请稍候)...")
            elif event_type == "playing":
                print(f"   [播放] 板载扬声器开始物理发声...")
            elif event_type == "done":
                samples = evt.get("samples", 0)
                sr = evt.get("source_rate", 22050)
                comp_s = evt.get("compute_us", 0) / 1000000.0
                wall_s = evt.get("wall_us", 0) / 1000000.0
                underruns = evt.get("underruns", 0)
                dur_s = samples / sr if sr > 0 else 0
                print(f"   [完成] 播放结束！音频时长: {dur_s:.3f}秒 ({samples} 采样点), 耗时: {wall_s:.2f}秒, 欠载: {underruns}次")
                return True
            elif event_type == "error":
                print(f"   [错误] 板端返回错误: code={evt.get('code')}, msg={evt.get('msg')}")
                return False
            elif event_type == "cancelled":
                print(f"   [取消] 请求已被中止")
                return False
        except Exception as e:
            pass

    print(f"   [超时] 等待板端响应超时 ({timeout}秒)")
    return False

def main():
    parser = argparse.ArgumentParser(description="sanoTTS 文本朗读工具")
    parser.add_argument("text", nargs="?", default="", help="待朗读的 UTF-8 中文文本")
    parser.add_argument("-p", "--port", default=DEFAULT_PORT, help=f"串口路径 (默认: {DEFAULT_PORT})")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD, help=f"波特率 (默认: {DEFAULT_BAUD})")
    parser.add_argument("-i", "--interactive", action="store_true", help="进入交互模式")
    parser.add_argument("-g", "--g2p", action="store_true", help="仅进行 G2P 音素转换测试 (不合成声音)")
    parser.add_argument("-t", "--timeout", type=float, default=180.0, help="单次请求超时时间 (秒)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"[错误] 无法打开串口 {args.port}: {e}")
        print("提示: 请确认板卡已通过 USB 连接且没有其他程序占用该串口。")
        sys.exit(1)

    print(f"已连接到板卡串口: {args.port} (波特率 {args.baud})")
    req_id = int(time.time()) % 100000 + 1000

    try:
        if args.interactive:
            print("\n=== 进入 sanoTTS 交互模式 (输入 'q' 或按 Ctrl+C 退出) ===")
            while True:
                try:
                    text = input("\n请输入要朗读的文本 > ").strip()
                except EOFError:
                    break
                if not text:
                    continue
                if text.lower() in ("q", "quit", "exit"):
                    break
                req_id += 1
                send_say_command(ser, text, req_id, is_g2p_only=args.g2p, timeout=args.timeout)
        else:
            if not args.text:
                print("未提供文本。示例用法:")
                print("  python3 examples/multimedia/sanotts_serial/tools/say.py \"你好，欢迎使用涂鸦智能语音。\"")
                print("  python3 examples/multimedia/sanotts_serial/tools/say.py -i")
                sys.exit(0)
            req_id += 1
            send_say_command(ser, args.text, req_id, is_g2p_only=args.g2p, timeout=args.timeout)
    except KeyboardInterrupt:
        print("\n用户中断。正在向板端发送 STOP 指令...")
        ser.write(f"STOP\t{req_id + 99}\n".encode("utf-8"))
        time.sleep(0.3)
    finally:
        ser.close()

if __name__ == "__main__":
    main()
