//开启子进程相关
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

/******************************************************************************************
函数原型: 
功能描述: 处理网络事件和定时器事件，我们遵照nginx引入这个同名函数
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: void NgxWorkerProcessCycle(int iProcesseIndex,const char *pcProcName) 
创建日期: 2020年09月11日 09时44分14秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void NgxProcessNetEventsAndTimers()
{
    g_LogicSocket.ProcessEpollEvents(-1); // -1表示卡着等待吧

    //统计信息打印，考虑到测试的时候总会收到各种数据信息，所以上边的函数调用一般都不会卡住等待收数据
    g_LogicSocket.PrintTDInfo();
    
    //...再完善
}

