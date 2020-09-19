/******************************************************************************************
类    名: CThreadPool
头 文 件: _include/ngx_c_threadpool.h
实现文件: misc/ngx_c_threadpool.cxx
功能描述: 线程池相关类
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 13时58分00秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
#pragma once

#include <vector>
#include <pthread.h>
#include <atomic>   //c++11里的原子操作

class CThreadPool
{
public:
    //构造函数
    CThreadPool();               
    
    //析构函数
    ~CThreadPool();                           

public:
    bool Create(int threadNum);                     //创建该线程池中的所有线程
    void StopAll();                                 //使线程池中的所有线程退出

    void inMsgRecvQueueAndSignal(char *pcBuff);        //收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
    void Call();                                    //来任务了，调一个线程池中的线程下来干活  
    int  getRecvMsgQueueCount(){return m_iRecvMsgQueueCount;} //获取接收消息队列大小

private:
    static void* ThreadFunc(void *pvThreadData);      //新线程的线程回调函数    
	//char *outMsgRecvQueue();                        //将一个消息出消息队列	，不需要，直接在ThreadFunc()中处理
    void clearMsgRecvQueue();                       //清理接收消息队列

private:
    //定义一个 线程池中的 线程 的结构，以后可能做一些统计之类的 功能扩展，所以引入这么个结构来 代表线程 感觉更方便一些；    
    struct ThreadItem   
    {
        pthread_t   m_hThread;  //线程句柄
        CThreadPool *m_pThreadPool;    //记录线程池的指针	
        bool        m_bIsRunning;  //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        ThreadItem(CThreadPool *pthis):m_pThreadPool(pthis),m_bIsRunning(false){}                             
        ~ThreadItem(){}        
    };

private:
    static pthread_mutex_t     m_pthreadMutex;      //线程同步互斥量/也叫线程同步锁
    static pthread_cond_t      m_pthreadCond;       //线程同步条件变量
    static bool                m_bIsShutDown;          //线程退出标志，false不退出，true退出

    int                        m_iThreadNum;        //要创建的线程数量

    //int                        m_iRunningThreadNum; //线程数, 运行中的线程数	
    std::atomic<int>           m_iRunningThreadNum; //线程数, 运行中的线程数，原子操作
    time_t                     m_iLastEmgTime;      //上次发生线程不够用【紧急事件】的时间,防止日志报的太频繁
    //time_t                     m_iPrintInfoTime;    //打印信息的一个间隔时间，我准备10秒打印出一些信息供参考和调试
    //time_t                     m_iCurrTime;         //当前时间

    std::vector<ThreadItem *>  m_vectorThread;      //线程 容器，容器里就是各个线程了 

    //接收消息队列相关
    std::list<char *>          m_MsgRecvQueue;      //接收数据消息队列 
	int                        m_iRecvMsgQueueCount;//收消息队列大小
};

