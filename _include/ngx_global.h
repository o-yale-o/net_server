
#pragma once

#include <signal.h> 

#include "ngx_c_slogic.h"
#include "ngx_c_threadpool.h"

//一些比较通用的定义放在这里，比如typedef定义
//一些全局变量的外部声明也放在这里

//类型定义----------------

//结构定义
typedef struct _CConfItem
{
	char ItemName[50];
	char ItemContent[500];
}CConfItem,*LPCConfItem;

//和运行日志相关 
typedef struct
{
	int    log_level;   //日志级别 或者日志类型，ngx_macro.h里分0-8共9个级别
	int    fd;          //日志文件描述符
}ngx_log_t;

//外部全局量声明
extern size_t        g_ulArgvNeedMemLen;
extern size_t        g_ulEnvBuffLen; 
extern int           g_iOSArgCount; 
extern char          **g_ppcOSArgv;
extern char          *g_pcEnvBuff; 
extern int           g_iIsDaemonModel;
extern CLogicSocket  g_LogicSocket;  
extern CThreadPool   g_ThreadPool;

extern pid_t         g_CurrPID;
extern pid_t         g_ParentPID;
extern ngx_log_t     g_structNgxLog;
extern int           g_iProcessType;   
extern sig_atomic_t  g_atomicHaveSigCHLD;   
extern int           g_iStopEvent;

