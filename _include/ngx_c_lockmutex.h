/******************************************************************************************
类    名: CLock
头 文 件: 
实现文件: 
功能描述: 
依 赖 于: 
被引用于: 
创建日期: 2020年09月10日 13时20分48秒
修改记录: 
		修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/

#pragma once 

#include <pthread.h> 

//本类用于自动释放互斥量，防止忘记调用pthread_mutex_unlock的情况发生
//本类其实在《c++从入门到精通c++98/11/14/17中也详细讲过》
class CLock
{
public:
	CLock(pthread_mutex_t *pMutex)
	{
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex); //加锁互斥量
	}
	~CLock()
	{
		pthread_mutex_unlock(m_pMutex); //解锁互斥量
	}
private:
	pthread_mutex_t *m_pMutex;
    
};

