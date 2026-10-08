/******************************************************************************************
类    名: CMemory
头 文 件: _include/ngx_c_memory.h
实现文件: misc/ngx_c_memory.cxx
功能描述: 内存相关
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 13时52分08秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/

#pragma once

#include <stddef.h>  //NULL

class CMemory 
{
private:
	CMemory() {}  //构造函数，因为要做成单例类，所以是私有的构造函数

public:
	~CMemory(){};

private:

public:	
	static CMemory* GetInstance() //单例：C++11 Meyers单例，函数内static的初始化由编译器保证线程安全
	{			
		static CMemory instance;
		return &instance;
	}	
	//-------

public:
	void *AllocMemory(int memCount,bool ifmemset);
	void FreeMemory(void *point);
	
};

