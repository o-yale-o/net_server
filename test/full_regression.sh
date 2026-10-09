#!/bin/bash
# 全量回归：所有功能测试 + 配置组合矩阵 + 性能采样
# 用法: ./test/full_regression.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"   #所有测试脚本/相对路径都以test目录为工作目录

#清场: 杀掉所有残留nginx实例(历次测试的孤儿master会占住端口, 导致后续矩阵全挂)
pkill -9 -x nginx 2>/dev/null
sleep 1
PASS_TOTAL=0
FAIL_TOTAL=0

note() { echo; echo "======== $1 ========"; }
record() {  # record <名称> <退出码>
    if [ "$2" = "0" ]; then PASS_TOTAL=$((PASS_TOTAL+1)); echo ">>> [PASS] $1";
    else FAIL_TOTAL=$((FAIL_TOTAL+1)); echo ">>> [FAIL] $1"; fi
}

# ---- 1. 默认模式(LT+传统监听+无TLS): 全功能 ----
note "矩阵1: LT + 传统监听 (默认配置)"
./run_server_for_test.sh 18080 2 10 500 3 0 0 >/dev/null
python3 test_smoke.py;           record "smoke" $?
python3 test_pushmsg.py;         record "pushmsg" $?
python3 test_broadcast.py;       record "broadcast" $?
python3 test_reconnect.py;       record "reconnect" $?
python3 test_perf.py 127.0.0.1 18080 4 2000; record "perf-LT" $?
./stop_server_for_test.sh >/dev/null

# ---- 2. ET模式 ----
note "矩阵2: ET + 传统监听"
./run_server_for_test.sh 18080 2 10 500 3 1 0 >/dev/null
python3 test_smoke.py;           record "smoke-ET" $?
python3 test_pushmsg.py;         record "pushmsg-ET" $?
python3 test_perf.py 127.0.0.1 18080 4 2000; record "perf-ET" $?
./stop_server_for_test.sh >/dev/null

# ---- 3. REUSEPORT模式 ----
note "矩阵3: LT + REUSEPORT"
./run_server_for_test.sh 18080 2 10 500 3 0 1 >/dev/null
python3 test_smoke.py;           record "smoke-RP" $?
python3 test_pushmsg.py;         record "pushmsg-RP" $?
python3 test_perf.py 127.0.0.1 18080 4 2000; record "perf-RP" $?
./stop_server_for_test.sh >/dev/null

# ---- 4. TLS模式 ----
note "矩阵4: TLS (LT + 传统监听)"
./run_server_for_test.sh 18080 2 10 500 3 0 0 1 >/dev/null
python3 test_tls.py;             record "tls" $?
./stop_server_for_test.sh >/dev/null

# ---- 5. 自管服务器的测试 ----
note "独立实例测试"
python3 test_onlineuser.py;      record "onlineuser" $?
./test_lifecycle.sh 18080 >/dev/null; record "lifecycle" $?
./test_reload.sh >/dev/null;     record "reload" $?

echo
echo "================ 全量回归结果 ================"
echo "通过: $PASS_TOTAL  失败: $FAIL_TOTAL"
[ "$FAIL_TOTAL" = "0" ] && echo "★ 全部通过" || echo "★ 存在失败项"
exit $FAIL_TOTAL
