# -*- coding: utf-8 -*-
"""
TLS会话票据测试: 重连时恢复会话跳过完整握手 + 密钥轮换平滑过渡

原理: 服务器把会话状态加密成"票据"发给客户端; 客户端重连时递回票据,
     服务器解密恢复会话 → 跳过证书验证/ECDHE(省CPU和RTT)。
     票据密钥存共享内存, 全worker共享(轮换前签发的票据任意worker都能恢复)。

用法:
    cd test
    ./run_server_for_test.sh 18080 2 10 500 3 0 0 1   # 第8参数=1开TLS
    python3 test_ticket.py [主机] [端口]

验证点:
    1. 首连: 完整握手(session_reused=False)
    2. 带票据重连: 恢复握手(session_reused=True)【票据密钥共享, 跨worker也成立】
    3. SIGHUP轮换一次: 旧票据仍可恢复(备用密钥平滑过渡)
    4. SIGHUP轮换两次: 旧票据失效(降级完整握手但连接成功)
    5. 新票据重新可用
"""
import socket, ssl, struct, sys, time, zlib, os

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080
DIR = open('/tmp/ngtest_dir').read().strip() if os.path.exists('/tmp/ngtest_dir') else None

def make_ctx():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.options |= ssl.OP_NO_TLSv1_3  #禁用1.3强制TLS1.2: 使票据恢复行为在python3.6下可确定
    return ctx

CTX = make_ctx()  #全局共用一个SSLContext(session对象绑定ctx, 必须复用同一个)

def connect(session=None):
    raw = socket.create_connection((HOST, PORT), timeout=5)
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s = CTX.wrap_socket(raw, server_hostname='localhost', session=session)
    return s

if __name__ == '__main__':
    ok = True

    # 1) 首连: 完整握手
    c1 = connect()
    r1 = (c1.session_reused == False)
    print('%s 首连完整握手 (reused=%s)' % ('PASS' if r1 else 'FAIL', c1.session_reused))
    ok &= r1
    sess = c1.session
    c1.close()

    # 2) 带票据重连: 恢复握手(密钥共享内存, 同/跨worker均成立)
    reused_cnt = 0
    for i in range(4):
        c2 = connect(session=sess)
        reused_cnt += (c2.session_reused == True)
        c2.close()
    r2 = (reused_cnt == 4)
    print('%s 票据恢复握手×4 (恢复%d次)' % ('PASS' if r2 else 'FAIL', reused_cnt))
    ok &= r2

    # 3) SIGHUP轮换一次: 旧票据仍可恢复(备用密钥平滑过渡)
    if DIR is None:
        print('跳过轮换测试(需在test目录内运行以拿到ngtest_dir)')
    else:
        master = open(DIR + '/master.pid').read().strip()
        import signal
        os.kill(int(master), signal.SIGHUP)
        time.sleep(2)  # master消费SIGHUP并轮换密钥
        c3 = connect(session=sess)
        r3 = (c3.session_reused == True)  # 备用密钥仍在, 旧票据可恢复
        print('%s 轮换1次后旧票据仍可恢复(平滑过渡)' % ('PASS' if r3 else 'FAIL'))
        ok &= r3
        c3.close()

        # 4) 再轮换一次: 旧票据彻底失效, 降级完整握手但连接成功
        os.kill(int(master), signal.SIGHUP)
        time.sleep(2)
        c4 = connect(session=sess)
        r4 = (c4.session_reused == False)
        print('%s 轮换2次后旧票据失效, 降级完整握手' % ('PASS' if r4 else 'FAIL'))
        ok &= r4

        # 5) 新连接拿到新票据, 再次恢复成功
        sess2 = c4.session
        c4.close()
        c5 = connect(session=sess2)
        r5 = (c5.session_reused == True)
        print('%s 新票据恢复正常' % ('PASS' if r5 else 'FAIL'))
        ok &= r5
        c5.close()

    print('== %s ==' % ('全部通过' if ok else '存在FAIL'))
    sys.exit(0 if ok else 1)
