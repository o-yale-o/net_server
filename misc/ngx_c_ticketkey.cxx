#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <openssl/rand.h>

#include "ngx_c_ticketkey.h"
#include "ngx_c_shmutil.h"     //nginx风格共享内存封装(MAP_ANON|MAP_SHARED)
#include "ngx_macro.h"
#include "ngx_func.h"

//共享内存布局: [进程共享互斥量][当前密钥][备用密钥]
struct TICKET_SHM
{
    pthread_mutex_t   mutex;
    TICKET_KEY_ITEM   itemCurr;   //当前密钥: 签发新票据用 + 解密当前票据用
    TICKET_KEY_ITEM   itemPrev;   //上一把密钥: 仅解密用(轮换过渡期, 已发出的旧票据仍可恢复)
};

//OpenSSL票据回调(框架调用, 转发到单例逻辑)
static int NgxTicketKeyCb(SSL *pSSL, unsigned char acKeyName[16], unsigned char acIV[EVP_MAX_IV_LENGTH],
                          EVP_CIPHER_CTX *pEvpCtx, HMAC_CTX *pHmacCtx, int iEnc)
{
    return CTicketKey::GetInstance()->OnTicketKey(acKeyName, acIV, pEvpCtx, pHmacCtx, iEnc);
}

//生成一把新密钥(随机name/aes/hmac)
static void GenOneKey(TICKET_KEY_ITEM *pItem, uint64_t uiVer)
{
    memset(pItem, 0, sizeof(TICKET_KEY_ITEM));
    RAND_bytes(pItem->acName, sizeof(pItem->acName));
    RAND_bytes(pItem->acAesKey, sizeof(pItem->acAesKey));
    RAND_bytes(pItem->acHmacKey, sizeof(pItem->acHmacKey));
    pItem->uiVer = uiVer;
    pItem->uiCreateTime = time(NULL);
}

CTicketKey::CTicketKey()
{
    m_pShm    = NULL;
    m_bInited = false;
}

CTicketKey* CTicketKey::GetInstance() //单例：C++11 Meyers单例，函数内static的初始化由编译器保证线程安全
{
    static CTicketKey instance;
    return &instance;
}

/******************************************************************************************
函数原型:
功能描述: 初始化: master创建共享内存并生成首把密钥[必须在fork之前]; worker校验继承的映射
返 回 值: 成功true 失败false
被引用于: CSocekt::NgxSSLInit()(UseTLS=1时)
创建日期: 2026年10月09日
修改记录:
******************************************************************************************/
bool CTicketKey::Init(bool bIsMaster)
{
    if(m_bInited == true)  //幂等
        return true;

    if(bIsMaster == true)
    {
        m_pShm = NgxShmCreateAnon(sizeof(TICKET_SHM));
        if(m_pShm == NULL)
        {
            LOG_STDERR1(errno, "CTicketKey::Init()创建匿名共享内存失败!");
            return false;
        }

        TICKET_SHM *pShm = (TICKET_SHM *)m_pShm;  //NgxShmCreateAnon已清零
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&pShm->mutex, &attr);
        pthread_mutexattr_destroy(&attr);

        GenOneKey(&pShm->itemCurr, 1);  //首把密钥: 版本1
    }
    else
    {
        //worker: fork继承了master的映射, 只需确认有效
        if(NgxShmInherited(m_pShm) == false)
        {
            LOG_STDERR("CTicketKey::Init()worker继承的共享内存无效[master是否先Init(true)?]!");
            return false;
        }
    }

    m_bInited = true;
    LOG_INFO("TLS会话票据密钥初始化成功[%s进程]。", bIsMaster == true ? "master" : "worker");
    return true;
}

bool CTicketKey::Lock()
{
    if(m_bInited == false)
        return false;
    return pthread_mutex_lock(&((TICKET_SHM *)m_pShm)->mutex) == 0;
}

void CTicketKey::Unlock()
{
    if(m_bInited == true)
        pthread_mutex_unlock(&((TICKET_SHM *)m_pShm)->mutex);
}

/******************************************************************************************
函数原型:
功能描述: 轮换密钥: 当前→备用, 生成新的当前密钥[SIGHUP重载时由master调用]
          已发出的旧票据仍可用备用密钥恢复, 平滑过渡
返 回 值: true=已轮换 false=未初始化/失败
被引用于: NgxReloadWorkers()
创建日期: 2026年10月09日
修改记录:
******************************************************************************************/
bool CTicketKey::Rotate()
{
    if(Lock() == false)
        return false;

    TICKET_SHM *pShm = (TICKET_SHM *)m_pShm;
    uint64_t uiNewVer = pShm->itemCurr.uiVer + 1;
    pShm->itemPrev = pShm->itemCurr;      //当前→备用(旧票据仍可解密, 平滑过渡)
    GenOneKey(&pShm->itemCurr, uiNewVer); //生成新当前密钥

    Unlock();

    LOG_INFO("TLS会话票据密钥已轮换, 旧票据在过渡期仍可恢复。");
    return true;
}

/******************************************************************************************
函数原型:
功能描述: OpenSSL票据回调的实际逻辑【由各worker在握手时调用】
参数说明:   iEnc=1: 签发新票据——填充票据名/IV, 装配加密与HMAC上下文
            iEnc=0: 解密客户端递回的票据——按票据名匹配当前/备用密钥, 装配解密上下文
返 回 值: 1=成功 0=票据无法使用(降级为完整握手并补发新票据) -1=致命错误
被引用于: SSL_CTX_set_tlsext_ticket_key_cb注册的回调
创建日期: 2026年10月09日
修改记录:
******************************************************************************************/
int CTicketKey::OnTicketKey(unsigned char acKeyName[16], unsigned char acIV[EVP_MAX_IV_LENGTH],
                            EVP_CIPHER_CTX *pEvpCtx, HMAC_CTX *pHmacCtx, int iEnc)
{
    if(Lock() == false)
        return -1;

    TICKET_SHM *pShm = (TICKET_SHM *)m_pShm;

    if(iEnc == 1)
    {
        //签发: 用当前密钥
        memcpy(acKeyName, pShm->itemCurr.acName, 16);
        if(RAND_bytes(acIV, EVP_MAX_IV_LENGTH) != 1)
        {
            Unlock();
            return -1;
        }
        EVP_EncryptInit_ex(pEvpCtx, EVP_aes_256_cbc(), NULL, pShm->itemCurr.acAesKey, acIV);
        HMAC_Init_ex(pHmacCtx, pShm->itemCurr.acHmacKey, sizeof(pShm->itemCurr.acHmacKey), EVP_sha256(), NULL);
    }
    else
    {
        //解密: 按票据名匹配当前或备用密钥(轮换过渡期旧票据仍可恢复)
        TICKET_KEY_ITEM *pMatch = NULL;
        if(memcmp(pShm->itemCurr.acName, acKeyName, 16) == 0)
            pMatch = &pShm->itemCurr;
        else if(pShm->itemPrev.uiCreateTime != 0 && memcmp(pShm->itemPrev.acName, acKeyName, 16) == 0)
            pMatch = &pShm->itemPrev;

        if(pMatch == NULL)
        {
            Unlock();
            return 0;  //票据名不认识(过期轮换/伪造): 降级为完整握手
        }

        EVP_DecryptInit_ex(pEvpCtx, EVP_aes_256_cbc(), NULL, pMatch->acAesKey, acIV);
        HMAC_Init_ex(pHmacCtx, pMatch->acHmacKey, sizeof(pMatch->acHmacKey), EVP_sha256(), NULL);
    }

    Unlock();
    return 1;
}

void CTicketKey::RegisterCb(SSL_CTX *pSSLCtx)
{
    if(pSSLCtx == NULL)
        return;
    if(SSL_CTX_set_tlsext_ticket_key_cb(pSSLCtx, NgxTicketKeyCb) == 0)
    {
        LOG_STDERR("注册TLS会话票据回调失败(OpenSSL过旧?), 降级为不支持会话恢复");
    }
    return;
}
