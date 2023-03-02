#pragma once


class CUser
{
	friend class CMonitoringServer;

	__int64 _sessionId;
	int	    _serverNo;      // 서버 번호. 외부 클라이언트일 경우 -1
	char    _loginKey[32];  // 로그인 키. 외부 클라이언트만 가짐

	bool _bLogin;                  // 로그인여부
	bool _bDisconnect;             // 연결끊김 여부
	ULONGLONG _lastHeartBeatTime;  // 마지막으로 heart beat 받은 시간

public:
	CUser();
	~CUser();

public:
	void Init(__int64 sessionId);

	bool IsWANUser() { return _serverNo == -1; }

	void SetLANUserInfo(int serverNo);
	void SetWANUserInfo(char loginKey[32]);
	void SetLogin();
	void SetHeartBeatTime() { _lastHeartBeatTime = GetTickCount64(); }
};

