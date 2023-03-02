#pragma once

#include "netserver/CNetServer.h"
#include "CLANClientMonitoring.h"

#include "defineChatServer.h"
#include "CMemoryPoolTLS.h"
#include "CLockfreeQueue.h"


class CSector;
class CPlayer;
class CDBAsyncWriter;
class CCpuUsage;
class CPDH;

namespace cpp_redis
{
	class client;
}


class CChatServer : public netserver::CNetServer
{
private:
	/* 경로 */
	WCHAR _szCurrentPath[MAX_PATH];     // 현재 디렉토리 경로
	WCHAR _szConfigFilePath[MAX_PATH];  // config 파일 경로

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

	/* redis */
	cpp_redis::client* _pRedisClient;

	/* Async DB Writer */
	CDBAsyncWriter* _pDBConn;

	/* sector */
	CSector** _sector;

	/* 플레이어 */
	std::unordered_map<__int64, CPlayer*> _mapPlayer;
	std::unordered_map<__int64, CPlayer*> _mapPlayerAccountNo;
	CMemoryPoolTLS<CPlayer> _poolPlayer;

	/* 채팅서버 */
	int _serverNo;                         // 서버번호(채팅서버:1)
	HANDLE _hThreadChatServer;             // 채팅 스레드
	unsigned int _threadChatServerId;
	HANDLE _hThreadMsgGenerator;           // 주기적 작업 메시지 발생 스레드
	unsigned int _threadMsgGeneratorId;
	HANDLE _hThreadMonitoringCollector;    // 모니터링 데이터 수집 스레드
	unsigned int _threadMonitoringCollectorId;

	volatile bool _bShutdown;               // shutdown 여부
	volatile bool _bTerminated;             // 종료됨 여부
	LARGE_INTEGER _performanceFrequency;

	/* 메시지 */
	CLockfreeQueue<MsgChatServer*> _msgQ;
	CMemoryPoolTLS<MsgChatServer>  _poolMsg;
	CMemoryPoolTLS<StAPCData> _poolAPCData;     // redis APC 요청을 위한 데이터 풀
	HANDLE _hEventMsg;
	bool _bEventSetFlag;

	/* timeout */
	int _timeoutLogin;
	int _timeoutHeartBeat;
	int _timeoutLoginCheckPeriod;
	int _timeoutHeartBeatCheckPeriod;
	
	/* 모니터링 */
	CLANClientMonitoring* _pLANClientMonitoring;       // 모니터링 서버와 연결할 LAN 클라이언트
	CCpuUsage* _pCPUUsage;
	CPDH* _pPDH;

	volatile __int64 _eventWaitTime = 0;              // 이벤트를 기다린 횟수
	volatile __int64 _msgHandleCount = 0;              // 메시지 처리 횟수
	volatile __int64 _disconnByPlayerLimit = 0;        // 플레이어수 제한으로 끊김
	volatile __int64 _disconnByLoginFail = 0;          // 로그인 실패로 끊김
	volatile __int64 _disconnByNoSessionKey = 0;       // redis에 세션key가 없음
	volatile __int64 _disconnByInvalidSessionKey = 0;  // redis의 세션key가 다름
	volatile __int64 _disconnByDupPlayer = 0;          // 중복로그인으로 끊김
	volatile __int64 _disconnByInvalidAccountNo = 0;   // accountNo가 잘못됨
	volatile __int64 _disconnByInvalidSector = 0;      // Sector가 잘못됨
	volatile __int64 _disconnByInvalidMessageType = 0; // 메시지 타입이 잘못됨
	volatile __int64 _disconnByLoginTimeout = 0;       // 로그인 타임아웃으로 끊김
	volatile __int64 _disconnByHeartBeatTimeout = 0;   // 하트비트 타임아웃으로 끊김
	
	

public:
	CChatServer();
	virtual ~CChatServer();

public:
	/* StartUp */
	bool StartUp();

	/* Get 서버상태 */
	int GetMsgPoolSize() { return _poolMsg.GetPoolSize(); };
	int GetMsgAllocCount() { return _poolMsg.GetAllocCount(); }
	int GetMsgActualAllocCount() { return _poolMsg.GetActualAllocCount(); }
	int GetMsgFreeCount() { return _poolMsg.GetFreeCount(); }
	int GetUnhandeledMsgCount() { return _msgQ.Size(); }
	int GetNumPlayer() { return (int)_mapPlayer.size(); }
	int GetNumAccount() { return (int)_mapPlayerAccountNo.size(); }
	int GetPlayerPoolSize() { return _poolPlayer.GetPoolSize(); };
	int GetPlayerAllocCount() { return _poolPlayer.GetAllocCount(); }
	int GetPlayerActualAllocCount() { return _poolPlayer.GetActualAllocCount(); }
	int GetPlayerFreeCount() { return _poolPlayer.GetFreeCount(); }
	bool IsTerminated() { return _bTerminated; }   // 종료됨 여부

	/* Get 모니터링 */
	__int64 GetEventWaitTime() { return _eventWaitTime; }
	__int64 GetMsgHandleCount() { return _msgHandleCount; }
	__int64 GetDisconnByPlayerLimit() { return _disconnByPlayerLimit; }
	__int64 GetDisconnByLoginFail() { return _disconnByLoginFail; }
	__int64 GetDisconnByNoSessionKey() { return _disconnByNoSessionKey; }
	__int64 GetDisconnByInvalidSessionKey() { return _disconnByInvalidSessionKey; }
	__int64 GetDisconnByDupPlayer() { return _disconnByDupPlayer; }
	__int64 GetDisconnByInvalidAccountNo() { return _disconnByInvalidAccountNo; }
	__int64 GetDisconnByInvalidSector() { return _disconnByInvalidSector; }
	__int64 GetDisconnByInvalidMessageType() { return _disconnByInvalidMessageType; }
	__int64 GetDisconnByLoginTimeout() { return _disconnByLoginTimeout; }
	__int64 GetDisconnByHeartBeatTimeout() { return _disconnByHeartBeatTimeout; }


	/* DB 상태 */
	int GetUnprocessedQueryCount();
	__int64 GetQueryRunCount();
	float GetMaxQueryRunTime();
	float Get2ndMaxQueryRunTime();
	float GetMinQueryRunTime();
	float Get2ndMinQueryRunTime();
	float GetAvgQueryRunTime();
	int GetQueryRequestPoolSize();
	int GetQueryRequestAllocCount();
	int GetQueryRequestFreeCount();

	/* Shutdown */
	bool Shutdown();

private:
	/* Get */
	CPlayer* GetPlayerBySessionId(__int64 sessionId);
	CPlayer* GetPlayerByAccountNo(__int64 accountNo);

	/* 네트워크 전송 */
	int SendUnicast(__int64 sessionId, netserver::CPacket* pPacket);
	int SendUnicast(CPlayer* pPlayer, netserver::CPacket* pPacket);
	int SendBroadcast(netserver::CPacket* pPacket);
	int SendOneSector(CPlayer* pPlayer, netserver::CPacket* pPacket, CPlayer* except);
	int SendAroundSector(CPlayer* pPlayer, netserver::CPacket* pPacket, CPlayer* except);

	/* player */
	void MoveSector(CPlayer* pPlayer, WORD x, WORD y);
	void DisconnectPlayer(CPlayer* pPlayer);
	void DeletePlayer(__int64 sessionId);
	std::unordered_map<__int64, CPlayer*>::iterator DeletePlayer(std::unordered_map<__int64, CPlayer*>::iterator& iterPlayer);
	void ReplacePlayerByAccountNo(__int64 accountNo, CPlayer* replacePlayer);

	/* Thread */
	static unsigned WINAPI ThreadChatServer(PVOID pParam);
	static unsigned WINAPI ThreadMsgGenerator(PVOID pParam);
	static unsigned WINAPI ThreadMonitoringCollector(PVOID pParam);

	/* crash */
	void Crash();

	/* 로그인 나머지 처리 APC 함수 */
	static void CompleteUnfinishedLogin(ULONG_PTR pStAPCData);

private:
	// 네트워크 라이브러리 callback 함수 구현
	virtual bool OnRecv(__int64 sessionId, netserver::CPacket& packet);
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port);
	virtual bool OnClientJoin(__int64 sessionId);
	virtual bool OnClientLeave(__int64 sessionId);
	virtual void OnError(const wchar_t* szError, ...);
	virtual void OnOutputDebug(const wchar_t* szError, ...);
	virtual void OnOutputSystem(const wchar_t* szError, ...);

};

