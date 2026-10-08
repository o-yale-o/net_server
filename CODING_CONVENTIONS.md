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
