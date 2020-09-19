/******************************************************************************************
类    名: CLogicSocket
头 文 件: _include/ngx_c_slogic.h
实现文件: logic/ngx_c_slogic.cxx
功能描述: 处理逻辑和通讯的子类
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 13时52分08秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/

#pragma once

#include <sys/socket.h>
#include "ngx_c_socket.h"

class CLogicSocket : public CSocekt   //继承自父类CScoekt
{
public:
	CLogicSocket();                                                         //构造函数
	virtual ~CLogicSocket();                                                //释放函数
	virtual bool Initialize();                                              //初始化函数

public:

	//通用收发数据相关函数
	void  SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader,unsigned short iMsgCode);

	//各种业务逻辑相关函数都在之类
	bool _HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	bool _HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);
	bool _HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength);

	virtual void ProcessIdleTimeOut(LPSTRUC_MSG_HEADER tmpmsg,time_t cur_time);      //心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作

public:
	virtual void ProcessClientRequest(char *pcMsgBuf);
};

