#!/bin/bash
# 启动一个用于测试的临时服务器实例(前台模式、临时目录、临时端口，不影响正式 nginx.conf)
# 用法: ./run_server_for_test.sh [端口] [worker进程数] [flood时间间隔ms] [flood踢人阈值]
# 示例: ./run_server_for_test.sh 18080 2        # 端口18080, 2个worker
# 停止: ./stop_server_for_test.sh
PORT=${1:-18080}
WORKERS=${2:-1}
FLOOD_MS=${3:-100}
FLOOD_KICK=${4:-10}
RECY_WAIT=${5:-3}   # 连接延迟回收等待秒数(测试用缩短，默认正式值150)

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -x "$PROJ_ROOT/nginx" ]; then
    echo "错误: 先在项目根目录 make 编译出 nginx 可执行文件"
    exit 1
fi

DIR=$(mktemp -d /tmp/ngtest.XXXXXX)
cp "$PROJ_ROOT/nginx" "$DIR/"
sed -e "s/^Daemon = 1/Daemon = 0/" \
    -e "s/^WorkerProcesses = 4/WorkerProcesses = $WORKERS/" \
    -e "s/^ListenPort0 = 80/ListenPort0 = $PORT/" \
    -e "s|^Log=error.log|Log=$DIR/error.log|" \
    -e "s/^Sock_FloodTimeInterval = .*/Sock_FloodTimeInterval = $FLOOD_MS/" \
    -e "s/^Sock_FloodKickCounter = .*/Sock_FloodKickCounter = $FLOOD_KICK/" \
    -e "s/^Sock_RecyConnectionWaitTime = .*/Sock_RecyConnectionWaitTime = $RECY_WAIT/" \
    "$PROJ_ROOT/nginx.conf" > "$DIR/nginx.conf"

cd "$DIR"
nohup ./nginx > stdout.log 2>&1 &
echo $! > "$DIR/master.pid"
sleep 2

if ! kill -0 "$(cat "$DIR/master.pid")" 2>/dev/null; then
    echo "启动失败，日志如下:"; tail -5 "$DIR/error.log" 2>/dev/null; exit 1
fi

echo "$DIR" > /tmp/ngtest_dir
echo "测试服务器已启动: 127.0.0.1:$PORT  (master.pid=$(cat "$DIR/master.pid"))"
echo "  运行目录: $DIR"
echo "  运行日志: $DIR/error.log"
echo "  停止方法: $PROJ_ROOT/test/stop_server_for_test.sh"
