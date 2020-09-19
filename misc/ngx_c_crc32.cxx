
//和 crc32校验算法 有关的代码

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_crc32.h"

//类静态变量初始化
CCRC32 *CCRC32::m_instance = NULL;

//构造函数
CCRC32::CCRC32()
{
	Init_CRC32_Table();
}
//释放函数
CCRC32::~CCRC32()
{
}
//初始化crc32表辅助函数
//unsigned long CCRC32::Reflect(unsigned long ref, char ch)
unsigned int CCRC32::Reflect(unsigned int ref, char ch)
{
	// Used only by Init_CRC32_Table()
	//unsigned long value(0);
    unsigned int value(0);
	// Swap bit 0 for bit 7 , bit 1 for bit 6, etc.
	for(int i = 1; i < (ch + 1); i++)
	{
		if(ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}

/******************************************************************************************
函数原型: 
功能描述: 初始化crc32表
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月16日 16时13分20秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CCRC32::Init_CRC32_Table()
{
	// This is the official polynomial used by CRC-32 in PKZip, WinZip and Ethernet. 
	//unsigned long ulPolynomial = 0x04c11db7;
    unsigned int ulPolynomial = 0x04c11db7;

	// 256 values representing ASCII character codes.
	for(int i = 0; i <= 0xFF; i++)
	{
		crc32_table[i]=Reflect(i, 8) << 24;
        //if (i == 1)printf("old1--i=%d,crc32_table[%d] = %lu\r\n",i,i,crc32_table[i]);
        
		for (int j = 0; j < 8; j++)
        {
            //if(i == 1)
            //{
            //    unsigned int tmp1 = (crc32_table[i] << 1);
            //    unsigned int tmp2 = (crc32_table[i] & (1 << 31) ? ulPolynomial : 0);
            //    unsigned int tmp3 = tmp1 ^ tmp2;
            //    tmp3 += 1;
            //    tmp3 -= 1;
            //
            //}
            
			crc32_table[i] = (crc32_table[i] << 1) ^ (crc32_table[i] & (1 << 31) ? ulPolynomial : 0);
            //if (i == 1)printf("old3--i=%d,crc32_table[%d] = %lu\r\n",i,i,crc32_table[i]);
        }
        //if (i == 1)printf("old2--i=%d,crc32_table[%d] = %lu\r\n",i,i,crc32_table[i]);
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}

}

/******************************************************************************************
函数原型: 
功能描述: 用crc32_table寻找表来产生数据的CRC值
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月16日 16时13分38秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
int CCRC32::Get_CRC(unsigned char* buffer, unsigned int dwSize)
{
	// Be sure to use unsigned variables,
	// because negative values introduce high bits
	// where zero bits are required.
	//unsigned long  crc(0xffffffff);
    unsigned int  crc(0xffffffff);
	int len;
	
	len = dwSize;	
	// Perform the algorithm on each character
	// in the string, using the lookup table values.
	while(len--)
		crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ *buffer++];
	// Exclusive OR the result with the beginning value.
	return crc^0xffffffff;
}

