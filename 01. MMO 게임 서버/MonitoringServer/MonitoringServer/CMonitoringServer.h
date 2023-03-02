#pragma once

#include "netserver/CNetServer.h"
#include "defineMonitoringServer.h"
#include "CDBConnector.h"
#include "CMemoryPool.h"


#include <sstream>

class CUser;
class CDBConnector;

class alignas(64) CMonitoringServer : public netserver::CNetServer
{
private:
	/* 경로 */
	WCHAR _szCurrentPath[MAX_PATH];     // 현재 디렉토리 경로
	WCHAR _szConfigFilePath[MAX_PATH];  // config 파일 경로

	/* config */
	wchar_t _szBindIP[20];
	unsigned short _portNumber;
	int _numMaxSession;
	int _numMaxUser;
	bool _bUseNagle;
	bool _bUsePacketEncryption;  // 패킷 암호화 사용 여부

	BYTE _packetCode;
	BYTE _packetKey;
	int _maxPacketSize;


	/* 유저 */
	std::unordered_map<__int64, CUser*> _mapUser;
	std::unordered_map<__int64, CUser*> _mapWANUser;
	CMemoryPool<CUser> _poolUser;

	/* 서버 */
	char _loginKey[32];          // login key    
	volatile bool _bShutdown;    // shutdown 여부
	volatile bool _bTerminated;  // 종료됨 여부
	int _timeoutLogin;           // 로그인 타임아웃 대상시간

	/* DB */
	CDBConnector* _pDBConn;
	std::wostringstream _ossQuery;

	/* 스레드 */
	HANDLE _hThreadJobGenerator;
	unsigned int _idThreadJobGenerator;

	/* 서버로부터 수집한 모니터링 데이터 */
	struct MonitoringDataSet
	{
		int dataCollectCount = 0;
		int arrValue5m[300] = { 0, };
	};
	MonitoringDataSet* _arrMonitoringDataSet;



	/* 모니터링 */
	volatile __int64 _msgHandleCount = 0;              // 메시지 처리 횟수
	volatile __int64 _receivedInvalidDatatype = 0;     // 잘못된 모니터링 데이터 타입을 받음
	volatile __int64 _disconnByUserLimit = 0;          // 유저수 제한으로 끊김
	volatile __int64 _disconnByInvalidMessageType = 0; // 메시지 타입이 잘못됨
	volatile __int64 _disconnByLoginKeyMismatch = 0;   // 로그인 키가 잘못됨
	volatile __int64 _disconnByWANUserUpdateRequest = 0;  // WAN 유저가 데이터 업데이트 요청을 보냄
	volatile __int64 _DBErrorCount = 0;                // DB 오류 횟수

public:
	CMonitoringServer();
	~CMonitoringServer();

	/* StartUp */
	bool StartUp();

	/* Shutdown */
	bool Shutdown();

	/* Get 서버상태 */
	int IsTerminated() { return _bTerminated; }
	int GetNumUser() { return (int)_mapUser.size(); }
	int GetNumWANUser() { return (int)_mapWANUser.size(); }
	int GetUserPoolSize() { return _poolUser.GetPoolSize(); };
	int GetUserAllocCount() { return _poolUser.GetUseCount(); }
	int GetUserFreeCount() { return _poolUser.GetFreeCount(); }
	__int64 GetQueryRunCount() { return _pDBConn->GetQueryRunCount(); }
	float GetMaxQueryRunTime() { return _pDBConn->GetMaxQueryRunTime(); }
	float GetMinQueryRunTime() { return _pDBConn->GetMinQueryRunTime(); }
	float GetAvgQueryRunTime() { return _pDBConn->GetAvgQueryRunTime(); }


	/* Get 모니터링 */
	__int64 GetMsgHandleCount() { return _msgHandleCount; }
	__int64 GetReceivedInvalidDatatype() { return _receivedInvalidDatatype; }
	__int64 GetDisconnByUserLimit() { return _disconnByUserLimit; }
	__int64 GetDisconnByInvalidMessageType() { return _disconnByInvalidMessageType; }
	__int64 GetDisconnByLoginKeyMismatch() { return _disconnByLoginKeyMismatch; }
	__int64 GetDisconnByWANUserUpdateRequest() { return _disconnByWANUserUpdateRequest; }
	__int64 GetDBErrorCount() { return _DBErrorCount; }

	/* dynamic alloc */
	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

private:
	/* 네트워크 전송 */
	int SendUnicast(__int64 sessionId, netserver::CPacket* pPacket);

	/* 유저 */
	CUser* AllocUser(__int64 sessionId);
	void FreeUser(CUser* pUser);
	CUser* GetUserBySessionId(__int64 sessionId);
	void DisconnectSession(__int64 sessionId);
	void DisconnectSession(CUser* pUser);

	/* Thread */
	static unsigned WINAPI ThreadJobGenerator(PVOID pParam);

	/* crash */
	void Crash();

private:
	// 네트워크 라이브러리 함수 구현
	virtual bool OnRecv(__int64 sessionId, netserver::CPacket& packet);
	virtual bool OnInnerRequest(netserver::CPacket& packet);
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port);
	virtual bool OnClientJoin(__int64 sessionId);
	virtual bool OnClientLeave(__int64 sessionId);
	virtual void OnError(const wchar_t* szError, ...);
	virtual void OnOutputDebug(const wchar_t* szError, ...);
	virtual void OnOutputSystem(const wchar_t* szError, ...);
};

