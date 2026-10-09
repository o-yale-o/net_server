# -*- coding: utf-8 -*-
"""
跨worker消息推送 + 会话认证/防重放 测试

协议:
    登录(6):  包体=STRUCT_LOGIN(96字节); 应答包体=uint64 会话令牌(主机序)
    发消息(8): 包体=token(8,主机序)+seq(8,主机序)+{toUid(8,主机序)+text(200)}
              token/seq 由服务器校验(令牌匹配+seq严格递增防重放)
    收消息(9): 服务器投递, 包体={fromUid(8)+text(200)}

用法:
    cd test
    ./run_server_for_test.sh 18080 2      # 2个worker, 消息可能跨worker投递
    python3 test_pushmsg.py [主机] [端口]
"""
import socket, struct, sys, time, zlib

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def login(sock, username):
    """登录并返回(uid, token)"""
    body = username.encode().ljust(56, b'\x00') + b'pwd'.ljust(40, b'\x00')
    pkt = struct.pack('>HHI', 8 + len(body), 6, crc(body)) + body
    sock.sendall(pkt)
    d = b''
    while len(d) < 8:
        d += sock.recv(8 - len(d))
    pkglen, msgcode, _ = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        body += sock.recv(pkglen - 8 - len(body))
    assert msgcode == 6 and len(body) == 8, '登录应答异常'
    uid = crc(username.encode())
    token = struct.unpack('<Q', body)[0]     #主机序
    assert token != 0, '令牌为0说明登记失败'
    return uid, token

def sendmsg_pkt(token, seq, to_uid, text):
    """命令8: token+seq+toUid+text[200], 均主机序"""
    payload = struct.pack('<QQQ', token, seq, to_uid) + text.encode().ljust(200, b'\x00')
    return struct.pack('>HHI', 8 + len(payload), 8, crc(payload)) + payload

def recv_pkt(sock, timeout=4.0):
    """收一个完整包, 返回(msgCode, body)"""
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

    # A/B 登录(2 worker 下可能落在不同进程, 消息走共享内存信箱跨进程投递)
    ca = socket.create_connection(('127.0.0.1', PORT), timeout=5)
    ca.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    uidA, tokenA = login(ca, 'alice')
    cb = socket.create_connection(('127.0.0.1', PORT), timeout=5)
    cb.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    uidB, tokenB = login(cb, 'bob')
    print('alice uid=%d token=%d / bob uid=%d token=%d' % (uidA, tokenA, uidB, tokenB))

    # 1) A→B 点对点消息(seq=1): B 应收到命令9, fromUid=uidA, 文本一致
    ca.sendall(sendmsg_pkt(tokenA, 1, uidB, 'hello-bob-from-alice'))
    mc, body = recv_pkt(cb)
    fromUid = struct.unpack('<Q', body[:8])[0]
    text = body[8:].rstrip(b'\x00').decode()
    r1 = (mc == 9 and fromUid == uidA and text == 'hello-bob-from-alice')
    print('%s A→B投递 (cmd=%d from=%d text=%r)' % ('PASS' if r1 else 'FAIL', mc, fromUid, text))
    ok &= r1

    # 2) 防重放: 用相同seq=1重发同样内容, B 不应再收到
    ca.sendall(sendmsg_pkt(tokenA, 1, uidB, 'hello-bob-from-alice'))
    dup = False
    try:
        mc, body = recv_pkt(cb, timeout=1.5)
        if mc == 9: dup = True
    except socket.timeout:
        pass
    print('%s 防重放(同seq重发被拒)' % ('FAIL' if dup else 'PASS'))
    ok &= (not dup)

    # 3) seq递增的正常新消息应送达
    ca.sendall(sendmsg_pkt(tokenA, 2, uidB, 'second-msg'))
    mc, body = recv_pkt(cb)
    text = body[8:].rstrip(b'\x00').decode()
    r3 = (mc == 9 and text == 'second-msg')
    print('%s seq递增新消息送达 (text=%r)' % ('PASS' if r3 else 'FAIL', text))
    ok &= r3

    # 4) 令牌错误被拒: B 伪造 A 的令牌发消息, B 的误投不会发生(直接无信箱投递), A 也收不到
    cb.sendall(sendmsg_pkt(0x12345678, 1, uidA, 'fake-token-msg'))
    fake = False
    try:
        mc, body = recv_pkt(ca, timeout=1.5)
        if mc == 9 and body[8:].rstrip(b'\x00').decode() == 'fake-token-msg':
            fake = True
    except socket.timeout:
        pass
    print('%s 伪造令牌被拒' % ('FAIL' if fake else 'PASS'))
    ok &= (not fake)

    ca.close(); cb.close()
    print('== %s ==' % ('全部通过' if ok else '存在FAIL'))
    sys.exit(0 if ok else 1)
