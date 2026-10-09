#pragma once

//收发命令宏定义
#define _CMD_START      0  
#define _CMD_PING       _CMD_START + 0   //ping命令【心跳包】
#define _CMD_REGISTER   _CMD_START + 5   //注册
#define _CMD_LOGIN      _CMD_START + 6   //登录
#define _CMD_WHOONLINE  _CMD_START + 7   //查询全局在线人数【应答包体: int 在线人数】

//结构定义------------------------------------
#pragma pack (1) //对齐方式,1字节对齐【结构之间成员不做任何字节对齐：紧密的排列在一起】

typedef struct _STRUCT_REGISTER
{
	int     iType;          //类型
	char    username[56];   //用户名 
	char    password[40];   //密码

}STRUCT_REGISTER, *LPSTRUCT_REGISTER;

typedef struct _STRUCT_LOGIN
{
	char    username[56];   //用户名 
	char    password[40];   //密码

}STRUCT_LOGIN, *LPSTRUCT_LOGIN;


//在线人数查询应答结构
typedef struct _STRUCT_WHOONLINE_REPLY
{
	int     iOnlineCount;   //当前全局在线人数【传输时用htonl转网络序】

}STRUCT_WHOONLINE_REPLY, *LPSTRUCT_WHOONLINE_REPLY;

#pragma pack() //取消指定对齐，恢复缺省对齐


