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

## 六、TLS 证书与加密传输

服务支持可选的 TLS 加密传输（`nginx.conf` [Net] 段 `UseTLS`，**默认 0 关闭**——关闭时零开销、行为不变）。开启后客户端须以 TLS 方式连接（实测 TLSv1.3），明文连接会在握手阶段被踢除。

### 1. 证书从哪来

| 场景 | 证书来源 |
|------|----------|
| 测试 | `test/run_server_for_test.sh` 第 8 参数传 `1` 时，**自动生成自签名证书**到临时目录（`/tmp/ngtest.XXXX/server.crt` + `server.key`），随 `stop_server_for_test.sh` 清理一起销毁，一次性使用 |
| 正式部署 | 自己准备证书/私钥，在 `nginx.conf` 中用 `TLSCertFile` / `TLSKeyFile` 指定路径（默认 `./server.crt` / `./server.key`，即工作目录） |

手动生成自签名测试证书（也可供正式内网使用）：

```bash
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -subj "/CN=你的域名或IP" \
    -keyout server.key -out server.crt
```

> 注意：**私钥（server.key）绝不能提交到 git**——如需放在仓库目录内，先在 .gitignore 中加入 `*.key`。正式对外服务建议使用 CA 签发证书（如 Let's Encrypt 免费证书）；自签名证书仅适合测试/内网。

### 2. 配置与部署

```bash
# nginx.conf [Net] 段
UseTLS = 1
TLSCertFile = /path/to/server.crt
TLSKeyFile  = /path/to/server.key
```

- 证书/私钥在 **master 启动期加载**（`NgxSSLInit()`），加载失败（文件不存在、格式错误、私钥不匹配）时 **master 启动即失败退出**（快速失败，日志有明确提示）；
- `SSL_CTX` 在 fork 前创建，各 worker 继承只读使用，连接对象在 accept 时创建（`pConn->pSSL`）、回收时释放（PutOneToFree）；
- 与 `UseEpollET` / `UseReusePort` 正交，可组合使用。

### 3. 测试方法

```bash
cd test
./run_server_for_test.sh 18080 2 10 500 3 0 0 1   # 第8参数=1 开启TLS(自动生成测试证书)
python3 test_tls.py                                # 4用例: TLSv1.3握手/加密心跳/加密登录+查询/明文拒连
./stop_server_for_test.sh
```

测试客户端（`test_tls.py`）使用 Python `ssl` 模块包装 socket，自签名证书需设置 `check_hostname=False`、`verify_mode=CERT_NONE`。curl 验证可用 `curl -k https://...`。

### 4. 维护难点与易错点清单（证书是 TLS 实操中最容易踩坑的环节）

按证书生命周期【生成 → 部署 → 加载 → 运行 → 更换】整理，每条都是高频翻车点：

**生成阶段**
- **格式必须是 PEM**（文本格式，`-----BEGIN CERTIFICATE-----` 开头）；拿到 DER（二进制）格式的文件直接加载会失败。转换：`openssl x509 -inform der -in a.der -out a.crt`；
- **证书与私钥不匹配**：只更新了证书、忘了同步换私钥（或反过来）是最常见事故——本服务启动期 `SSL_CTX_check_private_key()` 会检测并**拒绝启动**（快速失败，日志提示"私钥与证书不匹配"）；
- **CN/SAN 与域名不一致**：正式环境客户端会校验证书域名，`/CN=localhost` 这类自签名只能测试用；
- **有效期**：自签名默认给 3650 天，Let's Encrypt 只有 90 天（必须配自动续期）。

**部署阶段**
- **私钥权限**：`chmod 600 server.key`，只给运行账号可读——私钥泄露 = 身份被冒用；
- **私钥绝不进 git**：如需放仓库目录，先在 `.gitignore` 加 `*.key`；
- **路径是相对工作目录解析的**：`TLSCertFile = ./server.crt` 中的 `./` 指服务启动时的工作目录（守护进程化后与启动位置一致），建议用**绝对路径**避免歧义；
- **链不完整**：正式 CA 证书要部署 fullchain（证书链），只放叶子证书会导致部分客户端校验失败（Let's Encrypt 用 `fullchain.pem` 而不是 `cert.pem`）；
- **master 启动即检测**：加载失败（文件不存在/格式错误/私钥不匹配）master 直接退出，不会带病运行。

**运行阶段**
- 明文客户端连 TLS 端口：握手失败即被踢除（防御行为，test_tls.py 第 4 用例覆盖）；
- TLS 握手期间连接持有 SSL 对象（约几十 KB/连接），海量连接时注意内存；
- `UseTLS` 开关本身改动需重启生效（热重载仅覆盖证书内容更新，见下条）。

**证书更换（热重载）**
- 更换证书文件后 `kill -HUP master` 即可：master 检测证书 mtime 变化 → 重建 SSL_CTX → **轮换全部 worker** 加载新证书（服务不中断，加载失败自动沿用旧证书）；
- 配 Let's Encrypt 90 天自动续期：certbot 续期脚本里加一行 `kill -HUP $(cat master的pid文件)` 即完成闭环，无需重启服务；
- **更换时证书和私钥要一起换**（保持匹配），只换一个会被启动/重载校验拦下。

**排障速查**
| 现象 | 大概率原因 |
|------|-----------|
| 启动报"加载TLS证书失败" | 路径错（相对工作目录）/文件不存在/不是PEM格式 |
| 启动报"私钥与证书不匹配" | 证书和私钥不是一对（更新时只换了一个） |
| 客户端报证书校验失败 | 自签名未跳过校验 / CN与域名不符 / 链不完整(缺fullchain) |
| 客户端报证书过期 | 90天证书未续期 / 系统时间错误 |
| SIGHUP后仍是旧证书 | 证书文件 mtime 未变化（内容没真正更新）/ 重新加载失败看日志 |

## 七、测试

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

## 八、核心设计要点

- **Master/Worker 多进程**：master 只做管理，worker 各自跑 epoll 事件循环；
- **epoll LT 模式 + 非阻塞 socket**：连接池预分配，accept 与读写分离处理函数；
- **连接延迟回收**：连接关闭先进待回收队列，等 `Sock_RecyConnectionWaitTime` 秒后才真正复用，防止旧事件串扰新连接；
- **心跳保活**：定时器线程周期检查，超时可选踢出；
- **flood 检测**：按收包时间间隔统计，超阈值踢除；
- **线程池 + 消息队列**：网络线程只做收发，完整包投递队列由工作线程处理，I/O 与业务解耦；
- **单例模式**（C++11 Meyers 单例：CConfig/CMemory/CCRC32）、**函数指针表命令分发**、**setproctitle**。

## 九、编码规范

所有代码遵循 [CODING_CONVENTIONS.md](CODING_CONVENTIONS.md)：MFC 风格匈牙利命名（类型前缀 + 大驼峰，如 `iExitCode`、`g_iStopEvent`、`m_iLenPkgHeader`），类名 `C` 前缀，宏全大写下划线；新增代码请保持一致。

## 十、已知问题与改进方向（最新在前）

**2025-10 第十二轮**：TLS 会话票据上线后**全量回归暴露真 bug**——UseTLS=0（默认关闭）时，worker 启动即因"票据密钥共享内存不存在"而集体 exit(-2)，触发重启风暴、服务不可用（只在 UseTLS=0 的矩阵中爆发，单测 TLS-on 时不显现——回归矩阵的价值实证）。修复：worker 侧票据校验仅在 TLS 开启时执行（新增 `IsTLSOn()`），未启用 TLS 时该模块本就不存在，直接跳过。修后全量回归 15/15 通过。

**2025-10 第十一轮**：建立**全量回归框架**（test/full_regression.sh）：一个脚本跑完 4 个配置矩阵（LT默认 / ET / REUSEPORT / TLS）× 各功能用例 + 3 个独立实例测试，共 15 项，汇总 PASS/FAIL。同时把回归框架性说明（共享服务器 vs 自管实例、断言相对化、TLS 下禁跑明文用例等注意事项）写入 test/README.md。

**2025-10 第十轮**：实现**TLS 证书热重载**（SIGHUP 链路增强）：master 重载配置时用 `stat()` 检测证书文件 mtime，变化则重建 `SSL_CTX`（加载失败自动沿用旧证书）并**轮换全部 worker**（先启动继承新证书的新 worker，再通知旧 worker 优雅退出——nginx reload 同款手法）。配合 Let's Encrypt 90 天自动续期，**续期后 `kill -HUP` 即可生效，无需重启服务**。同时修正"后续待改进方向"清单（SO_REUSEPORT/TLS 已完成移出）。

**2025-10 第九轮**：新增 **掉线重连**（命令12，包体=uid+token）。客户端断线后在窗口期内（延迟回收时长，`Sock_RecyConnectionWaitTime`）重连，凭登录时下发的 uid+token 调用命令 12 → 在线表 `TryRebind` 校验令牌并把会话绑定到新连接：**在线计数不变、token/lastSeq 保留（业务序号继续防重放）**；且**离线窗口期内投递到信箱的消息自动补投到新连接**（依赖信箱"无人认领保留10秒"机制）。超窗口或令牌错误则应答失败、客户端走重新登录。依赖第八轮的连接分配 ID 设计——旧连接延迟回收的注销不会误删重连恢复的会话。配套 test/test_reconnect.py 4 用例（会话恢复/离线消息补投/错误令牌拒绝/超窗口拒绝）+ test_onlineuser.py 改造为自管独立服务器实例（消除链式回归的在线人数残留干扰）。

**2025-10 第八轮**：新增 **TLS 加密传输**（排行榜任务4，`UseTLS` 默认 0 关闭——关闭时零开销、行为不变）。开启后（`UseTLS=1` + `TLSCertFile`/`TLSKeyFile`）：master 启动期创建 SSL_CTX 并加载证书（失败快速退出）；accept 后连接进入**非阻塞握手状态机**（`SSL_accept` 的 WANT_READ/WANT_WRITE 分别挂可读/可写事件，完成后切回 OnRead/OnWrite）；`ReadData`/`WriteData` 按 `pConn->pSSL` 分支 `SSL_read`/`SSL_write`（WANT_* 等价 EAGAIN）；连接回收时 `SSL_free`（挂接在唯一汇聚点 PutOneToFree）。证书支持自签名（测试证书由 run_server_for_test.sh 自动生成到临时目录）。实测 TLSv1.3：加密心跳/登录/在线查询全通，明文客户端连 TLS 端口被拒（防御生效）。

价值：业务与业务逻辑代码零改动即可获得加密传输；风险备忘：①进程崩溃时已排队未落盘日志不丢但 TLS 会话中断属正常；②握手期间的连接占用内存（SSL 对象约几十KB）；③信号处理器内打日志的既有风险不变。

**2025-10 第七轮**：新增 **SO_REUSEPORT 模式**（nginx.conf [Net] 段 `UseReusePort`，默认 0；需重启生效）。开启后每个 worker 在 EpollInit 时自行创建带 SO_REUSEPORT 的监听 socket——内核按四元组哈希把新连接直接分派到各 worker 独立的 accept 队列，**彻底消除惊群**（原架构为 master 创建单个监听 socket 由 worker 继承）。实测 2 worker 心跳压测 47243 请求/秒，accept 在两 worker 间均匀分布（2+2）；缩容/worker 退出时其监听 socket 随之关闭，不存在"死队列"。价值：多核扩展性与连接接入吞吐上限提升。风险备忘：需 Linux 3.9+；与 UseEpollET 正交可组合。master 启动期用探测 socket 对各端口做 bind 后立即关闭——端口被占时 master 启动即快速失败退出（参考 nginx init cycle 的 bind 检测），避免 worker 重启风暴（已实测：普通程序占用端口时 master 报错退出、零残留）。

**2025-10 第六轮(第二步)**：新增 **UseEpollET 配置开关**（nginx.conf [Net] 段，默认 0=LT；需重启生效）。OnRead/OnWrite/OnAccept 的循环收发代码 LT/ET 两模式通用，注册事件按配置附加 EPOLLET。

LT/ET A/B 压测数据（4 worker×4 连接×2000 心跳，同机同配置各一次采样）：
- LT：37377 请求/秒，avg RTT 0.102ms
- ET：42230 请求/秒，avg RTT 0.085ms（ET 略优约 +13%，单次采样在波动区间上沿）
- 结论：心跳场景下两种模式功能均完全正确；ET 唤醒次数少略有优势，但小包场景收益有限（与理论一致）。默认保持 LT（编程容错性更好），需要极限吞吐时可开 ET。

价值：一次唤醒消化全部缓冲数据（ET 必须项），配置化切换便于实测选型。风险备忘：ET 下漏读数据将导致连接假死（已用批量收发循环+单次上限规避）；EPOLLET 必须在注册时附加（已实现）；改动后需重启生效。

**2025-10 第六轮(第一步)**：**批量收发循环**：OnRead/OnWrite/OnAccept 改为一次事件循环处理直到 EAGAIN（单次上限 64 防饥饿），LT 模式下同样合法（减少内核唤醒次数），并为 ET 模式做好代码准备；顺带精简 ReadData 中 EAGAIN/EINTR 的高频日志。压测：4 worker 心跳吞吐 3.9 万请求/秒（历史同口径 4.9 万，运行波动区间内）。价值：一次唤醒消化缓冲区堆积数据；风险：批量循环需防单连接饥饿（已设上限）。

**2025-10 第五轮**：新增 **SIGHUP 配置热重载**（master 重读 nginx.conf：失败自动保留旧配置；WorkerProcesses 数量变化实时扩容/缩容 worker——缩容走优雅退出；其余配置项在 worker 重启后生效）。配套 test/test_reload.sh 一键验证。

**2025-10 第四轮**：新增**广播消息**（命令11，token+seq 认证防重放后经信箱投递给全部其他在线用户；9/10 为服务器投递方向保留命令字）。并修复两个真 bug：①uid 计算符号扩展（Get_CRC 有符号返回值导致跨 worker 查不到用户）；②延迟回收的旧连接注销会误删同名用户新登录的条目（引入不可变的连接分配 ID uiConnId 做注销匹配）。

**2025-10 第三轮**：新增**全局在线用户表**（`misc/ngx_c_onlineuser.cxx`，POSIX 共享内存 + 进程共享互斥量 + 线性探测哈希），master 在 fork 前创建、各 worker 挂接同一块映射；登录（命令 6）登记、连接回收时自动注销、新命令 7 查询全局在线人数（跨 worker 实时可见）。并修复 accept 后未设置 TCP_NODELAY 导致的小包交互延迟。全部功能配备自动化测试（test/ 目录）并可一键回归。

**2025-10 第二轮**：完善 master 生命周期（waitpid 收尸 + worker 崩溃自动重启 + 优雅退出）并落实剩余四项（epoll 过期事件过滤恢复、发送队列锁外化、EMFILE 哑 fd 兜底、flood 固定窗口 + 按 recv 计数）。以上均通过运行时验证（恶意包防御、kill -9 自动补齐、SIGTERM 全员优雅退出零僵尸、低 fd 上限下不死循环可恢复）。

**2025-10 第一轮**：完成一轮代码审查并修复 9 项功能缺陷（协议包长校验、日志格式化字符串漏洞、EPOLLOUT 错误分支泄漏、send 0/-2 静默丢包、连接归还顺序、配置解析死循环、伪双检锁单例等，见 git log）。

**后续待改进方向**：跨进程广播的性能优化（当前逐 uid 投递信箱，万级用户时考虑广播位图/组播槽位）；worker 内多 reactor（单 worker 多事件线程）；离线消息持久化暂存（打破信箱 10 秒上限）；TLS 会话恢复/会话票据（减少重连握手开销）；reload 时平滑重启全量 worker 的统一入口（当前证书轮换已用此机制，可扩展到任意配置变更）。
