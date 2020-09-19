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
	static CMemory *m_instance;

public:	
	static CMemory* GetInstance() //单例
	{			
		if(m_instance == NULL)
		{
			//锁
			if(m_instance == NULL)
			{				
				m_instance = new CMemory(); //第一次调用不应该放在线程中，应该放在主进程中，以免和其他线程调用冲突从而导致同时执行两次new CMemory()
				static CGarhuishou cl; 
			}
			//放锁
		}
		return m_instance;
	}	
	class CGarhuishou 
	{
	public:				
		~CGarhuishou()
		{
			if (CMemory::m_instance)
			{						
				delete CMemory::m_instance; //这个释放是整个系统退出的时候，系统来调用释放内存的哦
				CMemory::m_instance = NULL;				
			}
		}
	};
	//-------

public:
	void *AllocMemory(int memCount,bool ifmemset);
	void FreeMemory(void *point);
	
};

