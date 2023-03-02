#pragma once
#include "CObject.h"

enum class EMonsterType
{
	NORMAL,
	BOSS,
	END
};

enum class EMonsterActionType
{
	ROAMING,
	RETURN,
	CHASING,
	ATTACK,
};

class CPlayer;

class CMonster : public CObject
{
private:
	friend class CGameContentsField;

	EMonsterType _eMonsterType;
	int _area;
	float _posX;
	float _posY;
	float _oldPosX;
	float _oldPosY;
	float _initialPosX;
	float _initialPosY;
	int _tileX;
	int _tileY;
	int _oldTileX;
	int _oldTileY;
	int _sectorX;
	int _sectorY;
	int _oldSectorX;
	int _oldSectorY;
	unsigned short _rotation;
	int _maxHp;
	int _hp;
	int _damage;
	BYTE _die;
	BYTE _respawn;

	bool _bAlive;
	bool _bMove;
	bool _bAttack;
	bool _bHasTarget;
	ULONGLONG _deadTime;   // 사망 시간
	int _respawnWaitTime;  // 리스폰 대기 시간

	EMonsterActionType _eActionType; // 현재 액션 타입
	float _viewRange;            // 시야 범위
	float _roamingRange;         // 로밍 범위
	float _roamingMoveDistance;  // 로밍 한번에 움직이는 거리
	float _chasingMoveDistance;  // 플레이어 쫓기 한번에 움직이는 거리
	float _maxMoveDistance;      // 이동가능한 최대 거리
	ULONGLONG _lastMoveTime;     // 마지막으로 이동한 시간
	DWORD _nextRoamingWaitTime;  // 다음 로밍까지 기다리는 시간
	CPlayer* _pTargetPlayer;     // 목표 플레이어

	float _attackRange;          // 공격 범위
	DWORD _attackDelay;          // 공격 딜레이
	ULONGLONG _lastAttackTime;   // 마지막으로 공격한 시간



public:
	CMonster();
	virtual ~CMonster();

public:
	void Init(EMonsterType type, int area, int tileX, int tileY, int hp, int damage);
	void SetRespawn(int tileX, int tileY);

	void Hit(int damage);                      // 피격됨
	void SetTargetPlayer(CPlayer* pPlayer);  // 플레이어를 타겟으로 지정함

	// @Override
	virtual void Update();

private:
	void MoveRandom();
	void MoveToInitialPosition();
	void MoveChasePlayer();
};