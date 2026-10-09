//全局函数声明
#pragma once

#include <sys/types.h>  //pid_t

typedef unsigned char u_char;
#define MYVER "1.0.0"

//字符串相关函数
void   Rtrim(char *string);
void   Ltrim(char *string);

//设置可执行程序标题相关
void   BackupEnv();
void   SetProcTitle(const char *szTitle);

//日志打印输出有关
void   LogInit();
void   LogAsyncInit();      //启动异步日志线程[各进程fork之后各自调用]
void   LogAsyncShutdown();  //停止异步日志线程并排空队列[进程退出前调用]
void   LogStdErr(int iSystemErrCode, const char *fmt, ...);
void   LogErrorCore(int iLogLevel,  int iSystemErrCode, const char *fmt, ...);
u_char *LogSystemErr(u_char *pcBuff, u_char *last, int iSystemErrCode);
u_char *NgxSnprintf(u_char *pcBuff, size_t max, const char *fmt, ...);
u_char *NgxSlprintf(u_char *pcBuff, u_char *last, const char *fmt, ...);
u_char *NgxVslprintf(u_char *pcBuff, u_char *last,const char *fmt,va_list args);

//信号/主流程相关
int    NgxInitSignals();
void   NgxMasterProcessCycle();
int    NgxDaemon();
void   NgxProcessNetEventsAndTimers();
void   NgxRemoveWorkerPid(pid_t pid);  //worker被waitpid收尸后，从master的worker表中摘除该pid
