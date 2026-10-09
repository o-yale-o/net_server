#pragma once

//收发命令宏定义
#define _CMD_START      0  
#define _CMD_PING       _CMD_START + 0   //ping命令【心跳包】
#define _CMD_REGISTER   _CMD_START + 5   //注册
#define _CMD_LOGIN      _CMD_START + 6   //登录
#define _CMD_WHOONLINE  _CMD_START + 7   //查询全局在线人数【应答包体: int 在线人数】
#define _CMD_SENDMSG    _CMD_START + 8   //客户端发消息给指定uid【需认证: 包体前16字节为token+seq】
#define _CMD_RECVMSG    _CMD_START + 9   //服务器投递给目标用户的消息【服务器主动下发,包体=STRUCT_RECVMSG】

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


//点对点消息请求结构【命令8, 包体=token(8)+seq(8)+本结构: 前16字节为认证与防重放字段】
typedef struct _STRUCT_SENDMSG
{
	uint64_t uiToUid;       //目标用户uid
	char     acText[200];   //消息文本

}STRUCT_SENDMSG, *LPSTRUCT_SENDMSG;

//点对点消息投递结构【命令9, 服务器→目标客户端】
typedef struct _STRUCT_RECVMSG
{
	uint64_t uiFromUid;     //发送方uid
	char     acText[200];   //消息文本

}STRUCT_RECVMSG, *LPSTRUCT_RECVMSG;

//登录应答结构【下发会话令牌】
typedef struct _STRUCT_LOGIN_REPLY
{
	uint64_t uiToken;       //会话令牌: 后续业务命令(8+)包体前8字节须携带

}STRUCT_LOGIN_REPLY, *LPSTRUCT_LOGIN_REPLY;

//在线人数查询应答结构
typedef struct _STRUCT_WHOONLINE_REPLY
{
	int     iOnlineCount;   //当前全局在线人数【传输时用htonl转网络序】

}STRUCT_WHOONLINE_REPLY, *LPSTRUCT_WHOONLINE_REPLY;

#pragma pack() //取消指定对齐，恢复缺省对齐


