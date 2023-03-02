#include <Windows.h>
#include "defineChatServer.h"
#include "CPlayer.h"



CPlayer::CPlayer()
	:_sessionId(-1), _accountNo(-1), _id{ 0, }, _nickname{ 0, }, _sessionKey{ 0, }
	, _bLogin(false), _bDisconnect(false), _lastHeartBeatTime(0)
	, _sectorX(SECTOR_NOT_SET), _sectorY(SECTOR_NOT_SET)
{

}

CPlayer::~CPlayer()
{

}


void CPlayer::Init(__int64 sessionId)
{
	_sessionId = sessionId;
	_accountNo = -1;
	_bLogin = false;
	_bDisconnect = false;
	_lastHeartBeatTime = GetTickCount64();

	_sectorX = SECTOR_NOT_SET;
	_sectorY = SECTOR_NOT_SET;
}

void CPlayer::SetPlayerInfo(INT64 accountNo, WCHAR id[20], WCHAR nickname[20], char sessionKey[64])
{
	_accountNo = accountNo;
	memcpy(_id, id, sizeof(_id));
	memcpy(_nickname, nickname, sizeof(_nickname));
	memcpy(_sessionKey, sessionKey, sizeof(_sessionKey));
}

void CPlayer::SetLogin()
{
	_bLogin = true;
	_lastHeartBeatTime = GetTickCount64();
}
