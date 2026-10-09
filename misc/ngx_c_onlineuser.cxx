#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "ngx_c_onlineuser.h"
#include "ngx_c_shmutil.h"     //nginx风格共享内存封装(MAP_ANON|MAP_SHARED)
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

COnlineUserTable::COnlineUserTable()
{
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

	if(bIsMaster == true)
	{
		//(1)master: 创建匿名共享映射(nginx MAP_ANON方案,参考ngx_shmem.c)并初始化【必须在fork之前】
		m_pShm = NgxShmCreateAnon(sizeof(ONLINE_USER_SHM));
		if(m_pShm == NULL)
		{
			LOG_STDERR1(errno, "COnlineUserTable::Init()创建匿名共享内存失败!");
			return false;
		}

		ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;  //NgxShmCreateAnon已清零
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);  //关键：跨进程共享该互斥量
		pthread_mutex_init(&pShm->mutex, &attr);
		pthread_mutexattr_destroy(&attr);
	}
	else
	{
		//(2)worker: fork继承了master的映射, 只需确认有效
		if(NgxShmInherited(m_pShm) == false)
		{
			LOG_STDERR("COnlineUserTable::Init()worker继承的共享内存无效[master是否先Init(true)?]!");
			return false;
		}
	}

	m_bInited = true;
	LOG_INFO("在线用户表初始化成功[%s进程], 表大小=%d。", bIsMaster == true ? "master" : "worker", _TABLE_SIZE_);
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
void COnlineUserTable::RemoveUser(uint64_t uiUid, uint64_t uiConnSeq)
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
			if(pShm->items[idx].uiConnSeq != uiConnSeq)
			{
				LOG_INFO("[DEBUG]注销序号不符: 槽内seq=%ud 传入seq=%ud",pShm->items[idx].uiConnSeq,uiConnSeq);
				//槽位里的连接序号与注销请求不符: 说明该uid已在新连接上重新登录,
				//这条是"旧连接延迟回收"的注销请求, 绝不能误删新登录条目
				Unlock();
				return;
			}
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
功能描述: 枚举全部在线uid【广播用】, 加锁遍历槽位收集
参数说明:   pUidArr    出参: uid数组(调用方分配)
            iArrSize   数组容量
            oUiCount   出参: 实际收集到的uid个数
******************************************************************************************/
void COnlineUserTable::GetAllOnlineUids(uint64_t *pUidArr, int iArrSize, int &oUiCount)
{
	oUiCount = 0;
	if(Lock() == false || pUidArr == NULL || iArrSize <= 0)
		return;

	ONLINE_USER_SHM *pShm = (ONLINE_USER_SHM *)m_pShm;
	for(int i = 0; i < _TABLE_SIZE_ && oUiCount < iArrSize; i++)
	{
		if(pShm->items[i].uiState == 1)
		{
			pUidArr[oUiCount] = pShm->items[i].uiUid;
			oUiCount++;
		}
	}

	Unlock();
	return;
}

/******************************************************************************************
函数原型: 
功能描述: 掉线重连: 校验令牌并把该uid的会话条目绑定到新连接(连接ID/所在worker更新,
          token与lastSeq保留——重连后业务序号继续递增, 不给重放窗口)
参数说明:   uiUid          用户唯一标识
            uiToken        客户端携带的会话令牌
            uiNewConnId    新连接的分配ID
            iNewWorkerPid  新连接所在worker进程pid
返 回 值: true=会话已恢复(绑定新连接) false=失败(不在线已超窗口/令牌错误)
被引用于: CLogicSocket::_HandleReconnect()
创建日期: 2026年10月09日
修改记录: 
******************************************************************************************/
bool COnlineUserTable::TryRebind(uint64_t uiUid, uint64_t uiToken, uint64_t uiNewConnId, pid_t iNewWorkerPid)
{
	bool bOK = false;
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
			if(pShm->items[idx].uiToken == uiToken)
			{
				//令牌匹配: 会话绑定到新连接(旧连接回收时按其旧ID注销, 不会误删本条目)
				pShm->items[idx].uiConnSeq  = uiNewConnId;
				pShm->items[idx].iWorkerPid = iNewWorkerPid;
				pShm->items[idx].timeLogin  = time(NULL);
				bOK = true;
			}
			break;
		}
	}

	Unlock();
	return bOK;
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
