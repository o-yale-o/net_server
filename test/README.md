# 测试目录说明

本目录存放项目的自动化测试：功能冒烟、安全防护、性能基准、进程生命周期。

所有测试针对**独立启动的临时服务器实例**运行（临时端口 + 临时配置目录），不会影响正式的 `nginx.conf` 和 `logs/`。

## 0. 前置条件

```bash
# 先在项目根目录编译
make
```

测试脚本默认连接 `127.0.0.1:18080`，所有脚本都支持传参覆盖（见各自文件头部说明）。

## 1. 启动/停止测试服务器

```bash
cd test
./run_server_for_test.sh [端口] [worker数] [flood窗口ms] [flood阈值] [回收等待秒]
    # 默认: 18080 / 2个worker / 100ms / 10次 / 3秒(正式默认150秒)
    # 启动后会把运行目录写到 /tmp/ngtest_dir，日志在 <运行目录>/error.log

./stop_server_for_test.sh      # SIGTERM优雅停止并清理临时目录
```

## 2. 功能冒烟测试

```bash
./run_server_for_test.sh 18080
python3 test_smoke.py            # 5个用例: 正常心跳/包长下溢/超大包长/坏CRC/攻击后存活
```

覆盖的防御点（对应逻辑层 `ProcessClientRequest` 的包长校验与 CRC 校验）。

## 3. flood 防护测试

```bash
./run_server_for_test.sh 18080 2 100 10    # flood参数保持默认: 100ms窗口/10次阈值
python3 test_flood.py            # 3个用例: 高频攻击被踢 / 1字节拆包绕过被封堵 / 正常用户不受影响
```

覆盖 `TestFlood` 固定窗口计数 + `OnRead` 按 recv 计数两个机制。

## 4. 性能基准

```bash
# 压测速率会超过flood阈值(10次/100ms)，务必放宽flood参数，否则连接会被当成攻击踢掉
./run_server_for_test.sh 18080 4 10 500    # 4个worker, flood窗口10ms/阈值500(实际关闭)
python3 test_perf.py 127.0.0.1 18080 4 2000   # 4连接×每连接2000次心跳往返
```

输出：吞吐（请求/秒）与 RTT 延迟分布（min/avg/P50/P90/P99/max）。
**建议**：每次改动网络层代码后跑一遍，与上次数据对比，用数据验证优化效果。

## 5. 进程生命周期测试

```bash
./test_lifecycle.sh              # 一键自动化: 启动→kill -9 worker→验证自动重启→SIGTERM→验证零僵尸
```

覆盖 master 进程的 waitpid 收尸、worker 自动补齐、SIGTERM 优雅退出（脚本会自行启停服务器）。

## 6. 在线用户表测试

```bash
./run_server_for_test.sh 18080 2          # 回收等待已默认缩短为3秒，断线后稍等即可看到人数回落
python3 test_onlineuser.py                # 登记登录/重复登录覆盖/断线注销/全局人数查询(共享内存跨worker)
./stop_server_for_test.sh
```

注意: 用户注销发生在连接被延迟回收时(`Sock_RecyConnectionWaitTime`秒后)，所以断开后人数回落有最多3秒延迟。

## 7. 广播消息测试

```bash
./run_server_for_test.sh 18080 2
python3 test_broadcast.py     # 5用例: 多人群发/发送者不收/二次广播
```

覆盖命令10(广播): 认证+防重放+排除自己+全员投递。

## 8. TLS 加密传输测试

```bash
./run_server_for_test.sh 18080 2 10 500 3 0 0 1   # 第8参数=1: 开启TLS并自动生成自签名测试证书
python3 test_tls.py          # 4用例: TLSv1.3握手/加密心跳/加密登录+在线查询/明文拒连
./stop_server_for_test.sh
```

证书相关说明(生成命令/部署/注意点)见项目根 README「六、TLS 证书与加密传输」。

## 9. 配置热重载测试

```bash
./test_reload.sh     # 一键: SIGHUP未变重载/扩容2→3/缩容3→1/全程服务不中断/优雅退出
```

覆盖 SIGHUP 重载链路: 信号置位 → master 重读配置(失败保留旧配置) → WorkerProcesses 扩缩容 → 缩容 worker 优雅退出。

## 10. 一键回归（全部测试）

```bash
cd test
./run_server_for_test.sh 18080 2 100 10
python3 test_smoke.py && python3 test_flood.py
./stop_server_for_test.sh
./test_lifecycle.sh
python3 -c "print('如需性能数据: 见第4节')"
```
