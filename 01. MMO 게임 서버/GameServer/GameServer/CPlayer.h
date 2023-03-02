#pragma once
#include "CObject.h"

namespace game_netserver
{
	class CPacket;
}

class CPlayer : public CObject
{
private:
	friend class CGameServer;
	friend class CGameContents;
	friend class CGameContentsAuth;
	friend class CGameContentsField;

	__int64 _sessionId;
	INT64	_accountNo;
	char	_sessionKey[64];	// 인증토큰

	wchar_t _szIP[16];
	unsigned short _port;

	bool _bLogin;                  // 로그인여부
	bool _bDisconnect;             // 연결끊김 여부
	ULONGLONG _lastHeartBeatTime;  // 마지막으로 heart beat 받은 시간

	WCHAR _userId[20];
	WCHAR _userNick[20];

	BYTE _characterType;
	float _posX;
	float _posY;
	float _oldPosX;
	float _oldPosY;
	int _tileX;
	int _tileY;
	int _oldTileX;
	int _oldTileY;
	int _sectorX;
	int _sectorY;
	int _oldSectorX;
	int _oldSectorY;
	unsigned short _rotation;
	int _crystal;
	int _oldCrystal;
	int _maxHp;
	int _hp;
	INT64 _exp;
	unsigned short _level;
	BYTE _die;
	BYTE _sit;
	BYTE _respawn;
	BYTE _VKey;
	BYTE _HKey;

	bool _bNewPlayer;              // 최초 접속한 계정 여부
	bool _bAlive;
	bool _bMove;
	bool _bAttack;
	bool _bHit; 
	int _damageAttack1;
	int _damageAttack2;

	int _amountSitHPRecovery;     // 앉기 상태일때 초당 회복되는 HP양
	int _sitStartHp;              // 앉기 시작했을 때의 HP
	ULONGLONG _sitStartTime;     // 앉기 시작한 시간
	ULONGLONG _lastSitHPRecoveryTime;  // 앉기로 인해 HP가 회복된 마지막 시간

	bool _bJustDied;      // 방금 죽음
	bool _bJustStandUp;   // 방금 일어남
	ULONGLONG _deadTime;  // 죽은 시간


	ULONGLONG _lastDBUpdateTime;    // 마지막으로 DB 업데이트를 한 시간

	std::vector<game_netserver::CPacket*> _vecPacketBuffer;

public:
	CPlayer();
	virtual ~CPlayer();

public:
	/* get */
	float GetPosX() { return _posX; }
	float GetPosY() { return _posY; }
	bool IsAlive() { return _bAlive; }

	/* init */
	void Init(__int64 sessionId);
	void SetAccountInfo(const char* userId, const char* userNick, const wchar_t* ip, unsigned short port);
	void SetCharacterInfo(BYTE characterType);
	void LoadCharacterInfo(BYTE characterType, float posX, float posY, int tileX, int tileY, USHORT rotation, int crystal, int hp, INT64 exp, USHORT level, BYTE die);

	/* set */
	void SetLogin(INT64 accountNo, char sessionKey[64]);
	void SetHeartBeatTime() { _lastHeartBeatTime = GetTickCount64(); }
	void SetSit();
	void SetRespawn(int tileX, int tileY);


	void AddCrystal(int amount);
	void Hit(int damage);


	// @Override
	virtual void Update();  
};

