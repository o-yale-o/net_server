# -*- coding: utf-8 -*-
"""
功能冒烟测试：验证协议解析与恶意包防御
覆盖: 正常心跳 / 包长下溢 / 超大包长 / 坏CRC / 攻击后服务器存活

用法:
    cd test
    python3 test_smoke.py [主机] [端口]
    (默认 127.0.0.1 18080，需先用 ./run_server_for_test.sh 18080 启动测试服务器)

预期输出: 每个用例一行 PASS/FAIL，全部 PASS 退出码为 0
"""
import socket, struct, sys

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080
ADDR = (HOST, PORT)

def ping_pkt():
    """合法心跳包: 只有包头(8字节)，无包体的包CRC固定填0"""
    return struct.pack('>HHI', 8, 0, 0)

def send_and_recv(payload, expect_reply=True):
    """连接、发送payload、按需收包；返回(replied, reply_data)"""
    c = socket.create_connection(ADDR, timeout=3)
    c.sendall(payload)
    data = b''
    if expect_reply:
        try:
            data = c.recv(1024)
        except socket.timeout:
            pass
    else:
        try:
            c.settimeout(2)
            data = c.recv(1024)  # 不期望回包，收到即异常
        except socket.timeout:
            pass
    c.close()
    return (len(data) > 0, data)

def t_normal():
    replied, d = send_and_recv(ping_pkt(), True)
    if not replied:
        return False
    pkglen, msgcode, crc = struct.unpack('>HHI', d[:8])
    return pkglen == 8 and msgcode == 0

def t_bad_len():
    # 包长字段(1)小于包头长(8): 触发长度下溢防御，应被静默丢弃且服务器不崩
    replied, _ = send_and_recv(struct.pack('>HHI', 1, 0, 0), False)
    return not replied

def t_huge_len():
    # 包长字段(60000)超过协议上限(_PKG_MAX_LENGTH): 应被丢弃
    replied, _ = send_and_recv(struct.pack('>HHI', 60000, 0, 0), False)
    return not replied

def t_bad_crc():
    # 带包体但CRC错误: 应被CRC校验丢弃
    body = b'A' * 100
    pkt = struct.pack('>HHI', 8 + len(body), 5, 0xDEADBEEF) + body
    replied, _ = send_and_recv(pkt, False)
    return not replied

def t_alive_after():
    # 经历以上恶意包后，服务器必须仍然正常服务
    replied, d = send_and_recv(ping_pkt(), True)
    return replied and len(d) >= 8

TESTS = [
    ('正常心跳收发',        t_normal),
    ('包长下溢防御',        t_bad_len),
    ('超大包长防御',        t_huge_len),
    ('坏CRC丢弃',           t_bad_crc),
    ('攻击后服务器存活',    t_alive_after),
]

if __name__ == '__main__':
    fail = 0
    for name, fn in TESTS:
        try:
            ok = fn()
        except Exception as e:
            print('  [异常] %s: %s' % (name, e)); ok = False
        print('%s %s' % ('PASS' if ok else 'FAIL', name))
        fail += (not ok)
    sys.exit(1 if fail else 0)
