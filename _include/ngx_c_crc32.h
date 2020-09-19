/******************************************************************************************
类    名: CCRC32
头 文 件: _include/ngx_c_crc32.h
实现文件: misc/ngx_c_crc32.cxx
功能描述: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 13时19分33秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/

#pragma once

#include <stddef.h>  //NULL

class CCRC32
{
private:
	CCRC32();
public:
	~CCRC32();
private:
	static CCRC32 *m_instance;

public:	
	static CCRC32* GetInstance() 
	{
		if(m_instance == NULL)
		{
			//锁
			if(m_instance == NULL)
			{				
				m_instance = new CCRC32();
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
			if (CCRC32::m_instance)
			{						
				delete CCRC32::m_instance;
				CCRC32::m_instance = NULL;				
			}
		}
	};
	//-------
public:

	void  Init_CRC32_Table();
	//unsigned long Reflect(unsigned long ref, char ch); // Reflects CRC bits in the lookup table
    unsigned int Reflect(unsigned int ref, char ch); // Reflects CRC bits in the lookup table
    
	//int   Get_CRC(unsigned char* buffer, unsigned long dwSize);
    int   Get_CRC(unsigned char* buffer, unsigned int dwSize);
    
public:
	//unsigned long crc32_table[256]; // Lookup table arrays
    unsigned int crc32_table[256]; // Lookup table arrays
};



