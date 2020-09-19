#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> 
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>          //gettimeofday

#include "ngx_macro.h"         //各种宏定义
#include "ngx_func.h"          //各种函数声明
#include "ngx_c_conf.h"        //和配置文件处理相关的类,名字带c_表示和类有关
#include "ngx_c_socket.h"      //和socket通讯相关
#include "ngx_c_memory.h"      //和内存分配释放等相关
#include "ngx_c_threadpool.h"  //和多线程有关
#include "ngx_c_crc32.h"       //和crc32校验算法有关 
#include "ngx_c_slogic.h"      //和socket通讯相关

//本文件用的函数声明
static void FreeResource();

//和设置标题有关的全局量
char    *g_pcEnvBuff        = NULL; //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
size_t  g_ulEnvBuffLen      = 0;    //环境变量所占内存大小
size_t  g_ulArgvNeedMemLen  = 0;    //保存下这些argv参数所需要的内存大小
int     g_iOSArgCount       = 0;    //参数个数 
char    **g_ppcOSArgv       = NULL; //原始命令行参数数组,在main中会被赋值
int     g_iIsDaemonModel    = 0;    //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了

//socket/线程池相关
CLogicSocket   g_LogicSocket;   //socket全局对象  
CThreadPool    g_ThreadPool;    //线程池全局对象

//和进程本身有关的全局量
pid_t   g_CurrPID;      //当前进程的pid
pid_t   g_ParentPID;    //父进程的pid
int     g_iProcessType; //进程类型，比如master,worker进程等: master进程、work进程
int     g_iStopEvent;   //退出标记  0-不退出 1-退出

sig_atomic_t  g_atomicHaveSigCHLD;         //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
                                   //一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】                                   

/******************************************************************************************
函数原型: 
功能描述: 主入口函数
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月13日 13时57分00秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
int main(int argc, char *const *argv)
{     
    int exitcode = 0;           //退出代码，先给0表示正常退出
    int i;                      //临时用
    
    //(0)先初始化的变量
    g_iStopEvent = 0;            //标记程序是否退出，0不退出          

    //(1)无伤大雅也不需要释放的放最上边    
    g_CurrPID    = getpid();      //取得进程pid
    g_ParentPID = getppid();     //取得父进程的id 
    //统计argv所占的内存
    g_ulArgvNeedMemLen = 0;
    for(i = 0; i < argc; i++)  //argv =  ./nginx -a -b -c asdfas
    {
        g_ulArgvNeedMemLen += strlen(argv[i]) + 1; //+1是给\0留空间。
    } 
    //统计环境变量所占的内存。注意判断方法是environ[i]是否为空作为环境变量结束标记
    for(i = 0; environ[i]; i++) 
    {
        g_ulEnvBuffLen += strlen(environ[i]) + 1; //+1是因为末尾有\0,是占实际内存位置的，要算进来
    } //end for

    g_iOSArgCount = argc;           //保存参数个数
    g_ppcOSArgv = (char **) argv; //保存参数指针

    //全局量有必要初始化的
    g_structNgxLog.fd = -1;                  //-1：表示日志文件尚未打开；因为后边ngx_log_stderr要用所以这里先给-1
    g_iProcessType = NGX_PROCESS_MASTER; //先标记本进程是master进程
    g_atomicHaveSigCHLD = 0;                     //标记子进程没有发生变化
   
    //(2)初始化失败，就要直接退出的
    //配置文件必须最先要，后边初始化啥的都用，所以先把配置读出来，供后续使用 
    CConfig *p_Config = CConfig::GetInstance(); //单例类
    if(p_Config->Load("nginx.conf") == false) //把配置文件内容载入到内存            
    {   
        LogInit();    //初始化日志
       LOG_STDERR("配置文件[%s]载入失败，退出!","nginx.conf");
        //exit(1);终止进程，在main中出现和return效果一样 ,exit(0)表示程序正常, exit(1)/exit(-1)表示程序异常退出，exit(2)表示表示系统找不到指定的文件
        exitcode = 2; //标记找不到文件
        goto lblexit;
    }

    CMemory::GetInstance();	// 内存单例类初始化，返回值不用保存
    CCRC32::GetInstance();  // 校验算法单例类初始化，返回值不用保存
        
    //(3)一些必须事先准备好的资源，先初始化
    LogInit();             //日志初始化(创建/打开日志文件)，这个需要配置项，所以要放配置文件载入之后     
        
    //(4)一些初始化函数，准备放这里        
    if(NgxInitSignals() != 0) //信号初始化
    {
        exitcode = 1;
        goto lblexit;
    }        
    if(g_LogicSocket.Initialize() == false)//初始化socket
    {
        exitcode = 1;
        goto lblexit;
    }

    //(5)一些不好归类的其他类别的代码，准备放这里
    BackupEnv();    //把环境变量搬家

    //------------------------------------
    //(6)创建守护进程
    if(p_Config->GetIntDefault("Daemon",0) == 1) //读配置文件，拿到配置文件中是否按守护进程方式启动的选项
    {
        //1：按守护进程方式运行
        int cdaemonresult = NgxDaemon();
        if(cdaemonresult == -1) //fork()失败
        {
            exitcode = 1;    //标记失败
            goto lblexit;
        }
        if(cdaemonresult == 1)
        {
            //这是原始的父进程
            FreeResource();   //只有进程退出了才goto到 lblexit，用于提醒用户进程退出了
                              //而我现在这个情况属于正常fork()守护进程后的正常退出，不应该跑到lblexit()去执行，因为那里有一条打印语句标记整个进程的退出，这里不该限制该条打印语句；
            exitcode = 0;
            return exitcode;  //整个进程直接在这里退出
        }
        //走到这里，成功创建了守护进程并且这里已经是fork()出来的进程，现在这个进程做了master进程
        g_iIsDaemonModel = 1;    //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
    }

    //(7)开始正式的主工作流程，主流程一直在下边这个函数里循环，暂时不会走下来，资源释放啥的日后再慢慢完善和考虑    
    NgxMasterProcessCycle(); //不管父进程还是子进程，正常工作期间都在这个函数里循环；
        
lblexit:  // 这里是执行不到的!!!
    //(5)该释放的资源要释放掉
    LOG_INFO("程序退出，再见了!");
    FreeResource();  //一系列的main返回前的释放动作函数
    return exitcode;
}

//专门在程序执行末尾释放资源的函数【一系列的main返回前的释放动作函数】
void FreeResource()
{
    //(1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if(g_pcEnvBuff)
    {
        delete []g_pcEnvBuff;
        g_pcEnvBuff = NULL;
    }

    //(2)关闭日志文件
    if(g_structNgxLog.fd != STDERR_FILENO && g_structNgxLog.fd != -1)  
    {        
        close(g_structNgxLog.fd); //不用判断结果了
        g_structNgxLog.fd = -1; //标记下，防止被再次close吧        
    }
}
