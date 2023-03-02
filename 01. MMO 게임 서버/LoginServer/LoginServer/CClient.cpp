#include <Windows.h>
#include "defineLoginServer.h"
#include "CClient.h"



CClient::CClient()
	:_sessionId(-1), _accountNo(-1), _id{ 0, }, _nickname{ 0, }, _sessionKey{ 0, }
	, _bLogin(false), _bDisconnect(false), _lastHeartBeatTime(0)
{
	InitializeSRWLock(&_srwlClient);
}

CClient::~CClient()
{

}


void CClient::Init(__int64 sessionId)
{
	_sessionId = sessionId;
	_accountNo = -1;
	_bLogin = false;
	_bDisconnect = false;
	_lastHeartBeatTime = GetTickCount64();
}

void CClient::SetClientInfo(INT64 accountNo, WCHAR id[20], WCHAR nickname[20], char sessionKey[64])
{
	_accountNo = accountNo;
	memcpy(_id, id, sizeof(_id));
	memcpy(_nickname, nickname, sizeof(_nickname));
	memcpy(_sessionKey, sessionKey, sizeof(_sessionKey));
}

void CClient::SetLogin()
{
	_bLogin = true;
	_lastHeartBeatTime = GetTickCount64();
}
