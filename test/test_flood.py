# -*- coding: utf-8 -*-
"""
flood 防护测试：验证固定窗口计数 + 按 recv 计数能否正确踢掉攻击者
覆盖: 高频完整包攻击 / 1字节拆包绕过攻击(修复前可绕过)

用法:
    cd test
    ./run_server_for_test.sh 18080 2 100 10    # flood参数用默认: 100ms窗口/10次阈值
    python3 test_flood.py [主机] [端口]

注意: 两次攻击测试之间需要等待约200ms让服务器清场，脚本已内置。
"""
import socket, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080
ADDR = (HOST, PORT)

def ping_pkt():
    return struct.pack('>HHI', 8, 0, 0)

def was_kicked(sock):
    """判断连接是否已被服务器踢掉: recv返回0(对端关闭)或发送/接收抛连接复位"""
    try:
        sock.settimeout(2)
        if sock.recv(100) == b'':
            return True
    except (ConnectionResetError, BrokenPipeError):
        return True
    except socket.timeout:
        return False
    return False

def t_flood_burst():
    """连续高频发包(远超 10次/100ms 窗口阈值)，期望被踢"""
    c = socket.create_connection(ADDR, timeout=3)
    kicked = False
    for i in range(50):
        try:
            c.sendall(ping_pkt())
        except (BrokenPipeError, ConnectionResetError):
            kicked = True; break
        time.sleep(0.002)  # 2ms间隔 → 50ms内50次 >> 阈值10次/100ms
    if not kicked:
        kicked = was_kicked(c)
    c.close()
    return kicked

def t_flood_split():
    """拆包绕过测试: 把包拆成1字节/次发送(共28字节×2个包)，
       修复前只统计完整包永远不触发；现在按recv计数应被踢"""
    c = socket.create_connection(ADDR, timeout=3)
    data = ping_pkt() * 2 + b'\x00' * 20
    kicked = False
    for b in data:
        try:
            c.sendall(bytes([b]))
        except (BrokenPipeError, ConnectionResetError):
            kicked = True; break
        time.sleep(0.002)
    if not kicked:
        kicked = was_kicked(c)
    c.close()
    return kicked

def t_normal_after():
    """攻击者被踢后，新的正常客户端必须不受影响"""
    c = socket.create_connection(ADDR, timeout=3)
    c.sendall(ping_pkt())
    d = c.recv(100)
    c.close()
    return len(d) >= 8

if __name__ == '__main__':
    fail = 0
    for name, fn in [('高频完整包攻击被踢', t_flood_burst),
                     ('1字节拆包绕过被封堵', t_flood_split),
                     ('踢掉攻击者后正常用户不受影响', t_normal_after)]:
        try:
            ok = fn()
        except Exception as e:
            print('  [异常] %s: %s' % (name, e)); ok = False
        time.sleep(0.3)  # 清场
        print('%s %s' % ('PASS' if ok else 'FAIL', name))
        fail += (not ok)
    sys.exit(1 if fail else 0)
