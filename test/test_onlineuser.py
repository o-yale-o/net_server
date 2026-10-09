# -*- coding: utf-8 -*-
"""
在线用户表测试：验证登录登记/断线注销/全局在线人数查询(共享内存跨worker)

协议约定:
    登录(msgCode=6):   包体=STRUCT_LOGIN{username[56]+password[40]}=96字节, crc=对该包体计算
    查在线(msgCode=7): 无包体; 应答包体=int(网络序)在线人数

用法:
    cd test
    ./run_server_for_test.sh 18080 2    # 建议2个worker，更能体现跨进程共享
    python3 test_onlineuser.py [主机] [端口]
"""
import socket, struct, sys, time, zlib, os, subprocess

# 本测试自管独立服务器实例(端口18090), 不受同服务器其他测试的在线人数残留影响
PORT = 18090
_HERE = os.path.dirname(os.path.abspath(__file__))
DIR = None
MASTER = None

def _start_server():
    subprocess.run([_HERE + '/run_server_for_test.sh', str(PORT), '2'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    global DIR, MASTER
    DIR = open('/tmp/ngtest_dir').read().strip()
    MASTER = open(DIR + '/master.pid').read().strip()

def _stop_server():
    subprocess.run([_HERE + '/stop_server_for_test.sh'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
ADDR = ('127.0.0.1', PORT)

def make_login_pkt(username):
    """构造合法登录包: 消息头不算, 包头8字节+96字节登录体"""
    body = username.encode().ljust(56, b'\x00') + b'pwd'.ljust(40, b'\x00')
    crc = zlib.crc32(body) & 0xFFFFFFFF
    return struct.pack('>HHI', 8 + len(body), 6, crc) + body

def whoonline_pkt():
    return struct.pack('>HHI', 8, 7, 0)

def send_recv(sock, payload):
    """发送并收一个完整包(按包长拼接), 返回(msgCode, body)"""
    sock.sendall(payload)
    d = b''
    while len(d) < 8:
        r = sock.recv(8 - len(d))
        if not r: raise ConnectionError('对端关闭')
        d += r
    pkglen, msgcode, crc = struct.unpack('>HHI', d)
    body = b''
    while len(body) < pkglen - 8:
        r = sock.recv(pkglen - 8 - len(body))
        if not r: raise ConnectionError('对端关闭')
        body += r
    return msgcode, body

def get_online_count():
    """新开一条连接查询全局在线人数"""
    c = socket.create_connection(ADDR, timeout=3)
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    msgcode, body = send_recv(c, whoonline_pkt())
    c.close()
    assert msgcode == 7 and len(body) == 4, '查询在线人数应答异常'
    return struct.unpack('>i', body)[0]

if __name__ == '__main__':
    _start_server()
    fail = False

    # 1) 初始在线人数(应为0或≥0)
    n0 = get_online_count()
    print('初始在线人数: %d' % n0)

    # 2) 用户A登录 → 人数+1
    ca = socket.create_connection(ADDR, timeout=3)
    ca.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    mc, body = send_recv(ca, make_login_pkt('userA'))
    assert mc == 6, '登录应答命令字异常'
    time.sleep(0.2)
    n1 = get_online_count()
    print('A登录后在线人数: %d (前:%d)' % (n1, n0))
    fail |= (n1 != n0 + 1)

    # 3) 用户B登录(可能落在另一个worker) → 人数再+1
    cb = socket.create_connection(ADDR, timeout=3)
    cb.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    send_recv(cb, make_login_pkt('userB'))
    time.sleep(0.2)
    n2 = get_online_count()
    print('B登录后在线人数: %d' % n2)
    fail |= (n2 != n1 + 1)

    # 4) 重复登录同一用户 → 覆盖不累加
    send_recv(ca, make_login_pkt('userA'))
    time.sleep(0.2)
    n3 = get_online_count()
    print('A重复登录后在线人数: %d (应仍为%d)' % (n3, n2))
    fail |= (n3 != n2)

    # 5) A断开 → 人数-1(连接回收时自动从共享内存表注销)
    ca.close()
    time.sleep(3.5)  # 等待延迟回收(测试配置3秒)后注销生效
    n4 = get_online_count()
    print('A断开后在线人数: %d' % n4)
    fail |= (n4 != n2 - 1)

    # 6) B断开 → 回到初始
    cb.close()
    time.sleep(3.5)  # 等待延迟回收(测试配置3秒)后注销生效
    n5 = get_online_count()
    print('B断开后在线人数: %d (初始:%d)' % (n5, n0))
    fail |= (n5 != n0)

    _stop_server()
    print('== %s ==' % ('全部通过' if not fail else '存在FAIL'))
    sys.exit(1 if fail else 0)
