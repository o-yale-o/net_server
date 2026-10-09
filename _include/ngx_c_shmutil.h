#pragma once

#include <stddef.h>   //size_t
#include <string.h>   //memset
#include <sys/mman.h> //mmap

//共享内存工具【nginx风格封装】
//参考: nginx src/os/unix/ngx_shmem.c 的 MAP_ANON|MAP_SHARED 方案。
//
//用法(与本项目的进程模型严格对应):
//  master进程在fork之前调用 NgxShmCreateAnon() 创建匿名共享映射并完成初始化;
//  fork()后各worker进程自动继承同一块MAP_SHARED映射, 直接使用该地址即可(无须任何attach)。
//相比 shm_open 方案的优势: 无/dev/shm残留文件、无命名冲突、无须-lrt。

//分配一块已清零的匿名共享内存(仅master在fork之前调用)
inline void *NgxShmCreateAnon(size_t ulSize)
{
    void *pAddr = mmap(NULL, ulSize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if(pAddr == MAP_FAILED)
        return NULL;
    memset(pAddr, 0, ulSize);
    return pAddr;
}

//worker侧确认继承的共享映射有效【fork继承, 地址与master一致, 无须额外操作】
inline bool NgxShmInherited(void *pAddr)
{
    return pAddr != NULL;
}
