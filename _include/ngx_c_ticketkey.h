#pragma once

#include <stdint.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

//TLS会话票据密钥表【跨worker共享】
//原理: TLS会话票据=服务器把会话状态加密后发给客户端保管, 重连时客户端递回票据, 服务器解密恢复会话,
//      跳过昂贵的非对称握手(证书验证/ECDHE)。票据密钥只有服务器知道——必须所有worker共享同一套,
//      否则客户端的票据在别的worker上无法解密, 会话恢复失效。
//实现: 密钥存于共享内存(nginx MAP_ANON方案), master在fork前生成; SIGHUP重载时轮换
//     (当前密钥→备用密钥, 新密钥上台——已发出的旧票据在轮换后仍可恢复, 平滑过渡)。
//安全注意: 票据密钥长期不换会削弱前向安全, 建议随每次SIGHUP轮换(或定期SIGHUP)。

//单把票据密钥: name(16) + AES-256-CBC密钥(32) + HMAC-SHA256密钥(32)
struct TICKET_KEY_ITEM
{
	uint64_t     uiVer;             //密钥版本号(每次轮换+1)
	uint64_t     uiCreateTime;      //创建时间(秒)
	unsigned char acName[16];       //票据名: 服务器发给客户端的票据里带此名, 解密时据此选密钥
	unsigned char acAesKey[32];     //AES-256-CBC密钥(加密会话状态)
	unsigned char acHmacKey[32];    //HMAC-SHA256密钥(票据完整性校验)
};

class CTicketKey
{
private:
	CTicketKey();

public:
	static CTicketKey* GetInstance();   //C++11 Meyers单例

	bool Init(bool bIsMaster);          //master: 创建共享内存并生成首把密钥[必须在fork之前]; worker: 校验继承的映射
	bool Rotate();                      //轮换: 当前密钥转备用, 生成新当前密钥[SIGHUP时由master调用]
	void RegisterCb(SSL_CTX *pSSLCtx);  //把票据回调注册到指定SSL_CTX[内部转发OnTicketKey]
	int  OnTicketKey(unsigned char acKeyName[16], unsigned char acIV[EVP_MAX_IV_LENGTH],
	                 EVP_CIPHER_CTX *pEvpCtx, HMAC_CTX *pHmacCtx, int iEnc);  //OpenSSL票据回调的实际逻辑

private:
	bool Lock();
	void Unlock();

private:
	void            *m_pShm;      //共享内存映射地址(master创建, worker经fork继承)
	bool             m_bInited;
};
