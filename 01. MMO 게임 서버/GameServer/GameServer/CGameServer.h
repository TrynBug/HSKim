#pragma once

#include "game_netserver/CNetServer.h"
#include "defineGameServer.h"
#include "CPlayer.h"

#include "cpp_redis.h"

#include "CMemoryPool.h"

class CJsonParser;
class CLANClientMonitoring;
class CCpuUsage;
class CPDH;

class CGameServer : public game_netserver::CNetServer
{
	friend class CGameContents;

private:
	/* 경로 */
	WCHAR _szCurrentPath[MAX_PATH];     // 현재 디렉토리 경로
	WCHAR _szConfigFilePath[MAX_PATH];  // config 파일 경로

	/* json */
	CJsonParser* _configJsonParser;    // config.json 파일 parser

	/* config */
	wchar_t _szBindIP[20];
	unsigned short _portNumber;
	int _numConcurrentThread;
	int _numWorkerThread;
	int _numMaxSession;
	int _numMaxPlayer;
	bool _bUseNagle;

	BYTE _packetCode;
	BYTE _packetKey;
	int _maxPacketSize;

	unsigned short _monitoringServerPortNumber;
	BYTE _monitoringServerPacketCode;
	BYTE _monitoringServerPacketKey;

	/* game server */
	int _serverNo;                          // 서버번호(게임서버:2)
	volatile bool _bShutdown;               // shutdown 여부
	volatile bool _bTerminated;             // 종료됨 여부

	/* redis */
	cpp_redis::client* _pRedisClient;

	/* 컨텐츠 */
	game_netserver::CContents* _arrContentsPtr[(UINT)EContentsThread::END];   // 컨텐츠 클래스 포인터들을 담을 배열

	/* 플레이어 */
	//CMemoryPoolTLS<CPlayer> _poolPlayer;               // 플레이어 풀
	CMemoryPool<CPlayer> _poolPlayer;

	/* 모니터링 */
	CLANClientMonitoring* _pLANClientMonitoring;       // 모니터링 서버와 연결할 LAN 클라이언트
	HANDLE _hThreadMonitoringCollector;                // 모니터링 데이터 수집 스레드
	unsigned int _threadMonitoringCollectorId;
	CCpuUsage* _pCPUUsage;
	CPDH* _pPDH;

	volatile __int64 _disconnByPlayerLimit = 0;        // 플레이어수 제한으로 끊김

public:
	CGameServer();
	virtual ~CGameServer();

public:
	/* game server */
	bool StartUp();
	void Shutdown();
	bool IsTerminated() { return _bTerminated; }   // 종료됨 여부

	/* Get */
	int GetPlayerPoolSize() { return _poolPlayer.GetPoolSize(); }
	//int GetPlayerAllocCount() { return _poolPlayer.GetAllocCount(); }
	//int GetPlayerActualAllocCount() { return _poolPlayer.GetActualAllocCount(); }
	int GetPlayerAllocCount() { return _poolPlayer.GetUseCount(); }
	int GetPlayerActualAllocCount() { return _poolPlayer.GetUseCount(); }
	int GetPlayerFreeCount() { return _poolPlayer.GetFreeCount(); }

	/* 컨텐츠 */
	const CGameContents* GetGameContentsPtr(EContentsThread contents);

	/* 스레드 */
	static unsigned WINAPI ThreadMonitoringCollector(PVOID pParam);  // 모니터링 스레드

private:
	/* 컨텐츠 */
	game_netserver::CContents* GetContentsPtr(EContentsThread contents);

	/* 네트워크 callback 함수 구현 */
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port); 
	virtual void OnError(const wchar_t* szError, ...);
	virtual void OnOutputDebug(const wchar_t* szError, ...);
	virtual void OnOutputSystem(const wchar_t* szError, ...);
};



