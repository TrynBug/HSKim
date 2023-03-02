#include <Windows.h>
#include <vector>
#include "defineGameServer.h"
#include "CPlayer.h"


CPlayer::CPlayer() 
	: _sessionId(0), _accountNo(0), _sessionKey{ 0, }
	, _bLogin(false), _bDisconnect(false), _lastHeartBeatTime(0)
	, _userId{ 0, }, _userNick{ 0, }
{

}

CPlayer::~CPlayer() 
{
}

void CPlayer::Init(__int64 sessionId)
{
	CObject::Init();

	_sessionId = sessionId;
	_accountNo = -1;
	_bLogin = false;
	_bDisconnect = false;
	_bNewPlayer = true;
	_lastHeartBeatTime = GetTickCount64();
}


void CPlayer::SetAccountInfo(const char* userId, const char* userNick, const wchar_t* ip, unsigned short port)
{
	MultiByteToWideChar(CP_UTF8, 0, userId, -1, _userId, sizeof(_userId) / sizeof(WCHAR));
	MultiByteToWideChar(CP_UTF8, 0, userNick, -1, _userNick, sizeof(_userNick) / sizeof(WCHAR));
	wcscpy_s(_szIP, sizeof(_szIP) / sizeof(wchar_t), ip);
	_port = port;
}


void CPlayer::SetCharacterInfo(BYTE characterType)
{
	_characterType = characterType;
}


void CPlayer::LoadCharacterInfo(BYTE characterType, float posX, float posY, int tileX, int tileY, USHORT rotation, int crystal, int hp, INT64 exp, USHORT level, BYTE die)
{
	_bNewPlayer = false;

	_characterType = characterType;
	_posX = posX;
	_posY = posY;
	_oldPosX = _posX;
	_oldPosY = _posY;
	_tileX = tileX;
	_tileY = tileY;
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_rotation = rotation;
	_crystal = crystal;
	_oldCrystal = _crystal;
	_maxHp = 5000;
	_hp = hp;
	_exp = exp;
	_level = level;
	_die = die;
	_sit = 0;
	_respawn = 0;
	_VKey = 0;
	_HKey = 0;

	if (_die == 0)
	{
		_bAlive = true;
	}
	else
	{
		_bAlive = false;
	}
	_bMove = false;
	_bAttack = false;
	_bHit = false;
	_damageAttack1 = 300;
	_damageAttack2 = 500;

	_amountSitHPRecovery = 1000;
	_sitStartTime = 0;
	_lastSitHPRecoveryTime = 0;

	_bJustDied = false;
	_bJustStandUp = false;
	_deadTime = 0;
}


void CPlayer::SetLogin(INT64 accountNo, char sessionKey[64])
{
	_accountNo = accountNo;
	memcpy(_sessionKey, sessionKey, sizeof(_sessionKey));
	_bLogin = true;
	_lastHeartBeatTime = GetTickCount64();
}

void CPlayer::SetSit()
{
	_sit = 1;
	_sitStartHp = _hp;
	_sitStartTime = GetTickCount64();
	_lastSitHPRecoveryTime = GetTickCount64();
}


void CPlayer::SetRespawn(int tileX, int tileY)
{
	_posX = dfFIELD_TILE_TO_POS(tileX);
	_posY = dfFIELD_TILE_TO_POS(tileY);
	_oldPosX = _posX;
	_oldPosY = _posY;
	_tileX = tileX;
	_tileY = tileY;
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_sectorX = dfFIELD_TILE_TO_SECTOR(tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(tileY);
	_oldSectorX = _sectorX;
	_oldSectorY = _sectorY;
	_hp = _maxHp;
	_die = 0;
	_bAlive = true;
	_bMove = false;
	_bAttack = false;
	_bHit = false;

	_bJustDied = false;
	_bJustStandUp = false;
	_deadTime = 0;
}

void CPlayer::AddCrystal(int amount)
{
	_oldCrystal = _crystal;
	_crystal += amount;
	if (_crystal < 0)
		_crystal = 0;
}


void CPlayer::Hit(int damage)
{
	_hp -= damage;
	_hp = max(0, _hp);
	_bHit = true;
}



// @Override
void CPlayer::Update()
{
	ULONGLONG currentTime = GetTickCount64();
	_bJustDied = false;
	_bJustStandUp = false;

	// 사망 처리
	if (_bAlive == true && _hp <= 0)
	{
		_bAlive = false;
		_die = 1;

		_bJustDied = true;
		_deadTime = currentTime;
	}


	if (_bAlive == true)
	{
		// 플레이어가 앉아있는데 맞았을 경우
		if (_sit == 1 && _bHit == true)
		{
			_sit = 0;
			_bJustStandUp = true;
		}
		// 플레이어가 앉기 상태일 경우 1초마다 체력회복
		else if (_sit == 1)
		{
			while (currentTime > _lastSitHPRecoveryTime + 1000)
			{
				_hp += _amountSitHPRecovery;
				_lastSitHPRecoveryTime += 1000;
			}
			
			if (_hp > _maxHp)
			{
				_hp = _maxHp;
			}
		}
	}

	_bHit = false;
}