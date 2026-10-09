# 编码风格总则（Coding Conventions）

本项目所有 C/C++ 代码遵循以下总则。新增/修改代码必须保持与现有代码一致的风格。

## 1. 命名规范（MFC 风格匈牙利命名 + 驼峰）

变量名 = 类型前缀 + 大驼峰（PascalCase），与 VC++/MFC 惯例一致：

| 前缀 | 类型 | 示例 |
|------|------|------|
| `i` | int / 整型 | `iExitCode`, `iLogLevel` |
| `ul` | unsigned long / size_t | `ulEnvBuffLen` |
| `ui` | unsigned int | `uiTimeLastFloodKick` |
| `l` | long | `lSendSize` |
| `b` | bool | `bParentInit` |
| `p` | 指针 | `p_Config`, `pPkgBody` |
| `pp` | 二级指针 | `ppcOSArgv` |
| `g_` | 全局变量（前缀+下划线+匈牙利） | `g_iStopEvent`, `g_pcEnvBuff` |
| `m_` | 类成员变量 | `m_iLenPkgHeader`, `m_hEpoll` |
| `h` | 句柄 | `m_hEpoll`, `m_hThread` |
| `sz` | 字符数组/字符串缓冲 | `szErrBuff` |
| `c` | char | `ch` 类单字符 |
| `fp`/`pfn` | 函数指针 | `statusHandler[]` 元素 |

- 函数名：大驼峰，如 `Initialize()`、`ProcessClientRequest()`、`FreeResource()`。
- 类名：`C` 前缀 + 大驼峰，如 `CSocekt`、`CLogicSocket`、`CThreadPool`、`CConfig`。
- 宏/常量：全大写下划线，如 `NGX_PROCESS_MASTER`、`_DATA_BUFSIZE_`。
- 类型：`ngx_` 前缀 + 小写下划线，如 `ngx_connection_t`；`lp`/`LP` 前缀表示远指针结构，如 `LPSTRUC_MSG_HEADER`。

## 2. 注释规范

- 关键函数保留统一注释块（功能描述/参数/返回值/创建日期/修改记录）。
- 中文注释，说明"为什么"而不只是"做了什么"。

## 3. 健壮性守则

- 所有外部输入（网络包、配置文件）在使用长度/下标前必须先做**上下限校验**。
- 禁止把运行期拼出的字符串直接当 printf 格式串使用，一律 `"%s"` 传入。
- 跨线程访问的标志/计数必须使用 `std::atomic` 或锁保护。
- close(fd) 之后才能把对象归还复用队列；复用队列取出的对象必须校验序列号（`iCurrSequence`）。
- 单例一律使用 C++11 Meyers 单例（函数内 static），禁止手写双检锁。
- 错误路径必须回滚已分配资源；`while(!feof(f))` 模式禁止，一律 `while(fgets(...))`。

## 4. 参考nginx源码时的命名转换总则

借鉴/移植 nginx 官方代码时，**一律把变量名、函数名、类型名转换为本项目风格**后再入库：
- nginx 的 `ngx_snprintf` / `ngx_shm_alloc` 类函数 → `NgxXxx` 大驼峰（如 `NgxSnprintf`、`NgxShmCreateAnon`）；
- nginx 的 `shm->addr` / `size` / `log` 等变量 → `pShmAddr` / `ulSize` / `pLog` 匈牙利前缀+驼峰；
- nginx 的 `ngx_shm_t` 结构 → 本项目 `_STRUCT_XXX`/`ngx_xxx_s` 既有类型体系内命名；
- 只借鉴**实现思路**，注释保留中文说明并注明参考nginx官方xx处。

## 5. 每轮提交/推送前的README更新总则

**每轮功能开发完成后、git 提交和推送 GitHub 之前，必须先把本轮成果更新到 README.md**：
- 功能/修复内容写入九、已知问题与改进方向或相应章节（已完成的从待办勾销）；
- 新增能力如有性能数据，附压测基准数字；
- 新增测试脚本同步更新 test/README.md 的用法说明；
- 然后再 git commit（commit message 中同样概括成果）并推送。
- README「已知问题与改进方向」等**变更记录类章节一律倒序排列：最新修改放在最前面**，其后按时间回溯；每条以「第N轮/日期」开头，便于快速看到最新状态。

## 6. GitHub 推送总则（拐弯推送法）

**所有 git push 到 GitHub 一律按以下流程处理**（Linux 端无 VPN，直连 GitHub 不稳定，禁止直接从 Linux push）：

1. Linux 端打包：`git bundle create /tmp/<repo>-<branch>.bundle refs/heads/<branch>`（不要用 `--all`，否则 ref 重复会导致克隆失败），用 `git bundle verify` 确认"完整历史"；
2. 传输到 Win11 本机（如 DSH `rw_download`）；
3. 本机克隆：`git clone <bundle路径> <临时目录>`；
4. 本机改远程：`git -C <临时目录> remote set-url origin https://github.com/<user>/<repo>.git`；
5. 本机推送：`git -C <临时目录> -c http.sslBackend=openssl push origin <branch>`。
   - 必须用 `openssl` 后端（schannel 在 VPN 环境会报 `SEC_E_NO_CREDENTIALS`）；
   - 首次推送由 Git Credential Manager 弹窗登录，之后免密；
6. 推送后 `ls-remote` 核验远端 HEAD；清理临时克隆与 bundle。

补充：从 GitHub 拉取/同步同理反向操作（本机 clone/pull → bundle → 传回 Linux → `git pull ../xxx.bundle master`）。
