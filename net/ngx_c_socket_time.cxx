//和网络 中 时间 有关的函数放这里

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
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

/******************************************************************************************
函数原型: 
功能描述: 设置踢出时钟(向multimap表中增加内容)，用户三次握手成功连入，然后我们开启了踢人开关【Sock_WaitTimeEnable = 1】，那么本函数被调用；
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时11分53秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CSocekt::AddToTimerQueue(lpngx_connection_t pConn)
{
    CMemory *pMemory = CMemory::GetInstance();

    time_t futtime = time(NULL);
    futtime += m_iWaitTime;  //20秒之后的时间

    CLock lock(&m_mutexTimeMap); //互斥，因为要操作m_timeQueuemap了
    LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)pMemory->AllocMemory(m_iLenMsgHeader,false);
    tmpMsgHeader->pConn = pConn;
    tmpMsgHeader->iCurrSequence = pConn->iCurrSequence;
    m_mapTime.insert(std::make_pair(futtime,tmpMsgHeader)); //按键 自动排序 小->大
    m_lTimeMaxCurrSize++;  //计时队列尺寸+1
    m_timeValue = GetEarliestTime(); //计时队列头部时间值保存到 m_timeValue 里
    return;    
}

/******************************************************************************************
函数原型: 
功能描述: 从multimap中取得最早的时间返回去，调用者负责互斥，所以本函数不用互斥，调用者确保m_timeQueuemap中一定不为空
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时11分41秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
time_t CSocekt::GetEarliestTime()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;	
	pos = m_mapTime.begin();		
	return pos->first;	
}

/******************************************************************************************
函数原型: 
功能描述: 从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时11分31秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
LPSTRUC_MSG_HEADER CSocekt::RemoveFirstTimer()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;	
	LPSTRUC_MSG_HEADER p_tmp;
	if(m_lTimeMaxCurrSize <= 0)
	{
		return NULL;
	}
	pos = m_mapTime.begin(); //调用者负责互斥的，这里直接操作没问题的
	p_tmp = pos->second;
	m_mapTime.erase(pos);
	--m_lTimeMaxCurrSize;
	return p_tmp;
}

/******************************************************************************************
函数原型: 
功能描述: 根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点
		  调用者负责互斥，所以本函数不用互斥
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时11分10秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
LPSTRUC_MSG_HEADER CSocekt::GetOverTimeTimer(time_t cur_time)
{	
	CMemory *pMemory = CMemory::GetInstance();
	LPSTRUC_MSG_HEADER ptmp;

	if (m_lTimeMaxCurrSize == 0 || m_mapTime.empty())
		return NULL; //队列为空

	time_t earliesttime = GetEarliestTime(); //到multimap中去查询
	if (earliesttime <= cur_time)
	{
		//这回确实是有到时间的了【超时的节点】
		ptmp = RemoveFirstTimer();    //把这个超时的节点从 m_mapTime 删掉，并把这个节点的第二项返回来；

		if(/*m_iIsOpenKickTimer == 1 && */m_ifTimeOutKick != 1)  //能调用到本函数第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
		{
			//如果不是要求超时就提出，则才做这里的事：

			//因为下次超时的时间我们也依然要判断，所以还要把这个节点加回来        
			time_t newinqueutime = cur_time+(m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)pMemory->AllocMemory(sizeof(STRUC_MSG_HEADER),false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrSequence = ptmp->iCurrSequence;			
			m_mapTime.insert(std::make_pair(newinqueutime,tmpMsgHeader)); //自动排序 小->大			
			m_lTimeMaxCurrSize++;       
		}

		if(m_lTimeMaxCurrSize > 0) //这个判断条件必要，因为以后我们可能在这里扩充别的代码
		{
			m_timeValue = GetEarliestTime(); //计时队列头部时间值保存到m_timer_value_里
		}
		return ptmp;
	}
	return NULL;
}

/******************************************************************************************
函数原型: 
功能描述: 把指定用户tcp连接从timer表中抠出去
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时10分54秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CSocekt::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;
	CMemory *pMemory = CMemory::GetInstance();

    CLock lock(&m_mutexTimeMap);

    //因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
	pos    = m_mapTime.begin();
	posend = m_mapTime.end();
	for(; pos != posend; ++pos)	
	{
		if(pos->second->pConn == pConn)
		{			
			pMemory->FreeMemory(pos->second);  //释放内存
			m_mapTime.erase(pos);
			--m_lTimeMaxCurrSize; //减去一个元素，必然要把尺寸减少1个;								
			goto lblMTQM;
		}		
	}
	if(m_lTimeMaxCurrSize > 0)
	{
		m_timeValue = GetEarliestTime();
	}
    return;    
}

/******************************************************************************************
函数原型: 
功能描述: 清理时间队列中所有内容
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时10分40秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CSocekt::clearAllFromTimerQueue()
{	
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;

	CMemory *pMemory = CMemory::GetInstance();	
	pos    = m_mapTime.begin();
	posend = m_mapTime.end();    
	for(; pos != posend; ++pos)	
	{
		pMemory->FreeMemory(pos->second);		
		--m_lTimeMaxCurrSize; 		
	}
	m_mapTime.clear();
}

/******************************************************************************************
函数原型: 
功能描述: 时间队列监视和处理线程，处理到期不发心跳包的用户踢出的线程
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时10分29秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void* CSocekt::ServerTimerQueueMonitorThread(void* pvThreadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(pvThreadData);
    CSocekt *pSocket = pThread->m_pSocekt;

    time_t absolute_time,cur_time;
    int iErr;

    while(g_iStopEvent == 0) //不退出
    {
        //这里没互斥判断，所以只是个初级判断，目的至少是队列为空时避免系统损耗		
		if(pSocket->m_lTimeMaxCurrSize > 0)//队列不为空，有内容
        {
			//时间队列中最近发生事情的时间放到 absolute_time里；
            absolute_time = pSocket->m_timeValue; //这个可是省了个互斥，十分划算
            cur_time = time(NULL);
            if(absolute_time < cur_time)
            {
                //时间到了，可以处理了
                std::list<LPSTRUC_MSG_HEADER> m_lsIdleList; //保存要处理的内容
                LPSTRUC_MSG_HEADER result;

                iErr = pthread_mutex_lock(&pSocket->m_mutexTimeMap);  
                if(iErr != 0) 
				{
					LOG_STDERR1(iErr, "pthread_mutex_lock()失败!");//有问题，要及时报告
				}

                while ((result = pSocket->GetOverTimeTimer(cur_time)) != NULL) //一次性的把所有超时节点都拿过来
				{
					m_lsIdleList.push_back(result); 
				}//end while
                iErr = pthread_mutex_unlock(&pSocket->m_mutexTimeMap); 
                if(iErr != 0)  
				{
					LOG_STDERR1(iErr, "pthread_mutex_unlock()失败!");//有问题，要及时报告
				}

                LPSTRUC_MSG_HEADER tmpmsg;
                while(!m_lsIdleList.empty())
                {
                    tmpmsg = m_lsIdleList.front();
					m_lsIdleList.pop_front(); 
                    pSocket->ProcessIdleTimeOut(tmpmsg,cur_time); //这里需要检查心跳超时问题
                } //end while(!m_lsIdleList.empty())
            }
        } //end if(pSocket->m_lTimeMaxCurrSize > 0)
        
        usleep(500 * 1000); //为简化问题，我们直接每次休息500毫秒
    } //end while

    return (void*)0;
}

/******************************************************************************************
函数原型: 
功能描述: 心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 19时10分05秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CSocekt::ProcessIdleTimeOut(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time)
{
	CMemory *pMemory = CMemory::GetInstance();
	pMemory->FreeMemory(tmpmsg);    
}


