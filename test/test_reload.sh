#!/bin/bash
# SIGHUP 配置重载测试: 不中断服务调整 worker 数量
# 用法: cd test && ./test_reload.sh
set -e
PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT=18080

echo "== [1/5] 启动测试服务器(2 worker) =="
"$PROJ_ROOT/test/run_server_for_test.sh" "$PORT" 2 >/dev/null
DIR=$(cat /tmp/ngtest_dir)
MASTER=$(cat "$DIR/master.pid")
count_workers() { pgrep -P "$MASTER" | wc -l; }
echo "master=$MASTER workers=$(count_workers)"

ping_ok() {
    python3 - "$PORT" <<'PYEOF'
import socket, struct, sys
try:
    c = socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=3)
    c.sendall(struct.pack('>HHI', 8, 0, 0))
    d = c.recv(100)
    sys.exit(0 if len(d) >= 8 else 1)
except Exception:
    sys.exit(1)
PYEOF
}

echo "== [2/5] SIGHUP(配置未变): 服务不中断, worker数不变 =="
kill -HUP "$MASTER"
sleep 2
if [ "$(count_workers)" != "2" ]; then echo "FAIL worker数异常: $(count_workers)"; kill -9 $MASTER; exit 1; fi
ping_ok && echo "PASS 重载后服务正常, worker仍为2" || { echo "FAIL 服务异常"; kill -9 $MASTER; exit 1; }

echo "== [3/5] 改配置 WorkerProcesses 2→3, SIGHUP 扩容 =="
sed -i 's/^WorkerProcesses = 2/WorkerProcesses = 3/' "$DIR/nginx.conf"
kill -HUP "$MASTER"
sleep 3
if [ "$(count_workers)" != "3" ]; then echo "FAIL 扩容失败: $(count_workers)"; kill -9 $MASTER; exit 1; fi
if ! grep -q "配置文件重载成功" "$DIR/error.log"; then echo "FAIL 无重载日志"; kill -9 $MASTER; exit 1; fi
echo "PASS 扩容到3个worker"

echo "== [4/5] 改配置 3→1, SIGHUP 缩容 =="
sed -i 's/^WorkerProcesses = 3/WorkerProcesses = 1/' "$DIR/nginx.conf"
kill -HUP "$MASTER"
sleep 4
if [ "$(count_workers)" != "1" ]; then echo "FAIL 缩容失败: $(count_workers)"; kill -9 $MASTER; exit 1; fi
echo "PASS 缩容到1个worker"

echo "== [5/5] 缩容后服务仍正常 + 优雅退出 =="
ping_ok && echo "PASS 服务正常" || { echo "FAIL 服务异常"; kill -9 $MASTER; exit 1; }
kill -TERM "$MASTER"
sleep 4
if pgrep -f "$DIR/nginx" > /dev/null 2>&1; then echo "FAIL 有残留进程"; pkill -9 -f "$DIR/nginx"; exit 1; fi
echo "PASS 优雅退出, 零残留"

grep -E "重载配置|扩容|缩容" "$DIR/error.log" | tail -6 || true
rm -rf "$DIR" /tmp/ngtest_dir
echo "== 全部通过 =="
