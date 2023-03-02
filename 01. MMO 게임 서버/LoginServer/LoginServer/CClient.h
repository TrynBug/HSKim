#pragma once

class CClient
{
	friend class CLoginServer;

	__int64 _sessionId;
	INT64	_accountNo;
	WCHAR	_id[20];			// null 포함
	WCHAR	_nickname[20];		// null 포함
	char	_sessionKey[64];	// 인증토큰

	bool _bLogin;                  // 로그인여부
	bool _bDisconnect;             // 연결끊김 여부
	ULONGLONG _lastHeartBeatTime;  // 마지막으로 heart beat 받은 시간

	SRWLOCK _srwlClient;

public:
	CClient();
	~CClient();

public:
	void Init(__int64 sessionId);

	void SetClientInfo(INT64 accountNo, WCHAR id[20], WCHAR nickname[20], char sessionKey[64]);
	void SetLogin();
	void SetHeartBeatTime() { _lastHeartBeatTime = GetTickCount64(); }

	void LockExclusive() { AcquireSRWLockExclusive(&_srwlClient); }
	void UnlockExclusive() { ReleaseSRWLockExclusive(&_srwlClient); }
	void LockShared() { AcquireSRWLockShared(&_srwlClient); }
	void UnlockShared() { ReleaseSRWLockShared(&_srwlClient); }
};

