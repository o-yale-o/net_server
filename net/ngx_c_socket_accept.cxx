﻿//和网络 中 接受连接【accept】 有关的函数放这里
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

/******************************************************************************************
函数原型: 
功能描述: 建立新连接专用函数，当新连接进入时
参数说明:   名称            类型                说明

返 回 值: 
依 赖 于: 
被引用于: ProcessEpollEvents()
创建日期: 2020年09月11日 09时46分04秒
修改记录: 
        修改日期    修改人          修改标记        新版本号    修改原因
******************************************************************************************/
void CSocekt::OnAccept(lpngx_connection_t pConnOld)
{
    //因为listen套接字上用的不是ET【边缘触发】，而是LT【水平触发】，意味着客户端连入如果我要不处理，这个函数会被多次调用，所以，我这里这里可以不必多次accept()，可以只执行一次accept()
    //这也可以避免本函数被卡太久，注意，本函数应该尽快返回，以免阻塞程序运行；
    struct sockaddr    sockaddrRemote;        //远端服务器的socket地址
    socklen_t          iSockaddrRemoteLen = 0;
    int                iErr = 0;
    int                iLogLevel = 0; 
    int                iSockNew = 0;
    static bool        bIsCanUseAccept4 = true; //我们先认为能够使用accept4()函数
    lpngx_connection_t pConnNew = NULL; //代表连接池中的一个连接【注意这是指针】
    
    //LogStdErr(0,"这是几个\n"); 这里会惊群，也就是说，epoll技术本身有惊群的问题

    iSockaddrRemoteLen = sizeof(sockaddrRemote);
    do   //用do，跳到while后边去方便
    {
        if(bIsCanUseAccept4)
        {
            //以为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept4()也不会卡在这里；
            iSockNew = accept4(pConnOld->fd, &sockaddrRemote, &iSockaddrRemoteLen, SOCK_NONBLOCK); //从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，节省一次ioctl【设置为非阻塞】调用
        }
        else
        {
            //以为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept()也不会卡在这里；
            iSockNew = accept(pConnOld->fd, &sockaddrRemote, &iSockaddrRemoteLen);
        }

        //惊群，有时候不一定完全惊动所有4个worker进程，可能只惊动其中2个等等，其中一个成功其余的accept4()都会返回-1；错误 (11: Resource temporarily unavailable【资源暂时不可用】) 
        //所以参考资料：https://blog.csdn.net/russell_tao/article/details/7204260
        //其实，在linux2.6内核上，accept系统调用已经不存在惊群了（至少我在2.6.18内核版本上已经不存在）。大家可以写个简单的程序试下，在父进程中bind,listen，然后fork出子进程，
               //所有的子进程都accept这个监听句柄。这样，当新连接过来时，大家会发现，仅有一个子进程返回新建的连接，其他子进程继续休眠在accept调用上，没有被唤醒。
        //LogStdErr(0,"测试惊群问题，看惊动几个worker进程%d\n",iSockNew); 【我的结论是：accept4可以认为基本解决惊群问题，但似乎并没有完全解决，有时候还会惊动其他的worker进程】

        /*
        if(iSockNew == -1)
        {
           LOG_STDERR("惊群测试:OnAccept()中accept失败,进程id=%d",g_CurrPID); 
        }
        else
        {
           LOG_STDERR("惊群测试:OnAccept()中accept成功,进程id=%d",g_CurrPID); 
        } */       

        if(iSockNew == -1)
        {
            iErr = errno;

            //对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
            if(iErr == EAGAIN) //accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
            {
                //除非你用一个循环不断的accept()取走所有的连接，不然一般不会有这个错误【我们这里只取一个连接，也就是accept()一次】
                return ;
            } 
            iLogLevel = NGX_LOG_ALERT;
            if (iErr == ECONNABORTED)  //ECONNRESET错误则发生在对方意外关闭套接字后【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            {
                //该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
                    //客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
                    //POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
                        //服务器进程一般可以忽略该错误，直接再次调用accept。
                iLogLevel = NGX_LOG_ERR;
            } 
            else if (iErr == EMFILE || iErr == ENFILE) //EMFILE:进程的fd已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】。可参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
                                                        //ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
                                                    //ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定受限于system-wide的resource limits。
            {
                iLogLevel = NGX_LOG_CRIT;
            }
            LOG_INFO( "accept4()失败!" );

            if(bIsCanUseAccept4 && iErr == ENOSYS) //accept4()函数没实现，坑爹？
            {
                bIsCanUseAccept4 = false;  //标记不使用accept4()函数，改用accept()函数
                continue;         //回去重新用accept()函数搞
            }

            if (iErr == ECONNABORTED)  //对方关闭套接字
            {
                //这个错误因为可以忽略，所以不用干啥
                //do nothing
            }
            
            if ( iErr == EMFILE || iErr == ENFILE ) 
            {
                //do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                //我这里目前先不处理吧【因为上边已经写这个日志了】；
            }            
            return;
        }  //end if(iSockNew == -1)

        //走到这里的，表示accept4()/accept()成功了        
        if(m_iOnlineUserCount >= m_iWorkerMaxConnection)  //用户连接数过多，要关闭该用户socket，因为现在也没分配连接，所以直接关闭即可
        {
            //LogStdErr(0,"超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)。",m_iWorkerMaxConnection,iSockNew);  
            close(iSockNew);
            return ;
        }
        //如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用ngx_get_connection()使用我们短时间内产生大量连接，危及本服务器安全
        if(m_listConnection.size() > (m_iWorkerMaxConnection * 5))
        {
            //比如你允许同时最大2048个连接，但连接池却有了 2048*5这么大的容量，这肯定是表示短时间内 产生大量连接/断开，因为我们的延迟回收机制，这里连接还在垃圾池里没有被回收
            if(m_listFreeConnection.size() < m_iWorkerMaxConnection)
            {
                //整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                //一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】
                close( iSockNew );
                return ;   
            }
        }

        LOG_INFO( "accept4成功s=%d", iSockNew );
        pConnNew = GetConnectionFromFreeList( iSockNew ); //这是针对新连入用户的连接，和监听套接字 所对应的连接是两个不同的东西，不要搞混
        if( !pConnNew )
        {
            //连接池中连接不够用，那么就得把这个socekt直接关闭并返回了，因为在ngx_get_connection()中已经写日志了，所以这里不需要写日志了
            if(close(iSockNew) == -1)
            {
                LOG_ALERT("close(%d)失败!", iSockNew );
            }
            return;
        }
        //...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

        //成功的拿到了连接池中的一个连接
        memcpy(&pConnNew->s_sockaddr,&sockaddrRemote,iSockaddrRemoteLen);  //拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】
        //{
        //    //测试将收到的地址弄成字符串，格式形如"192.168.1.126:40904"或者"192.168.1.126"
        //    u_char ipaddr[100]; memset(ipaddr,0,sizeof(ipaddr));
        //    ngx_sock_ntop(&pConnNew->s_sockaddr,1,ipaddr,sizeof(ipaddr)-10); //宽度给小点
        //   LOG_STDERR("ip信息为%iSockNew\n",ipaddr);
        //}

        if(!bIsCanUseAccept4)
        {
            //如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
            if(SetNonBlocking(iSockNew) == false)
            {
                //设置非阻塞居然失败
                CloseConnection(pConnNew); //关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
                return; //直接返回
            }
        }

        pConnNew->pListening = pConnOld->pListening;    //连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】
        //pConnNew->iWriteReady = 1;                    //标记可以写，新连接写事件肯定是ready的；【从连接池拿出一个连接时这个连接的所有成员都是0】            
        
        pConnNew->pmfRead  = &CSocekt::OnRead;  //设置数据来时的读处理函数，其实官方 nginx 中是 ngx_http_wait_request_handler()
        pConnNew->pmfWrite = &CSocekt::OnWrite; //设置数据发送时的写处理函数。

        //客户端应该主动发送第一次的数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发ngx_wait_request_handler()被ngx_epoll_process_events()调用        
        if(OperateEpollEvent(
                                iSockNew,                  //socekt句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
                                0,                  //对于事件类型为增加的，不需要这个参数
                                pConnNew                //连接池中的连接
                                ) == -1)         
        {
            //增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
            CloseConnection(pConnNew);//关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
            return; //直接返回
        }
        /*
        else
        {
            //打印下发送缓冲区大小
            int           n;
            socklen_t     len;
            len = sizeof(int);
            getsockopt(iSockNew,SOL_SOCKET,SO_SNDBUF, &n, &len);
           LOG_STDERR("发送缓冲区的大小为%d!",n); //87040

            n = 0;
            getsockopt(iSockNew,SOL_SOCKET,SO_RCVBUF, &n, &len);
           LOG_STDERR("接收缓冲区的大小为%d!",n); //374400

            int sendbuf = 2048;
            if (setsockopt(iSockNew, SOL_SOCKET, SO_SNDBUF,(const void *) &sendbuf,n) == 0)
            {
               LOG_STDERR("发送缓冲区大小成功设置为%d!",sendbuf); 
            }

             getsockopt(iSockNew,SOL_SOCKET,SO_SNDBUF, &n, &len);
           LOG_STDERR("发送缓冲区的大小为%d!",n); //87040
        }
        */

        if(m_iIsOpenKickTimer == 1)
        {
            AddToTimerQueue(pConnNew);
        }
        ++m_iOnlineUserCount;  //连入用户数量+1        
        break;  //一般就是循环一次就跳出去
    } while (1);   

    return;
}

