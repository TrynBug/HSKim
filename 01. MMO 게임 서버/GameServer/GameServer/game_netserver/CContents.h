#pragma once

#define CONTENTS_MODE_SEND_PACKET_IMMEDIATELY   0x1

namespace game_netserver
{

class CNetServer;
class CPacket;
class CTimeMgr;

class alignas(64) CContents
{
	friend class CNetServer;

private:
	/* 네트워크 */
	CNetServer* _pNetServer;       // 네트워크 서버 포인터. 컨텐츠 클래스 객체를 네트워크 라이브러리에 attach할 때 설정된다.

	/* 컨텐츠 */
	HANDLE _hThreadContents;                  // 컨텐츠 스레드 핸들
	CLockfreeQueue<MsgContents*> _msgQ;       // 스레드 메시지큐
	bool _bTerminated;                        // 스레드 종료됨 여부
	DWORD _mode;                              // 모드

	/* 세션 */
	std::unordered_map<__int64, CSession*> _mapSession;     // 세션 맵
	int _sessionPacketProcessLimit;                         // 하나의 세션당 한 프레임에 처리할 수 있는 최대 패킷 수

	/* 하트비트 */
	bool _bEnableHeartBeatTimeout;
	ULONGLONG _timeoutHeartBeat;
	int _timeoutHeartBeatCheckPeriod;
	ULONGLONG _lastHeartBeatCheckTime;

	/* 프레임 */
	int _FPS;                            // FPS
	__int64 _oneFrameTime;               // performance counter 기준의 1프레임당 count
	LARGE_INTEGER _logicStartTime;       // 로직 시작 performance count
	LARGE_INTEGER _logicEndTime;         // 로직 종료 performance count
	LARGE_INTEGER _performanceFrequency; // frequency
	__int64 _catchUpTime;                // FPS를 유지하기위해 따라잡아야할 performance count. 이 값만큼 덜 sleep 해야한다.

	/* Time */
	CTimeMgr* _pTimeMgr;    // 컨텐츠 스레드 시작 시 초기화한다.

	/* 모니터링 */
	volatile __int64 _updateCount = 0;
	volatile __int64 _internalMsgCount = 0;
	volatile __int64 _sessionMsgCount = 0;
	volatile __int64 _disconnByHeartBeat = 0;

public:
	CContents(int FPS);
	CContents(int FPS, DWORD mode);
	virtual ~CContents();

private:
	/* 컨텐츠 */
	bool StartUp();     // 스레드 시작
	void Shutdown();    // 스레드 종료
	void InsertMessage(MsgContents* pMsg);      // 스레드 메시지큐에 메시지 삽입
	void SyncLogicTime();                       // 로직 실행시간을 FPS에 맞춘다. 수행못한 프레임은 버린다. 최대 FPS가 정해진 값으로 유지된다.
	void SyncLogicTimeAndCatchUp();             // 로직 실행시간을 FPS에 맞춘다. 만약 수행못한 프레임이 있을 경우 한가할때 따라잡는다. 최대 FPS가 정해진 값보다 높아질 수 있다.


	/* 스레드 */
	static unsigned WINAPI ThreadContents(PVOID pParam);    // 컨텐츠 스레드 함수

	/* 사용자 구현 callback 함수 */
	virtual void OnUpdate() = 0;                                     // 주기적으로 호출됨
	virtual void OnRecv(__int64 sessionId, CPacket* pPacket) = 0;    // 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
	virtual void OnSessionJoin(__int64 sessionId, PVOID data) = 0;   // 스레드에 세션이 들어왔을 때 호출됨. 만약 최초 접속한 세션이 들어온 경우 data가 null임
	virtual void OnSessionDisconnected(__int64 sessionId) = 0;       // 스레드가 관리하는 세션의 연결이 끊겼을때 호출됨

	/* 세션 (private) */
	void _sendContentsPacket(CSession* pSession);               // 세션 내의 컨텐츠 패킷 벡터 내의 패킷을 전송한다.
	void _sendContentsPacketAndDisconnect(CSession* pSession);  // 세션 내의 컨텐츠 패킷 벡터 내의 패킷을 전송하고 연결을 끊는다.
	void DisconnectSession(CSession* pSession);                // 세션의 연결을 끊는다. 

public:
	/* 세션 */
	const wchar_t* GetSessionIP(__int64 sessionId);
	unsigned short GetSessionPort(__int64 sessionId);
	void TransferSession(__int64 sessionId, CContents* destination, PVOID data);   // 세션을 destination 컨텐츠로 옮긴다. 세션에 대한 메시지는 현재 스레드에서는 더이상 처리되지 않는다.
	void DisconnectSession(__int64 sessionId);   // 세션의 연결을 끊는다. 


	/* packet */
	CPacket* AllocPacket();
	bool SendPacket(__int64 sessionId, CPacket* pPacket);                 // 세션의 컨텐츠 패킷 벡터에 패킷을 삽입한다.
	bool SendPacket(__int64 sessionId, CPacket** arrPtrPacket, int numPacket);  // 세션의 컨텐츠 패킷 벡터에 패킷을 삽입한다.
	bool SendPacketAndDisconnect(__int64 sessionId, CPacket* pPacket);    // 세션의 컨텐츠 패킷 벡터에 패킷을 삽입한다. 패킷을 보낸다음 연결이 끊긴다.


	/* Set */
	void SetSessionPacketProcessLimit(int limit) { _sessionPacketProcessLimit = limit; }   // 하나의 세션당 한 프레임에 처리할 수 있는 최대 패킷 수 설정
	void EnableHeartBeatTimeout(int timeoutCriteria);   // 하트비트 타임아웃 기준시간 설정
	void DisableHeartBeatTimeout();                     // 하트비트 타임아웃 비활성화

	/* Time */
	double GetDT() const;
	float GetfDT() const;
	double GetAvgDT1s() const;
	double GetMinDT1st() const;
	double GetMinDT2nd() const;
	double GetMaxDT1st() const;
	double GetMaxDT2nd() const;
	int GetFPS() const;
	float GetAvgFPS1m() const;
	int GetMinFPS1st() const;
	int GetMinFPS2nd() const;
	int GetMaxFPS1st() const;
	int GetMaxFPS2nd() const;
	bool OneSecondTimer() const;

	/* Get 모니터링 */
	__int64 GetUpdateCount() const { return _updateCount; }
	__int64 GetInternalMsgCount() const { return _internalMsgCount; }
	__int64 GetSessionMsgCount() const { return _sessionMsgCount; }
	__int64 GetDisconnByHeartBeat() const { return _disconnByHeartBeat; }

	/* dynamic alloc */
	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

};


}