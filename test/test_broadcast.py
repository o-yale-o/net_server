# -*- coding: utf-8 -*-
"""
广播消息测试【命令10】: 认证通过后投递给除自己外的全部在线用户

用法:
    cd test
    ./run_server_for_test.sh 18080 2
    python3 test_broadcast.py [主机] [端口]
"""
import socket, struct, sys, time, zlib

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def login(sock, username):
    """登录, 返回(uid, token)"""
    body = username.encode().ljust(56, b'\x00') + b'pwd'.ljust(40, b'\x00')
    sock.sendall(struct.pack('>HHI', 8 + len(body), 6, crc(body)) + body)
    d = b''
    while len(d) < 8:
        d += sock.recv(8 - len(d))
    pkglen, msgcode, _ = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        body += sock.recv(pkglen - 8 - len(body))
    assert msgcode == 6 and len(body) == 8
    return crc(username.encode()), struct.unpack('<Q', body)[0]

def broadcast_pkt(token, seq, text):
    """命令10: token+seq+text[200], 主机序"""
    payload = struct.pack('<QQ', token, seq) + b'\x00'*8 + text.encode().ljust(200, b'\x00')  # +8保留区(服务器按token+seq+STRUCT_RECVMSG布局校验)
    return struct.pack('>HHI', 8 + len(payload), 11, crc(payload)) + payload

def recv_pkt(sock, timeout=4.0):
    sock.settimeout(timeout)
    d = b''
    while len(d) < 8:
        r = sock.recv(8 - len(d))
        if not r: raise ConnectionError('对端关闭')
        d += r
    pkglen, msgcode, _ = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        body += sock.recv(pkglen - 8 - len(body))
    return msgcode, body

if __name__ == '__main__':
    ok = True

    # 三人登录
    conns = {}
    tokens = {}
    for name in ('alice', 'bob', 'carol'):
        s = socket.create_connection(('127.0.0.1', PORT), timeout=5)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        uid, token = login(s, name)
        conns[name] = s
        tokens[name] = (uid, token)
    print('三人登录完成: uid=' + ','.join(str(tokens[n][0]) for n in ('alice','bob','carol')))

    # alice 广播(seq=1): bob/carol 都应收到, alice 自己不收
    conns['alice'].sendall(broadcast_pkt(tokens['alice'][1], 1, 'hello-everyone'))
    time.sleep(1.5)  # 等信箱轮询投递(500ms周期)

    for name in ('bob', 'carol'):
        mc, body = recv_pkt(conns[name])
        fromUid = struct.unpack('<Q', body[:8])[0]
        text = body[8:].rstrip(b'\x00').decode()
        r = (mc == 9 and fromUid == tokens['alice'][0] and text == 'hello-everyone')
        print('%s %s收到广播 (from=%d text=%r)' % ('PASS' if r else 'FAIL', name, fromUid, text))
        ok &= r

    dup = False
    try:
        mc, _ = recv_pkt(conns['alice'], timeout=1.5)
        if mc == 9: dup = True
    except socket.timeout:
        pass
    print('%s 发送者自身不收广播' % ('FAIL' if dup else 'PASS'))
    ok &= (not dup)

    # bob 广播(seq=1): alice/carol 收到
    conns['bob'].sendall(broadcast_pkt(tokens['bob'][1], 1, 'bob-here'))
    for name in ('alice', 'carol'):
        mc, body = recv_pkt(conns[name])
        text = body[8:].rstrip(b'\x00').decode()
        r = (mc == 9 and text == 'bob-here')
        print('%s %s收到bob广播' % ('PASS' if r else 'FAIL', name))
        ok &= r

    for s in conns.values(): s.close()
    print('== %s ==' % ('全部通过' if ok else '存在FAIL'))
    sys.exit(0 if ok else 1)
