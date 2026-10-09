# -*- coding: utf-8 -*-
"""
性能基准测试：测量心跳包的往返延迟(RTT)分布与并发吞吐

用法:
    cd test
    ./run_server_for_test.sh 18080 4
    python3 test_perf.py [主机] [端口] [并发连接数] [每连接请求数]

示例:
    python3 test_perf.py 127.0.0.1 18080 4 2000   # 4连接×2000请求

输出: RTT min/avg/P50/P90/P99/max(毫秒) + 总吞吐(请求/秒)
注意: 单个心跳包8字节，测的是"事件循环+线程池分发+回包"全链路开销
"""
import socket, struct, sys, time, threading

HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 18080
CONNS = int(sys.argv[3]) if len(sys.argv) > 3 else 4
REQS  = int(sys.argv[4]) if len(sys.argv) > 4 else 2000

def worker(iResult, idx):
    """单个连接: 串行 ping-pong 共 REQS 次，记录每次RTT(毫秒)"""
    rtts = []
    try:
        c = socket.create_connection((HOST, PORT), timeout=5)
        c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)  # 禁用Nagle，避免8字节ping-pong撞延迟ACK
        pkt = struct.pack('>HHI', 8, 0, 0)
        for i in range(REQS):
            t0 = time.time()
            c.sendall(pkt)
            d = b''
            while len(d) < 8:
                d += c.recv(8 - len(d))
            rtts.append((time.time() - t0) * 1000.0)
        c.close()
    except Exception as e:
        print('  [连接%d异常] %s' % (idx, e))
    iResult[idx] = rtts

if __name__ == '__main__':
    iResult = {}
    threads = [threading.Thread(target=worker, args=(iResult, i)) for i in range(CONNS)]
    t0 = time.time()
    for t in threads: t.start()
    for t in threads: t.join()
    elapsed = time.time() - t0

    allrtt = sorted(r for i in range(CONNS) for r in iResult.get(i, []))
    if not allrtt:
        print('没有任何成功请求'); sys.exit(1)

    def pct(p):
        return allrtt[min(int(len(allrtt) * p), len(allrtt) - 1)]

    total = len(allrtt)
    print('请求总数: %d (成功)  并发连接: %d  耗时: %.2fs' % (total, CONNS, elapsed))
    print('吞吐: %.0f 请求/秒' % (total / elapsed))
    print('RTT毫秒: min=%.3f  avg=%.3f  P50=%.3f  P90=%.3f  P99=%.3f  max=%.3f' % (
        allrtt[0], sum(allrtt) / total, pct(0.50), pct(0.90), pct(0.99), allrtt[-1]))
