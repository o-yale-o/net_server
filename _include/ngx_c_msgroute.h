#pragma once

#include <stdint.h>   //uint64_t

//跨worker消息路由信箱：基于POSIX共享内存的槽位环。
//任意worker可通过PutMsg把"发给某uid"的消息投入信箱；
//各worker在定时器线程中周期性调用ProcessAll(回调)：uid归属本worker连接的则投递到连接发送队列并消费，
//无人认领且超时的消息由任意轮询者回收，防止槽位泄漏。
//回调以函数指针+上下文实现，避免本模块依赖网络层。

static const int _ROUTE_BODY_SIZE_ = 256;   //单条路由消息体上限(须大于STRUCT_RECVMSG的208字节)
static const int _ROUTE_SLOTS_     = 1024;  //信箱槽位总数

struct ROUTE_MSG_ITEM
{
	uint64_t uiState;                       //0=空闲 1=待投递
	uint64_t uiToUid;                       //目标用户uid
	int      iCmd;                          //投递到目标连接时的消息命令字(主机序)
	int      iBodyLen;                      //消息体长度
	uint64_t uiTimePut;                     //投入时间(毫秒, 用于过期回收)
	char     acBody[_ROUTE_BODY_SIZE_];     //消息体
};

class CMsgRoute
{
private:
	CMsgRoute();

public:
	static CMsgRoute* GetInstance();        //C++11 Meyers单例

	bool Init(bool bIsMaster);              //master创建共享内存并初始化[fork之前]；worker挂接
	bool PutMsg(uint64_t uiToUid, int iCmd, const char *pcBody, int iBodyLen);  //投入一条消息

	//遍历所有待投递消息：
	//fn(pCtx,uiToUid,iCmd,pcBody,iBodyLen,uiTimePut) 返回true=已投递(槽位释放)；false=暂不投递(保留,超时由本函数回收)
	typedef bool (*FnOnMsg)(void *pCtx, uint64_t uiToUid, int iCmd, const char *pcBody, int iBodyLen, uint64_t uiTimePut);
	void ProcessAll(void *pCtx, FnOnMsg fn, uint64_t uiExpireMs);

private:
	bool Lock();
	void Unlock();

private:
	int              m_iShmFd;
	void            *m_pShm;
	bool             m_bInited;
};
