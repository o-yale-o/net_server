//和 线程池 有关的函数放这里

#include <stdarg.h>
#include <string.h>
#include <unistd.h>  //usleep

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  //#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;     //#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_bIsShutDown = false;    //刚开始标记整个线程池的线程是不退出的      

//构造函数
CThreadPool::CThreadPool()
{
    m_iRunningThreadNum = 0;  //正在运行的线程，开始给个0【注意这种写法：原子的对象给0也可以直接赋值，当整型变量来用】
    m_iLastEmgTime = 0;       //上次报告线程不够用了的时间；
    //m_iPrintInfoTime = 0;    //上次打印参考信息的时间；
    m_iRecvMsgQueueCount = 0; //收消息队列
}

//析构函数
CThreadPool::~CThreadPool()
{    
    //资源释放在StopAll()里统一进行，就不在这里进行了

    //接收消息队列中内容释放
    clearMsgRecvQueue();
}

/******************************************************************************************
函数原型: 
功能描述: 清理接收消息队列，注意这个函数的写法
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 13时31分16秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CThreadPool::clearMsgRecvQueue()
{
	char * sTmpMempoint;
	CMemory *pMemory = CMemory::GetInstance();

	//尾声阶段，需要互斥？该退的都退出了，该停止的都停止了，应该不需要退出了
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		pMemory->FreeMemory(sTmpMempoint);
	}	
}

/******************************************************************************************
函数原型: 
功能描述: 创建线程池中的线程(要手工调用，不在构造函数里调用了)
参数说明:   名称            类型                说明

返 回 值: 所有线程都创建成功则返回true，出现错误则返回false
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 13时31分51秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
bool CThreadPool::Create(int threadNum)
{    
    ThreadItem *pNew = nullptr;
    int iErr = 0;

    m_iThreadNum = threadNum; //保存要创建的线程数量    
    
    for(int i = 0; i < m_iThreadNum; ++i)
    {
        m_vectorThread.push_back(pNew = new ThreadItem(this));             //创建 一个新线程对象 并入到容器中         
        iErr = pthread_create(&pNew->m_hThread, NULL, ThreadFunc, pNew);   //创建线程，错误不返回到errno，一般返回错误码
        if(iErr != 0)
        {
            //创建线程有错
            LOG_STDERR1(iErr, "创建线程%d失败!", i );
            return false;
        }
        else
        {
            //创建线程成功
            //LogStdErr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->m_hThread);
        }        
    } //end for

    //我们必须保证每个线程都启动并运行到 pthread_cond_wait()，本函数才返回，只有这样，这几个线程才能进行后续的正常工作 
    std::vector<ThreadItem*>::iterator iter;
lblfor:
    for(iter = m_vectorThread.begin(); iter != m_vectorThread.end(); iter++)
    {
        if( (*iter)->m_bIsRunning == false) //这个条件保证所有线程完全启动起来，以保证整个线程池中的线程正常工作；
        {
            //这说明有没有启动完全的线程
            usleep(100 * 1000);  //单位是微妙,又因为1毫秒=1000微妙，所以 100 *1000 = 100毫秒
            goto lblfor;
        }
    }
    return true;
}

/******************************************************************************************
函数原型: 
功能描述: 线程函数（当用 pthread_create() 创建线程后，这个 ThreadFunc() 函数都会被立即执行）
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 08时29分45秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void* CThreadPool::ThreadFunc(void* pvThreadData)
{
    //这个是静态成员函数，是不存在this指针的；
    ThreadItem  *pThread = static_cast<ThreadItem*>(pvThreadData);
    CThreadPool *pThreadPool = pThread->m_pThreadPool;
    
    CMemory *pMemory = CMemory::GetInstance();	    
    int iErr;

    pthread_t tid = pthread_self(); //获取线程自身id，以方便调试打印信息等    
    while(true)
    {
        //线程用pthread_mutex_lock()函数去锁定指定的mutex变量，若该mutex已经被另外一个线程锁定了，该调用将会阻塞线程直到mutex被解锁。  
        iErr = pthread_mutex_lock(&m_pthreadMutex);  
        if(iErr != 0) 
        {
            LOG_STDERR1(iErr, "CThreadPool::ThreadFunc()中pthread_mutex_lock()失败!" );//有问题，要及时报告
        }

        //以下这行程序写法技巧十分重要，必须要用while这种写法，
        //因为：pthread_cond_wait()是个值得注意的函数，调用一次pthread_cond_signal()可能会唤醒多个【惊群】【官方描述是 至少一个/pthread_cond_signal 在多处理器上可能同时唤醒多个线程】
        //老师也在《c++入门到精通 c++ 98/11/14/17》里第六章第十三节谈过虚假唤醒，实际上是一个意思；
        //老师也在《c++入门到精通 c++ 98/11/14/17》里第六章第八节谈过条件变量、wait()、notify_one()、notify_all()，其实跟这里的pthread_cond_wait、pthread_cond_signal、pthread_cond_broadcast非常类似
        //pthread_cond_wait()函数，如果只有一条消息 唤醒了两个线程干活，那么其中有一个线程拿不到消息，那如果不用while写，就会出问题，所以被惊醒后必须再次用while拿消息，拿到才走下来；
        //while( (jobbuf = g_LogicSocket.outMsgRecvQueue()) == NULL && m_bIsShutDown == false)
        while ( (pThreadPool->m_MsgRecvQueue.size() == 0) && m_bIsShutDown == false)
        {
            //如果这个pthread_cond_wait被唤醒【被唤醒后程序执行流程往下走的前提是拿到了锁--官方：pthread_cond_wait()返回时，互斥量再次被锁住】，
            //那么会立即再次执行g_socket.outMsgRecvQueue()，如果拿到了一个NULL，则继续在这里wait着();
            if(pThread->m_bIsRunning == false)
            {
                pThread->m_bIsRunning = true; //标记为true了才允许调用StopAll()：测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱，所以每个线程必须执行到这里，才认为是启动成功了；
            }            
            
            //LOG_STDERR("执行 pthread_cond_wait(), 开始等待...");
            //刚开始执行pthread_cond_wait()的时候，会卡在这里，而且m_pthreadMutex会被释放掉；
            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex); //整个服务器程序刚初始化完，线程启动后就卡在这等待...
            //LOG_STDERR("pthread_cond_wait()唤醒.");
        }

        //能走下来的，必然是 拿到了真正的 消息队列中的数据   或者 m_bIsShutDown == true

        /*
        jobbuf = g_LogicSocket.outMsgRecvQueue(); //从消息队列中取消息
        if( jobbuf == NULL && m_bIsShutDown == false)
        {
            //消息队列为空，并且不要求退出，则
            //pthread_cond_wait()阻塞调用线程直到指定的条件有信号（signaled）。
                //该函数应该在互斥量锁定时调用，当在等待时会自动解锁互斥量【这是重点】。在信号被发送，线程被激活后，互斥量会自动被锁定，当线程结束时，由程序员负责解锁互斥量。  
                  //说白了，某个地方调用了pthread_cond_signal(&m_pthreadCond);，这个pthread_cond_wait就会走下来；

           LOG_STDERR("--------------即将调用pthread_cond_wait,tid=%d--------------",tid);


            if(pThread->m_bIsRunning == false)
                pThread->m_bIsRunning = true; //标记为true了才允许调用StopAll()：测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱，所以每个线程必须执行到这里，才认为是启动成功了；

            iErr = pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
            if(iErr != 0) LOG_STDERR1(iErr,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",iErr);//有问题，要及时报告



           LOG_STDERR("--------------调用pthread_cond_wait完毕,tid=%d--------------",tid);
        }
        */
        //if(!m_bIsShutDown)  //如果这个条件成立，表示肯定是拿到了真正消息队列中的数据，要去干活了，干活，则表示正在运行的线程数量要增加1；
        //    ++m_iRunningThreadNum; //因为这里是互斥的，所以这个+是OK的；

        //走到这里时刻，互斥量肯定是锁着的。。。。。。

        //先判断线程退出这个条件
        if(m_bIsShutDown)
        {   
            pthread_mutex_unlock(&m_pthreadMutex); //解锁互斥量
            break;                     
        }

        //走到这里，可以取得消息进行处理了【消息队列中必然有消息】,注意，目前还是互斥着呢
        char *jobbuf = pThreadPool->m_MsgRecvQueue.front();     //返回第一个元素但不检查元素存在与否
        pThreadPool->m_MsgRecvQueue.pop_front();                //移除第一个元素但不返回	
        --pThreadPool->m_iRecvMsgQueueCount;                    //收消息队列数字-1
               
        //可以解锁互斥量了
        iErr = pthread_mutex_unlock(&m_pthreadMutex); 
        if(iErr != 0)  
        {
            LOG_STDERR1(iErr,"pthread_mutex_unlock()失败!");//有问题，要及时报告
        }
        
        //能走到这里的，就是有消息可以处理，开始处理
        ++pThreadPool->m_iRunningThreadNum;    //原子+1【记录正在干活的线程数量增加1】，这比互斥量要快很多

        g_LogicSocket.ProcessClientRequest(jobbuf);     //处理消息队列中来的消息

        //LogStdErr(0,"执行开始---begin,tid=%ui!",tid);
        //sleep(5); //临时测试代码
        //LogStdErr(0,"执行结束---end,tid=%ui!",tid);

        pMemory->FreeMemory(jobbuf);              //释放消息内存 
        --pThreadPool->m_iRunningThreadNum;     //原子-1【记录正在干活的线程数量减少1】

    } //end while(true)

    //能走出来表示整个程序要结束啊，怎么判断所有线程都结束？
    return (void*)0;
}

/******************************************************************************************
函数原型: 
功能描述: 停止所有线程【等待结束线程池中所有线程，该函数返回后，应该是所有线程池中线程都结束了】
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 13时32分28秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CThreadPool::StopAll() 
{
    //(1)已经调用过，就不要重复调用了
    if(m_bIsShutDown == true)
    {
        return;
    }
    m_bIsShutDown = true;

    //(2)唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，一定要在改变条件状态以后再给线程发信号
    int iErr = pthread_cond_broadcast(&m_pthreadCond); 
    if(iErr != 0)
    {
        //这肯定是有问题，要打印紧急日志
        LOG_STDERR1(iErr,"StopAll()中pthread_cond_broadcast()失败!");
        return;
    }

    //(3)等等线程，让线程真返回    
    std::vector<ThreadItem*>::iterator iter;
	for(iter = m_vectorThread.begin(); iter != m_vectorThread.end(); iter++)
    {
        pthread_join((*iter)->m_hThread, NULL); //等待一个线程终止
    }

    //流程走到这里，那么所有的线程池中的线程肯定都返回了；
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);    

    //(4)释放一下new出来的ThreadItem【线程池中的线程】    
	for(iter = m_vectorThread.begin(); iter != m_vectorThread.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_vectorThread.clear();

    LOG_INFO("线程池中线程全部正常结束.");
    return;    
}

/******************************************************************************************
函数原型: 
功能描述: 收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 13时32分43秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CThreadPool::inMsgRecvQueueAndSignal(char *pcBuff)
{
    //互斥
    int iErr = pthread_mutex_lock(&m_pthreadMutex);     
    if(iErr != 0)
    {
        LOG_STDERR1(iErr,"pthread_mutex_lock()失败!");
    }
        
    m_MsgRecvQueue.push_back(pcBuff);   // 入消息队列
    ++m_iRecvMsgQueueCount;             // 收消息队列数字+1，个人认为用变量更方便一点，比 m_MsgRecvQueue.size()高效

    //取消互斥
    iErr = pthread_mutex_unlock(&m_pthreadMutex);   
    if(iErr != 0)
    {
        LOG_STDERR1(iErr,"pthread_mutex_unlock()失败!");
    }

    //可以激发一个线程来干活了
    Call();                                  
    return;
}

/******************************************************************************************
函数原型: 
功能描述: 来任务了，调一个线程池中的线程下来干活
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月14日 13时27分12秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CThreadPool::Call()
{
    //LogStdErr(0,"m_pthreadCondbegin--------------=%ui!",m_pthreadCond);  //数字5，此数字不靠谱
    //for(int i = 0; i <= 100; i++)
    //{
    int iErr = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if(iErr != 0 )
    {
        //这是有问题啊，要打印日志啊
        LOG_STDERR1(iErr,"pthread_cond_signal()失败!");
    }
    //}
    //唤醒完100次，试试打印下m_pthreadCond值;
    //LOG_INFO("m_pthreadCondend--------------=%ui!",m_pthreadCond);  //数字1
    
    //(1)如果当前的工作线程全部都忙，则要报警
    //bool ifallthreadbusy = false;
    if(m_iThreadNum == m_iRunningThreadNum) //线程池中线程总量，跟当前正在干活的线程数量一样，说明所有线程都忙碌起来，线程不够用了
    {        
        //线程不够用了
        //ifallthreadbusy = true;
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) //最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            //两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，频繁报告日志
            m_iLastEmgTime = currtime;  //更新时间
            LOG_ALERT("线程池空闲线程数量为0，要考虑扩容线程池了!");
        }
    } //end if 

/*
    //-------------------------------------------------------如下内容都是一些测试代码；
    //唤醒丢失？--------------------------------------------------------------------------
    //(2)整个工程中，只在一个线程（主线程）中调用了Call，所以不存在多个线程调用Call的情形。
    if(ifallthreadbusy == false)
    {
        //有空闲线程  ，有没有可能我这里调用   pthread_cond_signal()，但因为某个时刻线程曾经全忙过，导致本次调用 pthread_cond_signal()并没有激发某个线程的pthread_cond_wait()执行呢？
           //我认为这种可能性不排除，这叫 唤醒丢失。如果真出现这种问题，我们如何弥补？
        if(irmqc > 5) //我随便来个数字比如给个5吧
        {
            //如果有空闲线程，并且 接收消息队列中超过5条信息没有被处理，则我总感觉可能真的是 唤醒丢失
            //唤醒如果真丢失，我是否考虑这里多唤醒一次？以尝试逐渐补偿回丢失的唤醒？此法是否可行，我尚不可知，我打印一条日志【其实后来仔细相同：唤醒如果真丢失，也无所谓，因为ThreadFunc()会一直处理直到整个消息队列为空】
           LOG_STDERR("CThreadPool::Call()中感觉有唤醒丢失发生，irmqc = %d!",irmqc);

            int iErr = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
            if(iErr != 0 )
            {
                //这是有问题啊，要打印日志啊
                LOG_STDERR1(iErr,"CThreadPool::Call()中pthread_cond_signal 2()失败，返回的错误码为%d!",iErr);
            }
        }
    }  //end if

    //(3)准备打印一些参考信息【10秒打印一次】,当然是有触发本函数的情况下才行
    m_iCurrTime = time(NULL);
    if(m_iCurrTime - m_iPrintInfoTime > 10)
    {
        m_iPrintInfoTime = m_iCurrTime;
        int irunn = m_iRunningThreadNum;
       LOG_STDERR("信息：当前消息队列中的消息数为%d,整个线程池中线程数量为%d,正在运行的线程数量为 = %d!",irmqc,m_iThreadNum,irunn); //正常消息，三个数字为 1，X，0
    }
    */
    return;
}

//唤醒丢失问题，sem_t sem_write;
//参考信号量解决方案：https://blog.csdn.net/yusiguyuan/article/details/20215591  linux多线程编程--信号量和条件变量 唤醒丢失事件
