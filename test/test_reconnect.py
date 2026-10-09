# -*- coding: utf-8 -*-
"""
掉线重连测试【命令12】: 凭uid+token恢复会话 + 离线窗口期消息自动补投

前置: 用户A(alice)与B(bob)均登录; A断线; B给A发消息(窗口期内信箱保留);
     A重连(cmd12) → 会话恢复 + 离线消息自动补投到新连接。

用法:
    cd test
    ./run_server_for_test.sh 18080 2      # 回收等待默认缩短为3秒
    python3 test_reconnect.py [主机] [端口]
"""
import socket, struct, sys, time, zlib

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def login(sock, username):
    """登录, 应答包体=token(8,主机序), 返回(uid, token)"""
    body = username.encode().ljust(56, b'\x00') + b'pwd'.ljust(40, b'\x00')
    sock.sendall(struct.pack('>HHI', 8 + len(body), 6, crc(body)) + body)
    d = b''
    while len(d) < 8:
        r = sock.recv(8 - len(d))
        if not r: raise ConnectionError
        d += r
    pkglen, mc, _ = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        body += sock.recv(pkglen - 8 - len(body))
    assert mc == 6 and len(body) == 8
    return crc(username.encode()), struct.unpack('<Q', body)[0]

def reconnect_pkt(uid, token):
    """命令12: uid(8)+token(8) 主机序"""
    payload = struct.pack('<QQ', uid, token)
    return struct.pack('>HHI', 8 + len(payload), 12, crc(payload)) + payload

def sendmsg_pkt(token, seq, to_uid, text):
    """命令8: token+seq+toUid+text[200]"""
    payload = struct.pack('<QQQ', token, seq, to_uid) + text.encode().ljust(200, b'\x00')
    return struct.pack('>HHI', 8 + len(payload), 8, crc(payload)) + payload

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

    # 1) alice/bob 登录
    ca = socket.create_connection((HOST, PORT), timeout=5); ca.setsockopt(6, 1, 1)
    uidA, tokenA = login(ca, 'alice')
    cb = socket.create_connection((HOST, PORT), timeout=5); cb.setsockopt(6, 1, 1)
    uidB, tokenB = login(cb, 'bob')

    # 2) alice 断线(重连窗口期内)
    ca.close(); time.sleep(0.3)

    # 3) bob 趁 alice 掉线发点对点消息 → 窗口期内信箱保留
    cb.sendall(sendmsg_pkt(tokenB, 1, uidA, 'offline-msg-for-alice'))
    time.sleep(0.3)

    # 4) alice 重连: 新连接 + cmd12
    ca = socket.create_connection((HOST, PORT), timeout=5); ca.setsockopt(6, 1, 1)
    ca.sendall(reconnect_pkt(uidA, tokenA))
    mc, body = recv_pkt(ca)
    result = struct.unpack('>i', body)[0]
    r = (mc == 12 and result == 0)
    print('%s 重连会话恢复(result=%d)' % ('PASS' if r else 'FAIL', result))
    ok &= r

    # 5) 离线消息自动补投: alice 新连接应收到 bob 的消息(cmd9)
    mc, body = recv_pkt(ca, timeout=4.0)
    fromUid = struct.unpack('<Q', body[:8])[0]
    text = body[8:].rstrip(b'\x00').decode()
    r = (mc == 9 and fromUid == uidB and text == 'offline-msg-for-alice')
    print('%s 离线消息自动补投 (from=%d text=%r)' % ('PASS' if r else 'FAIL', fromUid, text))
    ok &= r

    # 6) 错误令牌重连被拒
    cc = socket.create_connection((HOST, PORT), timeout=5); cc.setsockopt(6, 1, 1)
    cc.sendall(reconnect_pkt(uidA, tokenA + 1))  # 错误token
    mc, body = recv_pkt(cc)
    result = struct.unpack('>i', body)[0]
    r = (mc == 12 and result == 1)
    print('%s 错误令牌重连被拒(result=%d)' % ('PASS' if r else 'FAIL', result))
    ok &= r
    cc.close()

    # 7) 超窗口重连被拒: 关闭并等回收(3秒)后, 条目已被注销
    ca.close(); time.sleep(4)
    ca = socket.create_connection((HOST, PORT), timeout=5); ca.setsockopt(6, 1, 1)
    ca.sendall(reconnect_pkt(uidA, tokenA))
    mc, body = recv_pkt(ca)
    result = struct.unpack('>i', body)[0]
    r = (mc == 12 and result == 1)
    print('%s 超窗口重连被拒(result=%d, 应重新登录)' % ('PASS' if r else 'FAIL', result))
    ok &= r
    ca.close(); cb.close()

    print('== %s ==' % ('全部通过' if ok else '存在FAIL'))
    sys.exit(0 if ok else 1)
