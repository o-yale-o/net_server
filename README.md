# my-nginx —— 仿 Nginx 的高并发 TCP 服务器框架

一个用 C++ 在 Linux 下从零实现的仿 Nginx 架构练习项目：**master/worker 多进程 + epoll 事件驱动 + 线程池业务处理**，面向长连接 TCP 业务（如游戏网关/逻辑服务器），而非 HTTP 反向代理。

## 一、项目宗旨

- 学习并复刻 Nginx 的核心架构：多进程模型、epoll 事件循环、连接池、延迟回收、心跳保活；
- 在其上叠加业务服务器常见的组件：线程池、消息队列、CRC32 包校验、flood 攻击检测；
- 全部代码手写、中文注释，作为网络编程/系统编程的练手工程。

## 二、整体架构

```
                    ┌─────────────── master 进程 ───────────────┐
                    │  读配置 → 守护进程化 → fork N 个 worker     │
                    │  信号管理 / 进程标题 / 日志初始化           │
                    └───────────────┬───────────────────────────┘
                                    │ fork
        ┌───────────────────────────┴───────────────────────────┐
        │                    worker 进程（×N）                    │
        │                                                        │
        │  net/    epoll 事件循环 ← accept ← 连接池(延迟回收)      │
        │           │ 收包(粘包状态机+flood检测)                    │
        │           ▼                                            │
        │  misc/   线程池 + 消息队列（网络I/O与业务解耦）            │
        │           │                                            │
        │           ▼                                            │
        │  logic/  业务逻辑层：命令字分发表（心跳/注册/登录…）       │
        │           │                                            │
        │           ▼                                            │
        │  misc/   回包 → 发送队列线程 → epoll 可写驱动             │
        └────────────────────────────────────────────────────────┘
```

**协议格式**：`消息头(内部) + 包头(pkgLen/msgCode/crc32，网络序) + 包体`，CRC32 校验包完整性。

## 三、目录结构

| 目录 | 职责 | 关键文件 |
|------|------|----------|
| `app/` | 程序入口与基础设施 | `nginx.cxx`(main 初始化主流程)、`ngx_log.cxx`(日志)、`ngx_c_conf.cxx`(配置)、`ngx_setproctitle.cxx`(进程标题) |
| `proc/` | 进程管理 | master 主循环、daemon 化、worker 创建 |
| `signal/` | 信号处理 | 信号注册与子进程状态处理 |
| `net/` | 网络核心 | `ngx_c_socket*.cxx`：epoll 循环、accept、连接读写、心跳定时器、收包/flood 检测 |
| `logic/` | 业务逻辑层 | `ngx_c_slogic.cxx`：成员函数指针表按命令字分发（0=心跳、5=注册、6=登录…） |
| `misc/` | 通用组件 | 线程池、内存单例、CRC32 |
| `_include/` | 公共头文件 | 类声明、宏、全局变量 |
| `logs/` | 日志输出 | |

## 四、编译与运行

```bash
# 编译（按 config.mk 中 BUILD_DIR 顺序逐目录编译，根目录链接出 ./nginx）
make

# 清理
make clean

# 运行（nginx.conf 中 Daemon=1 时自动守护进程化；监听端口见 ListenPort0）
./nginx
```

要求：Linux + g++（支持 C++11）、pthread。

## 五、配置说明（nginx.conf，key = value 格式）

| 配置项 | 默认 | 说明 |
|--------|------|------|
| `Log` | error.log | 日志文件路径 |
| `LogLevel` | 8 | 日志等级 0-8，数字越小级别越高 |
| `WorkerProcesses` | 4 | worker 进程数 |
| `Daemon` | 1 | 是否守护进程化 |
| `ProcMsgRecvWorkThreadCount` | 120 | 业务线程池线程数 |
| `ListenPortCount` / `ListenPort0` | 1 / 80 | 监听端口数量与端口 |
| `worker_connections` | 2048 | 每 worker 最大连接数 |
| `Sock_RecyConnectionWaitTime` | 150 | 连接关闭后延迟回收秒数 |
| `Sock_WaitTimeEnable` / `Sock_MaxWaitTime` / `Sock_TimeOutKick` | 1 / 20 / 0 | 心跳检测开关 / 检测周期 / 是否强踢 |
| `Sock_FloodAttackKickEnable` 等 | — | flood 攻击检测（时间间隔 + 连续次数） |

## 六、测试

自动化测试集中在 `test/` 目录（功能冒烟 / flood 防护 / 性能基准 / 进程生命周期），详见 [test/README.md](test/README.md)。快速上手：

```bash
make
cd test
./run_server_for_test.sh 18080      # 启动临时测试服务器(18080端口)
python3 test_smoke.py               # 功能冒烟
python3 test_perf.py 127.0.0.1 18080 4 2000   # 性能基准(注意按README放宽flood参数)
./stop_server_for_test.sh
```

参考基准：4 worker、4 连接并发心跳，吞吐约 4.5 万请求/秒，平均 RTT 0.081ms（2025-10，引入 TCP_NODELAY 后）。

## 七、核心设计要点

- **Master/Worker 多进程**：master 只做管理，worker 各自跑 epoll 事件循环；
- **epoll LT 模式 + 非阻塞 socket**：连接池预分配，accept 与读写分离处理函数；
- **连接延迟回收**：连接关闭先进待回收队列，等 `Sock_RecyConnectionWaitTime` 秒后才真正复用，防止旧事件串扰新连接；
- **心跳保活**：定时器线程周期检查，超时可选踢出；
- **flood 检测**：按收包时间间隔统计，超阈值踢除；
- **线程池 + 消息队列**：网络线程只做收发，完整包投递队列由工作线程处理，I/O 与业务解耦；
- **单例模式**（C++11 Meyers 单例：CConfig/CMemory/CCRC32）、**函数指针表命令分发**、**setproctitle**。

## 八、编码规范

所有代码遵循 [CODING_CONVENTIONS.md](CODING_CONVENTIONS.md)：MFC 风格匈牙利命名（类型前缀 + 大驼峰，如 `iExitCode`、`g_iStopEvent`、`m_iLenPkgHeader`），类名 `C` 前缀，宏全大写下划线；新增代码请保持一致。

## 九、已知问题与改进方向

2025-10 已完成一轮代码审查并修复 9 项功能缺陷（协议包长校验、日志格式化字符串漏洞、EPOLLOUT 错误分支泄漏、send 0/-2 静默丢包、连接归还顺序、配置解析死循环、伪双检锁单例等，见 git log）。

2025-10 第二轮：完善 master 生命周期（waitpid 收尸 + worker 崩溃自动重启 + 优雅退出）并落实剩余四项（epoll 过期事件过滤恢复、发送队列锁外化、EMFILE 哑 fd 兜底、flood 固定窗口 + 按 recv 计数）。以上均通过运行时验证（恶意包防御、kill -9 自动补齐、SIGTERM 全员优雅退出零僵尸、低 fd 上限下不死循环可恢复）。

2025-10 第三轮：新增**全局在线用户表**（`misc/ngx_c_onlineuser.cxx`，POSIX 共享内存 + 进程共享互斥量 + 线性探测哈希），master 在 fork 前创建、各 worker 挂接同一块映射；登录（命令 6）登记、连接回收时自动注销、新命令 7 查询全局在线人数（跨 worker 实时可见）。并修复 accept 后未设置 TCP_NODELAY 导致的小包交互延迟。全部功能配备自动化测试（test/ 目录）并可一键回归。

2025-10 第五轮：新增 **SIGHUP 配置热重载**（master 重读 nginx.conf：失败自动保留旧配置；WorkerProcesses 数量变化实时扩容/缩容 worker——缩容走优雅退出；其余配置项在 worker 重启后生效）。配套 test/test_reload.sh 一键验证。配test_broadcast：2025-10 第四轮：新增**广播消息**（命令11，token+seq认证防重放后经信箱投递给全部其他在线用户；9/10为服务器投递方向保留命令字）。并修复两个真bug：①uid计算符号扩展（Get_CRC有符号返回值导致跨worker查不到用户）；②延迟回收的旧连接注销会误删同名用户新登录的条目（引入不可变的连接分配ID uiConnId 做注销匹配）。
