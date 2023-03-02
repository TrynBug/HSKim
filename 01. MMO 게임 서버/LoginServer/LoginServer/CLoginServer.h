#pragma once

#include "netserver/CNetServer.h"
#include "CLANClientMonitoring.h"

#include "defineLoginServer.h"
#include "CMemoryPoolTLS.h"
#include "CDBConnectorTLSManager.h"

class CClient;
class CDBConnectorTLSManager;
class CCpuUsage;
class CPDH;

namespace cpp_redis
{
	class client;
}

class CLoginServer : public netserver::CNetServer
{
private:
	/* 경로 */
	WCHAR _szCurrentPath[MAX_PATH];     // 현재 디렉토리 경로
	WCHAR _szConfigFilePath[MAX_PATH];  // config 파일 경로

	/* config */
	wchar_t _szBindIP[20];
	unsigned short _portNumber;
	wchar_t _szGameServerIP[20];
	wchar_t _szGameServerDummy1IP[20];
	wchar_t _szGameServerDummy2IP[20];
	unsigned short _gameServerPort;
	wchar_t _szChatServerIP[20];
	wchar_t _szChatServerDummy1IP[20];
	wchar_t _szChatServerDummy2IP[20];
	unsigned short _chatServerPort;
	int _numConcurrentThread;
	int _numWorkerThread;
	int _numMaxSession;
	int _numMaxClient;
	bool _bUseNagle;

	BYTE _packetCode;
	BYTE _packetKey;
	int _maxPacketSize;

	unsigned short _monitoringServerPortNumber;
	BYTE _monitoringServerPacketCode;
	BYTE _monitoringServerPacketKey;

	/* server */
	int _serverNo;                         // 서버번호(로그인서버:3)
	HANDLE _hThreadPeriodicWorker;         // 주기적 작업 스레드
	unsigned int _threadPeriodicWorkerId;
	HANDLE _hThreadMonitoringCollector;    // 모니터링 데이터 수집 스레드
	unsigned int _threadMonitoringCollectorId;

	volatile bool _bShutdown;               // shutdown 여부
	volatile bool _bTerminated;             // 종료됨 여부
	LARGE_INTEGER _performanceFrequency;


	/* timeout */
	int _timeoutLogin;
	int _timeoutLoginCheckPeriod;

	/* TLS DBConnector */
	CDBConnectorTLSManager* _tlsDBConnector;

	/* Client */
	std::unordered_map<__int64, CClient*> _mapClient;
	SRWLOCK _srwlMapClient;

	/* account */
	std::unordered_map<__int64, __int64> _mapAccountToSession;
	SRWLOCK _srwlMapAccountToSession;

	/* pool */
	CMemoryPoolTLS<CClient> _poolClient;      // 클라이언트 풀

	/* redis */
	cpp_redis::client* _pRedisClient;

	/* 모니터링 */
	CLANClientMonitoring* _pLANClientMonitoring;       // 모니터링 서버와 연결할 LAN 클라이언트
	CCpuUsage* _pCPUUsage;
	CPDH* _pPDH;

	volatile __int64 _loginCount = 0;                  // 로그인 성공 횟수
	volatile __int64 _disconnByClientLimit = 0;        // 클라이언트수 제한으로 끊김
	volatile __int64 _disconnByNoClient = 0;           // 세션번호에 대한 클라이언트 객체를 찾지못함
	volatile __int64 _disconnByDBDataError = 0;        // DB 데이터 오류
	volatile __int64 _disconnByNoAccount = 0;          // account 테이블에서 account 번호를 찾지 못함
	volatile __int64 _disconnByDupAccount = 0;         // 동일 account가 다른스레드에서 로그인을 진행중임
	volatile __int64 _disconnByInvalidMessageType = 0; // 메시지 타입이 잘못됨
	volatile __int64 _disconnByLoginTimeout = 0;       // 로그인 타임아웃으로 끊김
	volatile __int64 _disconnByHeartBeatTimeout = 0;   // 하트비트 타임아웃으로 끊김

public:
	CLoginServer();
	virtual ~CLoginServer();
	
public:
	/* StartUp */
	bool StartUp();

	/* Shutdown */
	bool Shutdown();

	/* 서버 상태 */
	int GetNumClient() { return (int)_mapClient.size(); }
	int GetSizeAccountMap() { return (int)_mapAccountToSession.size(); }
	int GetClientPoolSize() { return _poolClient.GetPoolSize(); };
	int GetClientAllocCount() { return _poolClient.GetAllocCount(); }
	int GetClientActualAllocCount() { return _poolClient.GetActualAllocCount(); }
	int GetClientFreeCount() { return _poolClient.GetFreeCount(); }
	int GetNumDBConnection() { return _tlsDBConnector->GetNumConnection(); }
	__int64 GetQueryRunCount() { return _tlsDBConnector->GetQueryRunCount(); }
	float GetMaxQueryRunTime() { return _tlsDBConnector->GetMaxQueryRunTime(); }
	float GetMinQueryRunTime() { return _tlsDBConnector->GetMinQueryRunTime(); }
	float GetAvgQueryRunTime() { return _tlsDBConnector->GetAvgQueryRunTime(); }
	bool IsTerminated() { return _bTerminated; }


	/* Get 모니터링 */
	__int64 GetLoginCount() { return _loginCount; }
	__int64 GetDisconnByClientLimit() { return _disconnByClientLimit; }
	__int64 GetDisconnByNoClient() { return _disconnByNoClient; }
	__int64 GetDisconnByDBDataError() { return _disconnByDBDataError; }
	__int64 GetDisconnByNoAccount() { return _disconnByNoAccount; }
	__int64 GetDisconnByDupAccount() { return _disconnByDupAccount; }
	__int64 GetDisconnByInvalidMessageType() { return _disconnByInvalidMessageType; }
	__int64 GetDisconnByLoginTimeout() { return _disconnByLoginTimeout; }
	__int64 GetDisconnByHeartBeatTimeout() { return _disconnByHeartBeatTimeout; }


private:
	/* Get */
	CClient* GetClientBySessionId(__int64 sessionId);   // 세션ID로 클라이언트 객체를 얻는다. 실패할경우 null을 반환한다.
	
	/* Client */
	void InsertClientToMap(__int64 sessionId, CClient* pClient);   // 세션ID-클라이언트 map에 클라이언트를 등록한다. 
	void DeleteClientFromMap(__int64 sessionId);                   // 세션ID-클라이언트 map에서 클라이언트를 삭제한다.
	void DisconnectSession(__int64 sessionId);                     // 클라이언트의 연결을 끊는다.
	void DeleteClient(CClient* pClient);                           // 클라이언트를 map에서 삭제하고 free 한다.

	CClient* AllocClient(__int64 sessionId);
	void FreeClient(CClient* pClient);

	/* account */
	bool InsertAccountToMap(__int64 accountNo, __int64 sessionId);     // account번호를 account번호-세션ID map에 등록한다. 만약 accountNo에 해당하는 데이터가 이미 존재할 경우 실패하며 false를 반환한다.
	void DeleteAccountFromMap(__int64 accountNo);   // account번호를 account번호-세션ID map에서 제거한다.

	/* 네트워크 전송 */
	int SendUnicast(__int64 sessionId, netserver::CPacket* pPacket);
	int SendUnicastAndDisconnect(__int64 sessionId, netserver::CPacket* pPacket);

	/* 스레드 */
	static unsigned WINAPI ThreadPeriodicWorker(PVOID pParam);       // 주기적 작업 처리 스레드
	static unsigned WINAPI ThreadMonitoringCollector(PVOID pParam);

	/* crash */
	void Crash();


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

