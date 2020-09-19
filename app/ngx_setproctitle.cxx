#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //env
#include <string.h>

#include "ngx_global.h"

/******************************************************************************************
函数原型: 
功能描述: 备份环境变量（到新内存gp_envmem，以备后用）
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 09时44分58秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void BackupEnv()
{   
    //这里无需判断penvmen == NULL,有些编译器new会返回NULL，有些会报异常，但不管怎样，如果在重要的地方new失败了，你无法收场，让程序失控崩溃，助你发现问题为好； 
    g_pcEnvBuff = new char[g_ulEnvBuffLen]; 
    memset(g_pcEnvBuff,0,g_ulEnvBuffLen);  //内存要清空防止出现问题

    char *ptmp = g_pcEnvBuff;
    //把原来的内存内容搬到新地方来
    for (int i = 0; environ[i]; i++) 
    {
        size_t size = strlen(environ[i])+1 ; //记得+1! 否则内存全乱套了，因为strlen是不包括字符串末尾的\0的
        strcpy(ptmp,environ[i]);      //把原环境变量内容拷贝到新地方【新内存】
        environ[i] = ptmp;            //然后还要让新环境变量指向这段新内存
        ptmp += size;
    }
    return;
}

/******************************************************************************************
函数原型: 
功能描述: 设置可执行程序标题
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 09时45分05秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void SetProcTitle(const char *szTitle)
{
    //我们假设，所有的命令 行参数我们都不需要用到了，可以被随意覆盖了；
    //注意：我们的标题长度，不会长到原始标题和原始环境变量都装不下，否则怕出问题，不处理
    
    //(1)计算新标题长度
    size_t iTitleLen = strlen(szTitle); 

    //(2)计算总的原始的argv那块内存的总长度【包括各种参数】    
    size_t esy = g_ulArgvNeedMemLen + g_ulEnvBuffLen; //argv和environ内存总和
    if( esy <= iTitleLen)
    {
        //标题太长，不玩了!
        return;
    }

    //空间够保存标题的，够长，存得下，继续走下来    

    //(3)设置后续的命令行参数为空，表示只有argv[]中只有一个元素了，这是好习惯；防止后续argv被滥用，因为很多判断是用argv[] == NULL来做结束标记判断的;
    g_ppcOSArgv[1] = NULL;  

    //(4)把标题弄进来，注意原来的命令行参数都会被覆盖掉，不要再使用这些命令行参数,而且g_os_argv[1]已经被设置为NULL了
    char *pcTmp = g_ppcOSArgv[0]; //让ptmp指向g_os_argv所指向的内存
    strcpy(pcTmp,szTitle);
    pcTmp += iTitleLen; //跳过标题

    //(5)把剩余的原argv以及environ所占的内存全部清0，否则会出现在ps的cmd列可能还会残余一些没有被覆盖的内容；
    size_t cha = esy - iTitleLen;  //内存总和减去标题字符串长度(不含字符串末尾的\0)，剩余的大小，就是要memset的；
    memset(pcTmp,0,cha);
    return;
}