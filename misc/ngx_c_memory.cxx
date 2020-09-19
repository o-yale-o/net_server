
//和 内存分配 有关的函数放这里

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"

//类静态成员赋值
CMemory *CMemory::m_instance = NULL;

/******************************************************************************************
函数原型: 
功能描述: 分配内存
参数说明:   名称            类型                说明
            memCount                            分配的字节大小
            ifmemset                            是否要把分配的内存初始化为0
返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 16时32分51秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void *CMemory::AllocMemory(int memCount,bool ifmemset)
{	    
	void *tmpData = (void *)new char[memCount]; //我并不会判断new是否成功，如果new失败，程序根本不应该继续运行，就让它崩溃以方便我们排错吧
    if(ifmemset) //要求内存清0
    {
	    memset(tmpData,0,memCount);
    }
	return tmpData;
}

/******************************************************************************************
函数原型: 
功能描述: 内存释放函数
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月11日 16时33分37秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CMemory::FreeMemory(void *point)
{		
	//delete [] point;  //这么删除编译会出现警告：warning: deleting ‘void*’ is undefined [-Wdelete-incomplete]
    delete [] ((char *)point); //new的时候是char *，这里弄回char *，以免出警告
}

