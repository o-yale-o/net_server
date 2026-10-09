#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/time.h>

#include "ngx_c_msgroute.h"
#include "ngx_macro.h"
#include "ngx_func.h"

static const int _TABLE_SIZE_ = _ROUTE_SLOTS_;   //信箱槽位数

//共享内存布局: [进程共享互斥量][槽位数组]
struct ROUTE_SHM
{
	pthread_mutex_t   mutex;
	ROUTE_MSG_ITEM    items[_TABLE_SIZE_];
};

#define SHM_NAME "/net_server_msgroute"

CMsgRoute::CMsgRoute()
{
	m_iShmFd  = -1;
	m_pShm    = NULL;
	m_bInited = false;
}

CMsgRoute* CMsgRoute::GetInstance() //单例：C++11 Meyers单例
{
	static CMsgRoute instance;
	return &instance;
}

/******************************************************************************************
函数原型: 
功能描述: 初始化：master创建共享内存[必须在fork之前调用]；worker挂接
返 回 值: 成功true 失败false
被引用于: NgxMasterProcessCycle()/NgxWorkerProcessInit()
创建日期: 2026年10月09日
修改记录: 
******************************************************************************************/
bool CMsgRoute::Init(bool bIsMaster)
{
	if(m_bInited == true)  //幂等
		return true;

	int iFd = -1;
	if(bIsMaster == true)
	{
		shm_unlink(SHM_NAME);  //清理残留
		iFd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
		if(iFd == -1)
		{
			LOG_STDERR1(errno, "CMsgRoute::Init()中shm_open()创建失败!");
			return false;
		}
		if(ftruncate(iFd, sizeof(ROUTE_SHM)) == -1)
		{
			LOG_STDERR1(errno, "CMsgRoute::Init()中ftruncate()失败!");
			close(iFd);
			shm_unlink(SHM_NAME);
			return false;
		}
	}
	else
	{
		iFd = shm_open(SHM_NAME, O_RDWR, 0600);
		if(iFd == -1)
		{
			LOG_STDERR1(errno, "CMsgRoute::Init()中shm_open()打开失败[master是否先创建?]!");
			return false;
		}
	}

	void *pAddr = mmap(NULL, sizeof(ROUTE_SHM), PROT_READ | PROT_WRITE, MAP_SHARED, iFd, 0);
	if(pAddr == MAP_FAILED)
	{
		LOG_STDERR1(errno, "CMsgRoute::Init()中mmap()失败!");
		close(iFd);
		if(bIsMaster == true) shm_unlink(SHM_NAME);
		return false;
	}

	if(bIsMaster == true)
	{
		ROUTE_SHM *pShm = (ROUTE_SHM *)pAddr;
		memset(pShm, 0, sizeof(ROUTE_SHM));

		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&pShm->mutex, &attr);
		pthread_mutexattr_destroy(&attr);
	}

	m_iShmFd  = iFd;
	m_pShm    = pAddr;
	m_bInited = true;

	LOG_INFO("消息路由信箱初始化成功[%s进程], 槽位数=%d, 共享内存=%d字节。",
	         bIsMaster == true ? "master" : "worker", _TABLE_SIZE_, (int)sizeof(ROUTE_SHM));
	return true;
}

bool CMsgRoute::Lock()
{
	if(m_bInited == false)
		return false;
	return pthread_mutex_lock(&((ROUTE_SHM *)m_pShm)->mutex) == 0;
}

void CMsgRoute::Unlock()
{
	if(m_bInited == true)
		pthread_mutex_unlock(&((ROUTE_SHM *)m_pShm)->mutex);
}

/******************************************************************************************
函数原型: 
功能描述: 投入一条消息(任意worker均可调用)
参数说明:   uiToUid   目标用户uid
            iCmd      到达目标连接时的命令字
            pcBody    消息体
            iBodyLen  消息体长度(不得超过_ROUTE_BODY_SIZE_)
返 回 值: true=已投入 false=失败(信箱满/未初始化/超长)
******************************************************************************************/
bool CMsgRoute::PutMsg(uint64_t uiToUid, int iCmd, const char *pcBody, int iBodyLen)
{
	if(iBodyLen > _ROUTE_BODY_SIZE_ || iBodyLen < 0)
		return false;

	if(Lock() == false)
		return false;

	ROUTE_SHM *pShm = (ROUTE_SHM *)m_pShm;
	bool bRet = false;
	for(int i = 0; i < _TABLE_SIZE_; i++)
	{
		if(pShm->items[i].uiState == 0)
		{
			struct timeval tv;
			gettimeofday(&tv, NULL);
			pShm->items[i].uiState  = 1;
			pShm->items[i].uiToUid  = uiToUid;
			pShm->items[i].iCmd     = iCmd;
			pShm->items[i].iBodyLen = iBodyLen;
			pShm->items[i].uiTimePut = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
			if(iBodyLen > 0)
				memcpy(pShm->items[i].acBody, pcBody, iBodyLen);
			bRet = true;
			break;
		}
	}

	Unlock();
	return bRet;
}

/******************************************************************************************
函数原型: 
功能描述: 遍历信箱：已投递/已过期的槽位被回收；未过期且无人认领的保留给归属worker
参数说明:   pCtx         回调上下文(通常是CSocekt*)
            fn           投递回调
            uiExpireMs   过期毫秒数(超时无人认领即丢弃, 防止槽位泄漏)
******************************************************************************************/
void CMsgRoute::ProcessAll(void *pCtx, FnOnMsg fn, uint64_t uiExpireMs)
{
	if(Lock() == false)
		return;

	ROUTE_SHM *pShm = (ROUTE_SHM *)m_pShm;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t uiNow = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

	for(int i = 0; i < _TABLE_SIZE_; i++)
	{
		if(pShm->items[i].uiState != 1)
			continue;

		//先构造回调参数(回调运行期间仍持锁, 保证槽位操作的互斥)
		bool bHandled = fn(pCtx,
		                   pShm->items[i].uiToUid,
		                   pShm->items[i].iCmd,
		                   pShm->items[i].acBody,
		                   pShm->items[i].iBodyLen,
		                   pShm->items[i].uiTimePut);

		if(bHandled == true || (uiNow - pShm->items[i].uiTimePut) > uiExpireMs)
		{
			//已投递 或 已过期无人认领: 回收槽位
			pShm->items[i].uiState = 0;
		}
	}

	Unlock();
	return;
}
