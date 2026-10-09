#!/bin/bash
# master 进程生命周期测试：worker 崩溃自动重启 + 优雅退出 + 零僵尸
# 用法: cd test && ./test_lifecycle.sh
# 依赖: 已在项目根目录 make 出 nginx；脚本自行启动/停止测试服务器
set -e
PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT=${1:-18080}

echo "== [1/4] 启动测试服务器(2 worker) =="
"$PROJ_ROOT/test/run_server_for_test.sh" "$PORT" 2 >/dev/null
DIR=$(cat /tmp/ngtest_dir)
MASTER=$(cat "$DIR/master.pid")
WORKERS=$(pgrep -P "$MASTER")
echo "master=$MASTER workers=$WORKERS"

echo "== [2/4] kill -9 一个 worker，验证 master 自动补齐 =="
W_FIRST=$(echo "$WORKERS" | head -1)
kill -9 "$W_FIRST"
sleep 3
NEW_WORKERS=$(pgrep -P "$MASTER")
COUNT_NEW=$(echo "$NEW_WORKERS" | wc -l)
COUNT_OLD=$(echo "$WORKERS" | wc -l)
if [ "$COUNT_NEW" != "$COUNT_OLD" ] || echo "$NEW_WORKERS" | grep -q "^$W_FIRST$"; then
    echo "FAIL worker未自动重启"; kill -9 $MASTER; exit 1
fi
if ! grep -q "重新拉起" "$DIR/error.log"; then
    echo "FAIL 日志中无重启记录"; kill -9 $MASTER; exit 1
fi
echo "PASS worker已自动补齐 (新worker组: $NEW_WORKERS)"

echo "== [3/4] 心跳功能仍正常 =="
python3 - "$PORT" <<'PYEOF'
import socket, struct, sys
c = socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=3)
c.sendall(struct.pack('>HHI', 8, 0, 0))
d = c.recv(100)
sys.exit(0 if len(d) >= 8 else 1)
PYEOF
echo "PASS 心跳正常"

echo "== [4/4] SIGTERM 优雅退出，验证无残留无僵尸 =="
kill -TERM "$MASTER"
sleep 4
if ps -p "$MASTER" > /dev/null 2>&1; then
    echo "FAIL master未退出"; kill -9 "$MASTER"; exit 1
fi
if pgrep -f "$DIR/nginx" > /dev/null 2>&1; then
    echo "FAIL 仍有进程残留"; pkill -9 -f "$DIR/nginx"; exit 1
fi
if ps -e -o stat,cmd | grep -E '^Z' | grep -q nginx; then
    echo "FAIL 存在僵尸进程"; exit 1
fi
if ! grep -q "master进程退出" "$DIR/error.log"; then
    echo "FAIL 日志无优雅退出记录"; exit 1
fi
echo "PASS 全员优雅退出，零僵尸"

rm -rf "$DIR" /tmp/ngtest_dir
echo "== 全部通过 =="
