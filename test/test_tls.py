# -*- coding: utf-8 -*-
"""
TLS 测试【命令链路走加密通道】: 自签名证书 + 不校验CA
覆盖: TLS握手 / 加密心跳 / 加密登录+在线查询

用法:
    cd test
    ./run_server_for_test.sh 18080 2 10 500 3 0 0 1    # 第8参数=1开启TLS
    python3 test_tls.py [主机] [端口]
"""
import socket, ssl, struct, sys, time, zlib

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def tls_connect():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE  #自签名证书不校验
    raw = socket.create_connection((HOST, PORT), timeout=5)
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s = ctx.wrap_socket(raw, server_hostname='localhost')
    return s

def send_recv(s, payload):
    """发送并收一个完整包, 返回(msgCode, body)"""
    s.sendall(payload)
    d = b''
    while len(d) < 8:
        r = s.recv(8 - len(d))
        if not r: raise ConnectionError('对端关闭')
        d += r
    pkglen, msgcode, _ = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        body += s.recv(pkglen - 8 - len(body))
    return msgcode, body

def ping_pkt():
    return struct.pack('>HHI', 8, 0, 0)

def login_pkt(username):
    body = username.encode().ljust(56, b'\x00') + b'pwd'.ljust(40, b'\x00')
    return struct.pack('>HHI', 8 + len(body), 6, crc(body)) + body

def whoonline_pkt():
    return struct.pack('>HHI', 8, 7, 0)

if __name__ == '__main__':
    ok = True

    # 1) TLS握手 + 加密心跳
    s = tls_connect()
    print('TLS版本:', s.version())
    mc, _ = send_recv(s, ping_pkt())
    r1 = (mc == 0)
    print('%s TLS加密心跳' % ('PASS' if r1 else 'FAIL'))
    ok &= r1

    # 2) TLS加密登录
    mc, _ = send_recv(s, login_pkt('tlsuser'))
    r2 = (mc == 6)
    print('%s TLS加密登录' % ('PASS' if r2 else 'FAIL'))
    ok &= r2

    # 3) 登录后查询全局在线人数(应>=1)
    mc, body = send_recv(s, whoonline_pkt())
    cnt = struct.unpack('>i', body)[0]
    r3 = (mc == 7 and cnt >= 1)
    print('%s TLS加密在线查询(在线%d人)' % ('PASS' if r3 else 'FAIL', cnt))
    ok &= r3

    # 4) 明文客户端连TLS端口: 握手应失败(连接会被服务器踢掉)
    try:
        raw = socket.create_connection((HOST, PORT), timeout=3)
        raw.sendall(ping_pkt())  # 明文包发TLS端口 → 服务器SSL_accept失败 → 踢
        raw.settimeout(2)
        got = raw.recv(100)
        r4 = (got == b'')  # 服务器关闭连接=防御生效
    except (ConnectionResetError, BrokenPipeError, socket.timeout):
        r4 = True
    print('%s 明文连接被拒(握手失败即踢)' % ('PASS' if r4 else 'FAIL'))
    ok &= r4

    s.close()
    print('== %s ==' % ('全部通过' if ok else '存在FAIL'))
    sys.exit(0 if ok else 1)
