#!/bin/bash
# 停止由 run_server_for_test.sh 启动的测试服务器，并清理临时目录
DIR=$(cat /tmp/ngtest_dir 2>/dev/null)
if [ -z "$DIR" ] || [ ! -f "$DIR/master.pid" ]; then
    echo "没有找到运行中的测试服务器"; exit 0
fi
PID=$(cat "$DIR/master.pid")
kill -TERM "$PID" 2>/dev/null
sleep 2
# 兜底强杀残留
pkill -9 -f "$DIR/nginx" 2>/dev/null
rm -rf "$DIR" /tmp/ngtest_dir
echo "测试服务器已停止并清理: $DIR"
