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

## 六、核心设计要点

- **Master/Worker 多进程**：master 只做管理，worker 各自跑 epoll 事件循环；
- **epoll LT 模式 + 非阻塞 socket**：连接池预分配，accept 与读写分离处理函数；
- **连接延迟回收**：连接关闭先进待回收队列，等 `Sock_RecyConnectionWaitTime` 秒后才真正复用，防止旧事件串扰新连接；
- **心跳保活**：定时器线程周期检查，超时可选踢出；
- **flood 检测**：按收包时间间隔统计，超阈值踢除；
- **线程池 + 消息队列**：网络线程只做收发，完整包投递队列由工作线程处理，I/O 与业务解耦；
- **单例模式**（C++11 Meyers 单例：CConfig/CMemory/CCRC32）、**函数指针表命令分发**、**setproctitle**。

## 七、编码规范

所有代码遵循 [CODING_CONVENTIONS.md](CODING_CONVENTIONS.md)：MFC 风格匈牙利命名（类型前缀 + 大驼峰，如 `iExitCode`、`g_iStopEvent`、`m_iLenPkgHeader`），类名 `C` 前缀，宏全大写下划线；新增代码请保持一致。

## 八、已知问题与改进方向

2025-10 已完成一轮代码审查并修复 9 项功能缺陷（协议包长校验、日志格式化字符串漏洞、EPOLLOUT 错误分支泄漏、send 0/-2 静默丢包、连接归还顺序、配置解析死循环、伪双检锁单例等，见 git log）。

2025-10 第二轮：完善 master 生命周期（waitpid 收尸 + worker 崩溃自动重启 + 优雅退出）并落实剩余四项（epoll 过期事件过滤恢复、发送队列锁外化、EMFILE 哑 fd 兜底、flood 固定窗口 + 按 recv 计数）。以上均通过运行时验证（恶意包防御、kill -9 自动补齐、SIGTERM 全员优雅退出零僵尸、低 fd 上限下不死循环可恢复）。
