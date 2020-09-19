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

//函数声明
static void NgxStartWorkerProcesses(int iProcessesNum);
static int  NgxSpawnProcess(int iProcesseIndex, const char * pcProcName);
static void NgxWorkerProcessCycle(int iProcesseIndex,const char *pcProcName);
static void NgxWorkerProcessInit(int iProcesseIndex);

//变量声明
static u_char  g_szMasterProcessTitle[] = "master process";

/******************************************************************************************
函数原型: 
功能描述: Master进程循环：创建worker子进程
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 09时43分51秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void NgxMasterProcessCycle()
{    
    sigset_t sigsetIgnore;        //（要屏蔽的）信号集(不允许这些信号打断当前的信号处理OnSignalHandler())

    sigemptyset(&sigsetIgnore);   //清空信号集

    //下列这些信号在执行本函数期间不希望收到【考虑到官方nginx中有这些信号，老师就都搬过来了】（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&sigsetIgnore, SIGCHLD);     //子进程状态改变
    sigaddset(&sigsetIgnore, SIGALRM);     //定时器超时
    sigaddset(&sigsetIgnore, SIGIO);       //异步I/O
    sigaddset(&sigsetIgnore, SIGINT);      //终端中断符
    sigaddset(&sigsetIgnore, SIGHUP);      //连接断开
    sigaddset(&sigsetIgnore, SIGUSR1);     //用户定义信号
    sigaddset(&sigsetIgnore, SIGUSR2);     //用户定义信号
    sigaddset(&sigsetIgnore, SIGWINCH);    //终端窗口大小改变
    sigaddset(&sigsetIgnore, SIGTERM);     //终止
    sigaddset(&sigsetIgnore, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......
    
    //设置，此时无法接受的信号；阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号。。。
    //sigprocmask()在第三章第五节详细讲解过
    if (sigprocmask(SIG_BLOCK, &sigsetIgnore, NULL) == -1) //第一个参数用了SIG_BLOCK表明设置 进程 新的信号屏蔽字 为 “当前信号屏蔽字 和 第二个参数指向的信号集的并集
    {        
        LOG_ALERT1(errno, "sigprocmask()失败!");
    }
    //即便sigprocmask失败，程序流程 也继续往下走

    //设置主进程标题
    size_t size;
    int    i;
    size = sizeof(g_szMasterProcessTitle);  //注意我这里用的是sizeof，所以字符串末尾的\0是被计算进来了的
    size += g_ulArgvNeedMemLen;          //argv参数长度加进来    
    if(size < 1000) //长度小于这个，我才设置标题
    {
        char szTitle[1000] = {0};
        strcpy(szTitle,(const char *)g_szMasterProcessTitle); //"master process"
        strcat(szTitle," ");  //跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_iOSArgCount; i++)         //"master process ./nginx"
        {
            strcat(szTitle,g_ppcOSArgv[i]);
        }//end for
        SetProcTitle(szTitle); //设置标题
        LOG_INFO("%s 【master进程】开始运行...", szTitle);
    }    
    //end 设置主进程标题
        
    //从配置文件中读取要创建的worker进程数量
    CConfig *pConfig = CConfig::GetInstance(); //单例类
    int iProcessesNum = pConfig->GetIntDefault("WorkerProcesses",1); //从配置文件中得到要创建的worker进程数量
    NgxStartWorkerProcesses(iProcessesNum);  //这里要创建worker子进程

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
    sigemptyset(&sigsetIgnore); //清空信号屏蔽字（不屏蔽任何信号）
    
    for ( ;; ) 
    {

    //    usleep(100000);
        //LOG_INFO("haha--这是父进程，pid为%P",g_CurrPID);

        // sigsuspend(const sigset_t *mask))用于在接收到某个信号之前, 临时用mask替换进程的信号掩码, 并暂停进程执行，直到收到信号为止。
        // sigsuspend 返回后将恢复调用之前的信号掩码。信号处理函数完成后，进程将继续执行。该系统调用始终返回-1，并将errno设置为EINTR。

        //sigsuspend是一个原子操作，包含4个步骤：
        //a)根据给定的参数设置新的mask 并 阻塞当前进程【因为是个空集，所以不阻塞任何信号】
        //b)此时，一旦收到信号，便再次屏蔽上面10个信号【我们原来调用sigprocmask()的mask在上边设置的，阻塞了多达10个信号，从而保证我下边的执行流程不会再次被其他信号截断】
        //c)调用该信号对应的信号处理函数
        //d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走
        //printf("for进来了！\n"); //发现，如果print不加\n，无法及时显示到屏幕上，是行缓存问题，以往没注意；可参考https://blog.csdn.net/qq_26093511/article/details/53255970

        sigsuspend(&sigsetIgnore); // 阻塞在这里，等待一个信号（此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回））。 
                                   // 这样达到【master进程完全靠信号驱动干活】的效果
                                   // 类似于：windows的  WaitForSingleObject()  ...    

        // 信号来了，并且 OnSignalHandler() 执行完，才运行到这里!!!
        // printf("OnSignalHandler() 执行完毕!  才执行到sigsuspend()下边来了\n");
        
        //printf("master进程休息1秒\n");      
        //LogStdErr(0,"haha--这是父进程，pid为%P",g_CurrPID); 
        sleep(1); //休息1秒        
        //以后扩充.......

    }// end for(;;)
    return;
}

/******************************************************************************************
函数原型: 
功能描述: 创建 iProcessesNum 个子进程
参数说明:   名称            类型                说明
            threadnums      int                 要创建的子进程数量
返 回 值: 
依 赖 于: 
被引用于: void NgxMasterProcessCycle()
创建日期: 2020年09月11日 09时42分59秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static void NgxStartWorkerProcesses(int iProcessesNum)
{
    int i;

    for (i = 0; i < iProcessesNum; i++)  //master进程在走这个循环，来创建若干个子进程
    {
        NgxSpawnProcess( i, "worker process" );
    }

    return;
}

/******************************************************************************************
函数原型: 
功能描述: 生出一个子进程（并进入NgxWorkerProcessCycle()死循环不再出来)
参数说明:   名称            类型                说明
            iProcesseIndex  int                 进程编号【0开始】
            pcProcName       const char*         子进程名字"worker process" 
返 回 值: 
依 赖 于: 
被引用于: void NgxStartWorkerProcesses(int iProcessesNum)
创建日期: 2020年09月11日 09时41分47秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static int NgxSpawnProcess(int iProcesseIndex, const char *pcProcName)
{
    pid_t  pid;

    pid = fork(); //fork()系统调用产生子进程
    switch (pid)  //判断父子进程
    {  
    case -1: //产生子进程失败
        LOG_ALERT1(errno, "fork()产生子进程 iProcesseIndex =%d, pcProcName=\"%s\"失败!", iProcesseIndex, pcProcName);
        return -1;

    case 0:  //子进程分支
        g_ParentPID = g_CurrPID;    //(因为是子进程了，所以)g_CurrPID是pid是父进程
        g_CurrPID   = getpid();     //getpid()得到本（子）进程的pid
        NgxWorkerProcessCycle( iProcesseIndex, pcProcName );    //所有worker子进程，在这个函数里死循环着(不出来)（也就是说：后面的代码，子进程不会执行了）
        break;

    default: //这是父进程，直接break,继续往后后走            
        break;
    }//end switch

    //只有父进程会走到这里!!
    //若有需要，以后再扩展增加其他代码......
    return pid;
}

/******************************************************************************************
函数原型: 
功能描述: (每个)worker子进程功能循环（无限循环【处理网络事件和定时器事件以对外提供web服务】）
        子进程分叉才会走到这里
参数说明:   名称            类型                说明
            iProcesseIndex  int                 进程编号【0开始】
返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 09时40分30秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static void NgxWorkerProcessCycle(int iProcesseIndex,const char *pcProcName) 
{
    //设置一下变量
    g_iProcessType = NGX_PROCESS_WORKER;  // 设置进程的类型为【worker】

    //重新为子进程设置进程名(防止与父进程重复)
    NgxWorkerProcessInit( iProcesseIndex );
    SetProcTitle(pcProcName); //设置标题   
    LOG_INFO("%s 【worker进程】开始运行...", pcProcName);

    //测试代码，测试线程池的关闭
    //sleep(5); //休息5秒        
    //g_ThreadPool.StopAll(); //测试Create()后立即释放的效果

    //暂时先放个死循环，我们在这个循环里一直不出来
    //setvbuf(stdout,NULL,_IONBF,0); //这个函数. 直接将printf缓冲区禁止， printf就直接输出了。
    for(;;)
    {

        //先sleep一下 以后扩充.......
        //printf("worker进程休息1秒");       
        //fflush(stdout); //刷新标准输出缓冲区，把输出缓冲区里的东西打印到标准输出设备上，则printf里的东西会立即输出；
        //sleep(1); //休息1秒       
        //usleep(100000);
        //LOG_INFO("good--这是子进程，编号为%d,pid为%P！",inum,g_CurrPID);
        //printf("1212");
        //if(inum == 1)
        //{
            //LogStdErr(0,"good--这是子进程，编号为%d,pid为%P",inum,g_CurrPID); 
            //printf("good--这是子进程，编号为%d,pid为%d\r\n",inum,g_CurrPID);
            //LOG_INFO("good--这是子进程，编号为%d",inum,g_CurrPID);
            //printf("我的测试哈inum=%d",inum++);
            //fflush(stdout);
        //}
            
        //LogStdErr(0,"good--这是子进程，编号为%d,pid为%P",inum,g_CurrPID); 
        //LOG_INFO("good--这是子进程，编号为%d,pid为%P",inum,g_CurrPID);

        NgxProcessNetEventsAndTimers(); //处理网络事件和定时器事件

        /*if(false) //优雅的退出
        {
            g_iStopEvent = 1;
            break;
        }*/

    } //end for(;;)

    //如果从这个循环跳出来
    g_ThreadPool.StopAll();      //考虑在这里停止线程池；
    g_LogicSocket.ShutdownSubProc(); //socket需要释放的东西考虑释放；
    return;
}

/******************************************************************************************
函数原型: 
功能描述: 子进程创建时调用本函数进行一些初始化工作
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: static void NgxWorkerProcessCycle(int iProcesseIndex,const char *pcProcName)
创建日期: 2020年09月11日 09时40分19秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static void NgxWorkerProcessInit(int iProcesseIndex)
{
    sigset_t  set;      //信号集

    sigemptyset(&set);  //清空信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  //原来是屏蔽那10个信号【防止fork()期间收到信号导致混乱】，现在不再屏蔽任何信号【接收任何信号】
    {
        LOG_ALERT1(errno, "sigprocmask()失败!");
    }

    //线程池代码，率先创建，至少要比和socket相关的内容优先
    CConfig *pConfig = CConfig::GetInstance();
    int tmpthreadnums = pConfig->GetIntDefault("ProcMsgRecvWorkThreadCount",5); //处理（接收到的）消息的（线程池中）线程数量
    if(g_ThreadPool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1); //再休息1秒；

    if(g_LogicSocket.InitializeSubProc() == false) //初始化子进程需要具备的一些多线程能力相关的信息
    {
        //内存没释放,粗暴退出；
        exit(-2);
    }
    
    //如下这些代码参照官方 nginx 里的 ngx_event_process_init() 函数中的代码
    g_LogicSocket.EpollInit();           //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
    //g_LogicSocket.ngx_epoll_listenportstart();//往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
    
    
    //....将来再扩充代码
    //....
    return;
}
