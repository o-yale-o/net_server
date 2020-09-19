//信号有关的函数

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>    //信号相关头文件 
#include <errno.h>     //errno
#include <sys/wait.h>  //waitpid

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h" 

//一个信号有关的结构 ngx_signal_t
typedef struct 
{
    int           iSigno;       //信号对应的数字编号 ，每个信号都有对应的#define ，大家已经学过了 
    const  char   *pcSigname;    //信号对应的中文名字 ，比如SIGHUP 

    //信号处理函数,这个函数由我们自己来提供，但是它的参数和返回值是固定的【操作系统就这样要求】,大家写的时候就先这么写，也不用思考这么多；
    void  (*pfunSigHandler)(int iSigno, siginfo_t *pSignfo, void *pvUContext); //函数指针,   siginfo_t:系统定义的结构
} ngx_signal_t;

//声明一个信号处理函数
static void OnSignalHandler(int iSigno, siginfo_t *pSignfo, void *pvUContext); //static表示该函数只在当前文件内可见
static void GetChildProcessStatus(void);                                    //获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程

//数组 ，定义本系统处理的各种信号，我们取一小部分nginx中的信号，并没有全部搬移到这里，日后若有需要根据具体情况再增加
//在实际商业代码中，你能想到的要处理的信号，都弄进来
ngx_signal_t  g_arrSignals[] = {
    // iSigno    pcSigname          pfunSigHandler
    { SIGHUP,    "SIGHUP",           OnSignalHandler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           OnSignalHandler },        //标识2   
	{ SIGTERM,   "SIGTERM",          OnSignalHandler },        //标识15
    { SIGCHLD,   "SIGCHLD",          OnSignalHandler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          OnSignalHandler },        //标识3
    { SIGIO,     "SIGIO",            OnSignalHandler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },        //我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                                                  //所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉）
    //...日后根据需要再继续增加
    { 0,         NULL,               NULL               }         //信号对应的数字至少是1，所以可以用0作为一个特殊标记
};

/******************************************************************************************
函数原型: 
功能描述: 初始化信号的函数，用于注册信号处理程序
参数说明:   名称            类型                说明

返 回 值:   0: 成功  -1: 失败
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 08时30分01秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
int NgxInitSignals()
{
    ngx_signal_t      *pSig;  //指向自定义结构数组的指针 
    struct sigaction   sa;   //sigaction：系统定义的跟信号有关的一个结构，我们后续调用系统的sigaction()函数时要用到这个同名的结构

    for (pSig = g_arrSignals; pSig->iSigno != 0; pSig++)  //将signo ==0作为一个标记，因为信号的编号都不为0；
    {        
        //我们注意，现在要把一堆信息往 变量sa对应的结构里弄 ......
        memset(&sa,0,sizeof(struct sigaction));

        if (pSig->pfunSigHandler)  //如果信号处理函数不为空，这当然表示我要定义自己的信号处理函数
        {
            sa.sa_sigaction = pSig->pfunSigHandler;  //sa_sigaction：指定信号处理程序(函数)，注意sa_sigaction也是函数指针，是这个系统定义的结构sigaction中的一个成员（函数指针成员）；
            sa.sa_flags = SA_SIGINFO;        //sa_flags：int型，指定信号的一些选项，设置了该标记(SA_SIGINFO)，就表示信号附带的参数可以被传递到信号处理函数中
                                                //说白了就是你要想让sa.sa_sigaction指定的信号处理程序(函数)生效，你就把sa_flags设定为SA_SIGINFO
        }
        else
        {
            sa.sa_handler = SIG_IGN; //sa_handler:这个标记SIG_IGN给到sa_handler成员，表示忽略信号的处理程序，否则操作系统的缺省信号处理程序很可能把这个进程杀掉；
                                      //其实sa_handler和sa_sigaction都是一个函数指针用来表示信号处理程序。只不过这两个函数指针他们参数不一样， sa_sigaction带的参数多，信息量大，
                                       //而sa_handler带的参数少，信息量少；如果你想用sa_sigaction，那么你就需要把sa_flags设置为SA_SIGINFO；                                       
        } //end if

        sigemptyset(&sa.sa_mask);   //比如咱们处理某个信号比如SIGUSR1信号时不希望收到SIGUSR2信号，就可以用sigaddset(&sa.sa_mask,SIGUSR2);对信号为SIGUSR1时做处理，这个sigaddset三章五节讲过；
                                    //这里.sa_mask是个信号集（描述信号的集合），用于表示要阻塞的信号，sigemptyset()这个函数咱们在第三章第五节讲过：把信号集中的所有信号清0，本意就是不准备阻塞任何信号；
                                    
        
        //设置信号处理动作(信号处理函数)，说白了这里就是让这个信号来了后调用我的处理程序，有个老的同类函数叫signal，不过signal这个函数被认为是不可靠信号语义，不建议使用，大家统一用sigaction
        if (sigaction(pSig->iSigno, &sa, NULL) == -1) //参数1：要操作的信号
                                                     //参数2：主要就是那个信号处理函数以及执行信号处理函数时候要屏蔽的信号等等内容
                                                      //参数3：返回以往的对信号的处理方式【跟sigprocmask()函数边的第三个参数是的】，跟参数2同一个类型，我们这里不需要这个东西，所以直接设置为NULL；
        {   
            LOG_EMERG1(errno, "sigaction(%s) 失败!", pSig->pcSigname);
            return -1; //有失败就直接返回
        }	
        else
        {            
            //LOG_INFO("sigaction(%s) succed!",pSig->pcSigname);//成功不用写日志 
            //LOG_STDERR("sigaction(%s) succed!",pSig->pcSigname);//直接往屏幕上打印看看 ，不需要时可以去掉
            //LOG_INFO("sigaction(%s) succed!",pSig->pcSigname);
        }
    } //end for
    return 0; //成功    
}

/******************************************************************************************
函数原型: 
功能描述: 信号处理函数(尽可能简单、不处理复杂业务逻辑；尽快返回)
参数说明:   名称            类型                说明
            iSigno           int
            pSignfo         siginfo_t*          该结构中包含信号产生原因相关信息
            pvUContext        void*               NoUse
返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 08时31分00秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static void OnSignalHandler(int iSigno, siginfo_t *pSignfo, void *pvUContext)
{    
    //printf("来信号了\n");    
    ngx_signal_t    *pSig;    //自定义结构
    char            *pcAction; //一个字符串，用于记录一个动作字符串以往日志文件中写
    
    for (pSig = g_arrSignals; pSig->iSigno != 0; pSig++) //遍历信号数组    
    {         
        //找到对应信号，即可处理
        if (pSig->iSigno == iSigno) 
        { 
            break;
        }
    } //end for

    pcAction = (char *)"";  //目前还没有什么动作；

    if(g_iProcessType == NGX_PROCESS_MASTER)      //master进程，管理进程，处理的信号一般会比较多 
    {
        //master进程的往这里走
        switch (iSigno)
        {
        case SIGCHLD:  //一般子进程退出会收到该信号
            g_atomicHaveSigCHLD = 1;  //标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
            break;

        //.....其他信号处理以后待增加

        default:
            break;
        } //end switch
    }
    else if(g_iProcessType == NGX_PROCESS_WORKER) //worker进程，具体干活的进程，处理的信号相对比较少
    {
        //worker进程的往这里走
        //......以后再增加
        //....
    }
    else
    {
        //非master非worker进程，先啥也不干
        //do nothing
    } //end if(g_iProcessType == NGX_PROCESS_MASTER)

    //这里记录一些日志信息
    if(pSignfo && pSignfo->si_pid)  //si_pid = sending process ID【发送该信号的进程id】
    {
        LOG_NOTICE("signal %d (%s) received from %P%s", iSigno, pSig->pcSigname, pSignfo->si_pid, pcAction);
    }
    else
    {
        LOG_NOTICE("signal %d (%s) received %s",iSigno, pSig->pcSigname, pcAction);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }

    //.......其他需要扩展的将来再处理；

    //子进程状态有变化，通常是意外退出【既然官方是在这里处理，我们也学习官方在这里处理】
    if (iSigno == SIGCHLD) 
    {
        GetChildProcessStatus(); //获取子进程的结束状态
    } //end if

    return;
}

/******************************************************************************************
函数原型: 
功能描述: 获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: static void OnSignalHandler(int iSigno, siginfo_t *pSignfo, void *pvUContext)
创建日期: 2020年09月11日 08时28分04秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
static void GetChildProcessStatus(void)
{
    pid_t   pid;
    int     iStatus;
    int     iErrNo;
    int     iOne=0; //抄自官方nginx，应该是标记信号正常处理过一次

    //当你杀死一个子进程时，父进程会收到这个SIGCHLD信号。
    for ( ; ; ) 
    {
        //调用一下waitpid（ 获取子进程的终止状态 ），子进程就不会成为僵尸进程了；
        //第一次waitpid返回 >0值，表示成功，后边显示 2019/01/14 21:43:38 [alert] 3375: pid = 3377 exited on signal 9【SIGKILL】
        //第二次再调用waitpid会返回0，表示子进程还没结束，然后这里有return来退出；
        pid = waitpid(-1, &iStatus, WNOHANG);   //第一个参数为-1，表示等待任何子进程，
                                                //第二个参数：保存子进程的状态信息(大家如果想详细了解，可以百度一下)。
                                                //第三个参数：提供额外选项，WNOHANG表示不要阻塞，让这个waitpid()立即返回        

        if(pid == 0) //子进程没结束，会立即返回这个数字，但这里应该不是这个数字【因为一般是子进程退出时会执行到这个函数】
        {
            return;
        } //end if(pid == 0)

        if(pid == -1)//waitpid调用出错: 返回出去（）我们管不了这么多
        {
            //这里处理代码抄自官方nginx，主要目的是打印一些日志。考虑到这些代码也许比较成熟，所以，就基本保持原样照抄吧；
            iErrNo = errno;             // errno为Linxux的LastErrorCode。  (类似Windows的GetLastError())
            if(iErrNo == EINTR)         // 调用被某个信号中断
            {
                continue;
            }

            if(iErrNo == ECHILD  && iOne)  //没有子进程
            {
                return;
            }

            if (iErrNo == ECHILD)         //没有子进程
            {
                LOG_INFO("waitpid() failed!");
                return;
            }

            LOG_ALERT1(errno, "waitpid() failed!");
            return;
        }  //end if(pid == -1)

        //能走到这里：表示waitpid（）成功【返回进程id】 . 打印子进程退出日志
        iOne = 1;  //标记waitpid()返回了正常的返回值
        if(WTERMSIG(iStatus))  //获取使子进程终止的信号编号
        {
            LOG_ALERT("pid = %P exited on signal %d!",pid,WTERMSIG(iStatus));//获取使子进程终止的信号编号
        }
        else
        {
            LOG_NOTICE("pid = %P exited with code %d!",pid,WEXITSTATUS(iStatus));//WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }
    } //end for

    return;
}

