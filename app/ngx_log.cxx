﻿//和日志相关的函数放之类
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

//全局量---------------------
//错误等级，和ngx_macro.h里定义的日志等级宏是一一对应关系
static u_char g_szErrLevelNames[][20]  = 
{
    {"STDERR"},    //0：控制台错误
    {"EMERG "},     //1：紧急
    {"ALERT "},     //2：警戒
    {"CRIT  "},      //3：严重
    {"ERROR "},     //4：错误
    {"WARN  "},      //5：警告
    {"NOTICE"},    //6：注意
    {"INFO  "},      //7：信息
    {"DEBUG "}      //8：调试
};
ngx_log_t   g_structNgxLog;


//----------------------------------------------------------------------------------------------------------------------
//描述：通过可变参数组合出字符串【支持...省略号形参】，自动往字符串最末尾增加换行符【所以调用者不用加\n】， 往标准错误上输出这个字符串；
//     如果err不为0，表示有错误，会将该错误编号以及对应的错误信息一并放到组合出的字符串中一起显示；

//《c++从入门到精通》里老师讲解过，比较典型的C语言中的写法，就是这种va_start,va_end
//fmt:通过这第一个普通参数来寻址后续的所有可变参数的类型及其值
//调用格式比如：ngx_log_stderr(0, "invalid option: \"%s\",%d", "testinfo",123);
 /* 
    // LOG_STDERR("invalid option: \"%s\"", argv[0]);  //nginx: invalid option: "./nginx"
    // LOG_STDERR("invalid option: %10d", 21);         //nginx: invalid option:         21  ---21前面有8个空格
    // LOG_STDERR( "invalid option: %.6f", 21.378);     //nginx: invalid option: 21.378000   ---%.这种只跟f配合有效，往末尾填充0
    // LOG_STDERR( "invalid option: %.6f", 12.999);     //nginx: invalid option: 12.999000
    // LOG_STDERR("invalid option: %.2f", 12.999);     //nginx: invalid option: 13.00
    // LOG_STDERR("invalid option: %xd", 1678);        //nginx: invalid option: 68E
    // LOG_STDERR("invalid option: %Xd", 1678);        //nginx: invalid option: 68E
    // LOG_STDERR1(15, "invalid option: %s , %d", "testInfo",326);        //nginx: invalid option: testInfo , 326
    // LOG_STDERR("invalid option: %d", 1678); 
*/
void LogStdErr(int iSystemErrCode, const char *fmt, ...)
{    
    va_list args;                        //创建一个va_list类型变量
    u_char  szErrBuff[NGX_MAX_ERROR_STR+1]; //2048  -- ************  +1是我自己填的，感谢官方写法有点小瑕疵，所以动手调整一下
    u_char  *pcWritePos = 0,  // 指向要写的位置
            *pcLast = 0;      // 指向缓冲区末尾（防止写过头）

    memset(szErrBuff, 0x00, sizeof(szErrBuff));     //我个人加的，这块有必要加，至少在va_end处理之前有必要，否则字符串没有结束标记不行的；***************************

    pcLast = szErrBuff + NGX_MAX_ERROR_STR;   //pcLast所致字符可写（他后面的是\0）
    pcWritePos = ngx_cpymem(szErrBuff, "nginx: ", 7);     //pcWritePos指向下一个可写位置    
    
    va_start(args, fmt); //使args指向起始的参数
    pcWritePos = NgxVslprintf(pcWritePos,pcLast,fmt,args); //组合出这个字符串保存在errstr里
    va_end(args);        //释放args

    if (iSystemErrCode)  //如果错误代码不是0，表示有错误发生
    {
        //错误代码和错误信息也要显示出来
        pcWritePos = LogSystemErr(pcWritePos, pcLast, iSystemErrCode);
    }
    
    //若位置不够，那换行也要硬插入到末尾，哪怕覆盖到其他内容    
    if (pcWritePos >= (pcLast - 1))
    {
        pcWritePos = (pcLast - 1) - 1; //把尾部空格留出来，这里感觉nginx处理的似乎就不对 
                             //我觉得，last-1，才是最后 一个而有效的内存，而这个位置要保存\0，所以我认为再减1，这个位置，才适合保存\n
    }
    *pcWritePos++ = '\n'; //增加个换行符    
    
    //往标准错误【一般是屏幕】输出信息    
    write(STDERR_FILENO,szErrBuff,pcWritePos - szErrBuff); //三章七节讲过，这个叫标准错误，一般指屏幕

    if(g_structNgxLog.fd > STDERR_FILENO) //日志文件有效:(本条件肯定成立)写到日志文件
    {
        //因为上边已经把err信息显示出来了，所以这里就不要显示了，否则显示重复了
        iSystemErrCode = 0;    //不要再次把错误信息弄到字符串里，否则字符串里重复了
        pcWritePos--;  *pcWritePos = 0; //把原来末尾的\n干掉，因为到ngx_log_err_core中还会加这个\n 
        LogErrorCore(NGX_LOG_STDERR,iSystemErrCode,(const char *)szErrBuff); 
    }    
    return;
}

/******************************************************************************************
函数原型: 
功能描述: 把系统错误号 iSystemErrCode 对应的错误描述取出来，格式化成形如"(系统错误号: 错误说明)"到 buff 中去
          这个函数我改造的比较多，和原始的nginx代码多有不同
参数说明:   名称            类型                说明
            pcBuff             u_char *
            last            u_char *
            iSystemErrCode  int
返 回 值: pcBuff
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 22时20分09秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
u_char *LogSystemErr(u_char *pcBuff, u_char *last, int iSystemErrCode)
{
    //以下代码是我自己改造，感觉作者的代码有些瑕疵
    char *perrorinfo = strerror(iSystemErrCode); // 根据资料不会返回NULL;
    size_t len = strlen(perrorinfo);

    //然后我还要插入一些字符串： (%d:)  
    char leftstr[10] = {0}; 
    sprintf(leftstr," (%d: ",iSystemErrCode);
    size_t leftlen = strlen(leftstr);

    char rightstr[] = ") "; 
    size_t rightlen = strlen(rightstr);
    
    size_t extralen = leftlen + rightlen; //左右的额外宽度
    if ((pcBuff + len + extralen) < last)
    {
        //保证整个我装得下，我就装，否则我全部抛弃 ,nginx的做法是 如果位置不够，就硬留出50个位置【哪怕覆盖掉以往的有效内容】，也要硬往后边塞，这样当然也可以；
        pcBuff = ngx_cpymem(pcBuff, leftstr, leftlen);
        pcBuff = ngx_cpymem(pcBuff, perrorinfo, len);
        pcBuff = ngx_cpymem(pcBuff, rightstr, rightlen);
    }
    return pcBuff;
}

//----------------------------------------------------------------------------------------------------------------------
// 往日志文件中写日志，代码中有自动加换行符，所以调用时字符串不用刻意加\n；
//    日过定向为标准错误，则直接往屏幕上写日志【比如日志文件打不开，则会直接定位到标准错误，此时日志就打印到屏幕上，参考ngx_log_init()】
// iLogLevel: 一个等级数字，我们把日志分成一些等级，以方便管理、显示、过滤等等，如果这个等级数字比配置文件中的等级数字"LogLevel"大，那么该条信息不被写到日志文件中
// iErr ：是个错误代码，如果不是0，就应该转换成显示对应的错误信息,一起写到日志文件中，
// LogErrorCore(5,8,"这个XXX工作的有问题,显示的结果是=%s","YYYY");
void LogErrorCore( int iLogLevel,  int iSystemErrCode, const char *fmt, ... )
{
    u_char  *last;
    u_char  szErrBuff[NGX_MAX_ERROR_STR+1];   //这个+1也是我放入进来的，本函数可以参考ngx_log_stderr()函数的写法；

    memset(szErrBuff,0,sizeof(szErrBuff));
    last = szErrBuff + NGX_MAX_ERROR_STR;
    
    struct timeval   tv;
    struct tm        tm;
    time_t           sec;   //秒
    u_char           *p;    //指向当前要拷贝数据到其中的内存位置
    va_list          args;

    memset(&tv,0,sizeof(struct timeval));    
    memset(&tm,0,sizeof(struct tm));

    gettimeofday(&tv, NULL);     //获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】        

    long lMS = tv.tv_usec%1000;  //毫秒
    sec = tv.tv_sec;             //秒
    localtime_r(&sec, &tm);      //把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
    tm.tm_mon++;                 //月份要调整下正常
    tm.tm_year += 1900;          //年份要调整下才正常
    
    u_char strcurrtime[40]={0};  //先组合出一个当前时间字符串，格式形如：2019-01-08 19:57:11.056
    NgxSlprintf(strcurrtime,  
                    (u_char *)-1,                       //若用一个u_char *接一个 (u_char *)-1,则 得到的结果是 0xffffffff....，这个值足够大
                    "%4d-%02d-%02d %02d:%02d:%02d.%03d",     //格式是 年-月-日 时:分:秒.毫秒
                    tm.tm_year, tm.tm_mon,
                    tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec, lMS);
    p = ngx_cpymem(szErrBuff,strcurrtime,strlen((const char *)strcurrtime));  //日期增加进来，得到形如：     2019-01-08 20:26:07
    p = NgxSlprintf(p, last, " [%s] ", g_szErrLevelNames[iLogLevel]);                //日志级别增加进来，得到形如：  2019-01-08 20:26:07 [crit] 
    p = NgxSlprintf(p, last, "PID=%P: ",g_CurrPID);                             //支持%P格式，进程id增加进来，得到形如：   2019-01-08 20:50:15 [crit] 2037:

    va_start(args, fmt);                     //使args指向起始的参数
    p = NgxVslprintf(p, last, fmt, args);   //把fmt和args参数弄进去，组合出来这个字符串
    va_end(args);                            //释放args 

    if (iSystemErrCode)  //如果错误代码不是0，表示有错误发生
    {
        //错误代码和错误信息也要显示出来
        p = LogSystemErr(p, last, iSystemErrCode);
    }

    //若位置不够，那换行也要硬插入到末尾，哪怕覆盖到其他内容
    if (p >= (last - 1))
    {
        p = (last - 1) - 1; //把尾部空格留出来，这里感觉nginx处理的似乎就不对 
                             //我觉得，last-1，才是最后 一个而有效的内存，而这个位置要保存\0，所以我认为再减1，这个位置，才适合保存\n
    }
    *p++ = '\n'; //增加个换行符       

    //这么写代码是图方便：随时可以把流程弄到while后边去；大家可以借鉴一下这种写法
    ssize_t   n;
    while(1) 
    {        
        if (iLogLevel > g_structNgxLog.log_level) 
        {
            //要打印的这个日志的等级太落后（等级数字太大，比配置文件中的数字大)
            //这种日志就不打印了
            break;
        }
        //磁盘是否满了的判断，先算了吧，还是由管理员保证这个事情吧； 

        //写日志文件        
        n = write( g_structNgxLog.fd, szErrBuff, p - szErrBuff );  //文件写入成功后，如果中途
        if (n == -1) 
        {
            //写失败有问题
            if(errno == ENOSPC) //写失败，且原因是磁盘没空间了
            {
                //磁盘没空间了
                //没空间还写个毛线啊
                //先do nothing吧；
            }
            else
            {
                //这是有其他错误，那么我考虑把这个错误显示到标准错误设备吧；
                if(g_structNgxLog.fd != STDERR_FILENO) //当前是定位到文件的，则条件成立
                {
                    n = write(STDERR_FILENO,szErrBuff,p - szErrBuff);
                }
            }
        }
        break;
    } //end while    
    return;
}

//----------------------------------------------------------------------------------------------------------------------
//描述：日志初始化，就是把日志文件打开 ，注意这里边涉及到释放的问题，如何解决？
void LogInit()
{
    u_char *plogname = NULL;
    size_t nlen;

    //从配置文件中读取和日志相关的配置信息
    CConfig *pConfig = CConfig::GetInstance();
    plogname = (u_char *)pConfig->GetString("Log");
    if(plogname == NULL)
    {
        //没读到，就要给个缺省的路径文件名了
        plogname = (u_char *) NGX_ERROR_LOG_PATH; //"logs/error.log" ,logs目录需要提前建立出来
    }
    g_structNgxLog.log_level = pConfig->GetIntDefault("LogLevel",NGX_LOG_NOTICE);//缺省日志等级为6【注意】 ，如果读失败，就给缺省日志等级
    //nlen = strlen((const char *)plogname);

    //只写打开|追加到末尾|文件不存在则创建【这个需要跟第三参数指定文件访问权限】
    //mode = 0644：文件访问权限， 6: 110    , 4: 100：     【用户：读写， 用户所在组：读，其他：读】 老师在第三章第一节介绍过
    //g_structNgxLog.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT|O_DIRECT,0644);   //绕过内和缓冲区，write()成功则写磁盘必然成功，但效率可能会比较低；
    g_structNgxLog.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT,0644);  // 注：O_APPEND 能保证多个进程同时写日志不会相互覆盖!!!
    if (g_structNgxLog.fd == -1)  //如果有错误，则直接定位到 标准错误上去 
    {
        LOG_STDERR1(errno,"[alert] could not open error log file: open() \"%s\" failed!", plogname);
        g_structNgxLog.fd = STDERR_FILENO; //直接定位到标准错误去了        
    } 
    return;
}
