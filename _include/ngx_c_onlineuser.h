#pragma once

#include <stdint.h>   //uint64_t
#include <time.h>     //time_t

//说明：本类实现跨worker进程的全局在线用户表，基于POSIX共享内存(shm_open+mmap)。
//master进程在fork之前Init(true)创建共享内存并初始化，worker进程Init(false)挂接同一块映射，
//从而任意worker登录/断开的用户，其余worker立即可见【为跨进程推送/踢人/全局统计打基础】。

//在线用户表中单个槽位
struct ONLINE_USER_ITEM
{
	uint64_t uiUid;           //用户唯一标识(登录名CRC32)
	uint64_t uiConnSeq;       //登录时所在连接的iCurrSequence(用于将来定位/校验连接)
	pid_t    iWorkerPid;      //登录发生在哪个worker进程
	time_t   timeLogin;       //登录时间
	uint64_t uiState;         //槽位状态: 0=空闲 1=占用
};

class COnlineUserTable
{
private:
	COnlineUserTable();

public:
	static COnlineUserTable* GetInstance(); //单例：C++11 Meyers单例

	//bIsMaster=true: 创建共享内存并初始化【必须在fork之前由master调用】
	//bIsMaster=false: 挂接已存在的共享内存【worker进程调用】
	bool Init(bool bIsMaster);

	bool AddUser(uint64_t uiUid, uint64_t uiConnSeq, pid_t iWorkerPid);    //登记上线用户(重复登录同一uid则覆盖)
	void RemoveUser(uint64_t uiUid);                                       //注销下线用户
	bool FindUser(uint64_t uiUid, uint64_t &oUiConnSeq, pid_t &oIWorkerPid); //查找用户是否在线
	int  GetOnlineCount();                                                 //当前在线总人数(全局)

private:
	bool Lock();
	void Unlock();

private:
	int              m_iShmFd;              //共享内存描述符
	void            *m_pShm;                //共享内存映射首地址
	bool             m_bInited;             //是否已成功初始化
};
