#include <Windows.h>
#include "defineMonitoringServer.h"
#include "CUser.h"




CUser::CUser()
	:_sessionId(-1), _serverNo(-1), _loginKey{ 0, }
	, _bLogin(false), _bDisconnect(false), _lastHeartBeatTime(0)
{
}

CUser::~CUser()
{
}

void CUser::Init(__int64 sessionId)
{
	_sessionId = sessionId;
	_serverNo = -1;
	ZeroMemory(_loginKey, 32);
	_bLogin = false;
	_bDisconnect = false;
	_lastHeartBeatTime = GetTickCount64();
}

void CUser::SetLANUserInfo(int serverNo)
{
	_serverNo = serverNo;
}

void CUser::SetWANUserInfo(char loginKey[32])
{
	_serverNo = -1;
	memcpy(_loginKey, loginKey, 32);
}

void CUser::SetLogin()
{
	_bLogin = true;
	_lastHeartBeatTime = GetTickCount64();
}
