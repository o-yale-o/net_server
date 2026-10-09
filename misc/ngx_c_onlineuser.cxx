#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>

#include "ngx_c_onlineuser.h"
#include "ngx_macro.h"
#include "ngx_func.h"   //LogErrorCore等函数声明

static const int _TABLE_SIZE_ = 4096;   //在线用户表槽位总数(线性探测哈希)

//共享内存布局: [进程共享互斥量][在线计数][槽位数组]
struct ONLINE_USER_SHM
{
	pthread_mutex_t        mutex;                       //进程共享互斥量，保护整张表
	int                    iOnlineCount;                //当前在线人数
	ONLINE_USER_ITEM       items[_TABLE_SIZE_];         //哈希槽位数组
};

#define SHM_NAME "/net_server_online_user"              //共享内存名(实际落在/dev/shm下)

COnlineUserTable::COnlineUserTable()
{
	m_iShmFd  = -1;
	m_pShm    = NULL;
	m_bInited = false;
}

COnlineUserTable* COnlineUserTable::GetInstance() //单例：C++11 Meyers单例，函数内static的初始化由编译器保证线程安全
{
	static COnlineUserTable instance;
	return &instance;
}

/******************************************************************************************
函数原型: 
功能描述: 初始化：master创建共享内存并初始化[必须在fork之前调用]；worker挂接已存在的共享内存
参数说明:   bIsMaster   bool    true=master进程
返 回 值: 成功true 失败false
依 赖 于: 
被引用于: NgxMasterProcessCycle()/NgxWorkerProcessInit()
创建日期: 2026年10月09日
修改记录: 
******************************************************************************************/
bool COnlineUserTable::Init(bool bIsMaster)
{
	if(m_bInited == true)  //幂等保护
		return true;

	//(1)取得共享内存fd: master独占创建；worker打开master已创建好的
	int iFd = -1;
	if(bIsMaster == true)
	{
		shm_unlink(SHM_NAME);  //清除可能残留的旧共享内存(不存在则忽略错误)
		iFd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
		if(iFd == -1)
		{
			LOG_STDERR1(errno, "COnlineUserTable::Init()中shm_open()创建共享内存失败!");
			return false;
		}
		if(ftruncate(iFd, sizeof(ONLINE_USER_SHM)) == -1)  //把共享内存扩到需要的大小
		{
			LOG_STDERR1(errno, "COnlineUserTable::Init()中ftruncate()失败!");
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
			LOG_STDERR1(errno, "COnlineUserTable::Init()中shm_open()打开共享内存失败[master是否先创建?]!");
			return false;
		}
	}

	//(2)映射到本进程地址空间
	void *pAddr = mmap(NULL, sizeof(ONLINE_USER_SHM), PROT_READ | PROT_WRITE, MAP_SHARED, iFd, 0);
	if(pAddr == MAP_FAILED)
	{
		LOG_STDERR1(errno, "COnlineUserTable::Init()中mmap()失败!");
		close(iFd);
		if(bIsMaster == true) shm_unlink(SHM_NAME);
		return false;
	}

	//(3)master负责初始化共享内存内容: 进程共享互斥量 + 清零
	if(bIsMaster == true)
	{
		ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)pAddr;
		memset(pShm, 0, sizeof(ONLINE_USER_SHM));

		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);  //关键：跨进程共享该互斥量
		pthread_mutex_init(&pShm->mutex, &attr);
		pthread_mutexattr_destroy(&attr);
	}

	m_iShmFd  = iFd;
	m_pShm    = pAddr;
	m_bInited = true;

	LOG_INFO("在线用户表初始化成功[%s进程], 表大小=%d, 共享内存=%d字节。",
	         bIsMaster == true ? "master" : "worker", _TABLE_SIZE_, (int)sizeof(ONLINE_USER_SHM));
	return true;
}

/******************************************************************************************
函数原型: 
功能描述: 加锁/解锁(进程共享互斥量)
******************************************************************************************/
bool COnlineUserTable::Lock()
{
	if(m_bInited == false)
		return false;
	return pthread_mutex_lock(&((ONLINE_USER_SHM *)m_pShm)->mutex) == 0;
}

void COnlineUserTable::Unlock()
{
	if(m_bInited == true)
		pthread_mutex_unlock(&((ONLINE_USER_SHM *)m_pShm)->mutex);
}

/******************************************************************************************
函数原型: 
功能描述: 登记上线用户(哈希: uid对表大小取余, 线性探测; 同一uid重复登录则覆盖原槽位)
参数说明:   uiUid       uint64_t   用户唯一标识
            uiConnSeq   uint64_t   登录时所在连接的序号
            iWorkerPid  pid_t      登录发生的worker进程pid
返 回 值: 成功true(含覆盖) 失败false(表满/未初始化)
******************************************************************************************/
bool COnlineUserTable::AddUser(uint64_t uiUid, uint64_t uiConnSeq, pid_t iWorkerPid, uint64_t uiToken)
{
	if(Lock() == false)
		return false;

	ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;
	int iEmpty = -1;   //探测路上找到的第一个空槽
	int iExist = -1;   //已存在的同uid槽位
	int iHash  = (int)(uiUid % _TABLE_SIZE_);

	for(int iProbe = 0; iProbe < _TABLE_SIZE_; iProbe++)
	{
		int idx = (iHash + iProbe) % _TABLE_SIZE_;
		if(pShm->items[idx].uiState == 1 && pShm->items[idx].uiUid == uiUid)
		{
			iExist = idx;
			break;
		}
		if(pShm->items[idx].uiState == 0)
		{
			if(iEmpty == -1) iEmpty = idx;  //探测链在此中断，本uid不在表中；该空槽可安全复用
			break;
		}
		//uiState==1但uid不同: 继续线性探测
	}

	bool bRet = true;
	if(iExist != -1)
	{
		//同uid重复登录: 覆盖原槽位(更新连接序号/pid/时间/令牌)，在线人数不变
		pShm->items[iExist].uiConnSeq  = uiConnSeq;
		pShm->items[iExist].iWorkerPid = iWorkerPid;
		pShm->items[iExist].timeLogin  = time(NULL);
		pShm->items[iExist].uiToken    = uiToken;
		pShm->items[iExist].uiLastSeq  = 0;   //重新登录后序号重新从0计
	}
	else if(iEmpty != -1)
	{
		pShm->items[iEmpty].uiUid      = uiUid;
		pShm->items[iEmpty].uiConnSeq  = uiConnSeq;
		pShm->items[iEmpty].iWorkerPid = iWorkerPid;
		pShm->items[iEmpty].timeLogin  = time(NULL);
		pShm->items[iEmpty].uiToken    = uiToken;
		pShm->items[iEmpty].uiLastSeq  = 0;
		pShm->items[iEmpty].uiState    = 1;
		pShm->iOnlineCount++;
	}
	else
	{
		bRet = false; //表满
	}

	Unlock();
	return bRet;
}

/******************************************************************************************
函数原型: 
功能描述: 注销下线用户(线性探测找到后移除，并对其后的探测聚簇做重哈希，保证后续查找链不断裂)
参数说明:   uiUid   uint64_t   用户唯一标识
******************************************************************************************/
void COnlineUserTable::RemoveUser(uint64_t uiUid)
{
	if(Lock() == false)
		return;

	ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;
	int iHash = (int)(uiUid % _TABLE_SIZE_);

	for(int iProbe = 0; iProbe < _TABLE_SIZE_; iProbe++)
	{
		int idx = (iHash + iProbe) % _TABLE_SIZE_;
		if(pShm->items[idx].uiState == 0)  //探测链中断: 该uid不在表中
			break;

		if(pShm->items[idx].uiUid == uiUid)
		{
			//找到: 移除并计数-1
			pShm->items[idx].uiState = 0;
			pShm->iOnlineCount--;
			if(pShm->iOnlineCount < 0) pShm->iOnlineCount = 0;

			//线性探测删除的标准做法: 把其后的连续占用聚簇逐个搬走重插，防止探测链断裂
			int j = (idx + 1) % _TABLE_SIZE_;
			while(pShm->items[j].uiState == 1)
			{
				ONLINE_USER_ITEM itemTmp = pShm->items[j];
				pShm->items[j].uiState = 0;

				int k = (int)(itemTmp.uiUid % _TABLE_SIZE_);
				while(pShm->items[k].uiState == 1)
					k = (k + 1) % _TABLE_SIZE_;
				pShm->items[k] = itemTmp;

				j = (j + 1) % _TABLE_SIZE_;
			}
			break;
		}
		//uid不同: 继续探测
	}

	Unlock();
	return;
}

/******************************************************************************************
函数原型: 
功能描述: 认证+防重放：校验令牌匹配且序号严格递增，通过则记录新序号
参数说明:   uiUid    用户唯一标识
            uiToken  客户端携带的令牌
            uiSeq    客户端携带的业务序号
返 回 值: true=通过(已更新lastseq) false=拒绝(未在线/令牌错/序号回放)
******************************************************************************************/
bool COnlineUserTable::CheckUserSeqToken(uint64_t uiUid, uint64_t uiToken, uint64_t uiSeq)
{
	bool bPass = false;
	if(Lock() == false)
		return false;

	ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;
	int iHash = (int)(uiUid % _TABLE_SIZE_);

	for(int iProbe = 0; iProbe < _TABLE_SIZE_; iProbe++)
	{
		int idx = (iHash + iProbe) % _TABLE_SIZE_;
		if(pShm->items[idx].uiState == 0)
			break;
		if(pShm->items[idx].uiUid == uiUid)
		{
			if(pShm->items[idx].uiToken == uiToken && uiSeq > pShm->items[idx].uiLastSeq)
			{
				pShm->items[idx].uiLastSeq = uiSeq;  //更新序号,该序号不可再用
				bPass = true;
			}
			break;
		}
	}

	Unlock();
	return bPass;
}

/******************************************************************************************
函数原型: 
功能描述: 查找用户是否在线
参数说明:   uiUid        uint64_t    用户唯一标识
            oUiConnSeq   uint64_t&   出参: 登录时的连接序号
            oIWorkerPid  pid_t&      出参: 所在worker进程pid
返 回 值: true=在线 false=不在线
******************************************************************************************/
bool COnlineUserTable::FindUser(uint64_t uiUid, uint64_t &oUiConnSeq, pid_t &oIWorkerPid)
{
	bool bFound = false;
	if(Lock() == false)
		return false;

	ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;
	int iHash = (int)(uiUid % _TABLE_SIZE_);

	for(int iProbe = 0; iProbe < _TABLE_SIZE_; iProbe++)
	{
		int idx = (iHash + iProbe) % _TABLE_SIZE_;
		if(pShm->items[idx].uiState == 0)
			break;
		if(pShm->items[idx].uiUid == uiUid)
		{
			oUiConnSeq  = pShm->items[idx].uiConnSeq;
			oIWorkerPid = pShm->items[idx].iWorkerPid;
			bFound = true;
			break;
		}
	}

	Unlock();
	return bFound;
}

/******************************************************************************************
函数原型: 
功能描述: 取当前全局在线总人数
返 回 值: 在线人数(未初始化返回-1)
******************************************************************************************/
int COnlineUserTable::GetOnlineCount()
{
	if(Lock() == false)
		return -1;
	int iCount = ((ONLINE_USER_SHM *)m_pShm)->iOnlineCount;
	Unlock();
	return iCount;
}
