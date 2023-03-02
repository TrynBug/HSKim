#include <Windows.h>
#include <iostream>
#include <math.h>
#include <vector>
#include "defineGameServer.h"
#include "CPlayer.h"
#include "CMonster.h"

#define M_PI  3.1415926535f
#define dfGET_SQUARE_DISTANCE(posX1, posY1, posX2, posY2)     (((posX1) - (posX2)) * ((posX1) - (posX2)) + ((posY1) - (posY2)) * ((posY1) - (posY2)))
#define dfGET_DISTANCE(posX1, posY1, posX2, posY2)        sqrt(((posX1) - (posX2)) * ((posX1) - (posX2)) + ((posY1) - (posY2)) * ((posY1) - (posY2)))

CMonster::CMonster()
{

}

CMonster::~CMonster()
{
}

void CMonster::Init(EMonsterType type, int area, int tileX, int tileY, int hp, int damage)
{
	CObject::Init();

	_eMonsterType = type;
	_area = area;
	_posX = dfFIELD_TILE_TO_POS(tileX);
	_posY = dfFIELD_TILE_TO_POS(tileY);
	_oldPosX = _posX;
	_oldPosY = _posY;
	_initialPosX = _posX;
	_initialPosY = _posY;
	_tileX = tileX;
	_tileY = tileY;
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_sectorX = dfFIELD_TILE_TO_SECTOR(tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(tileY);
	_oldSectorX = _sectorX;
	_oldSectorY = _sectorY;
	_rotation = 0;
	_maxHp = hp;
	_hp = hp;
	_damage = damage;
	_die = 0;
	_respawn = 0;

	_bAlive = true;
	_bMove = false;
	_bAttack = false;
	_bHasTarget = false;
	_deadTime = 0;
	_respawnWaitTime = 5000;

	if (type == EMonsterType::NORMAL)
	{
		_viewRange = 10.0f;
		_roamingRange = 4.0f;
		_roamingMoveDistance = 1.0f;
		_chasingMoveDistance = 0.5f;
		_maxMoveDistance = 10.0f;
	}
	if (type == EMonsterType::BOSS)
	{
		_viewRange = 10.0f;
		_roamingRange = 6.0f;
		_roamingMoveDistance = 2.0f;
		_chasingMoveDistance = 1.0f;
		_maxMoveDistance = 15.0f;
	}

	_eActionType = EMonsterActionType::ROAMING;
	_nextRoamingWaitTime = 5000;
	_lastMoveTime = GetTickCount64();
	_pTargetPlayer = nullptr;

	_attackRange = 0.6f;
	_attackDelay = 3000;
	_lastAttackTime = 0;

}


void CMonster::SetRespawn(int tileX, int tileY)
{
	Init(_eMonsterType, _area, tileX, tileY, _maxHp, _damage);
	_respawn = 1;
}


void CMonster::Hit(int damage)
{
	_hp -= damage;
	if (_hp <= 0)
	{
		_bAlive = false;
		_die = 1;
		_deadTime = GetTickCount64();
	}
}


void CMonster::SetTargetPlayer(CPlayer* pPlayer)
{
	if (_pTargetPlayer != nullptr)
		return;

	// 플레이어와 몬스터 사이의 거리 계산
	float distX = _posX - pPlayer->GetPosX();
	float distY = _posY - pPlayer->GetPosY();

	// 거리가 시야범위 내라면 플레이어를 타겟으로 지정
	if (distX * distX + distY * distY < _viewRange)
	{
		_pTargetPlayer = pPlayer;
		_bHasTarget = true;
		_eActionType = EMonsterActionType::CHASING;
	}
}


// @Override
void CMonster::Update()
{
	if (_bAlive == false)
		return;

	ULONGLONG currentTime = GetTickCount64();
	_bMove = false;
	_bAttack = false;

	// 로밍중일 경우
	if (_eActionType == EMonsterActionType::ROAMING)
	{
		// 마지막으로 이동한 시간에서 _nextRoamingWaitTime 밀리세컨드가 지났다면 움직임
		if (_lastMoveTime + _nextRoamingWaitTime < currentTime)
		{
			_bMove = true;  // 이동 플래그 set
			_lastMoveTime = currentTime;
			_nextRoamingWaitTime = rand() % 2001 + 4000;

			// 현재 위치와 초기 위치사이의 거리 계산
			float distX = _posX - _initialPosX;
			float distY = _posY - _initialPosY;
			if (distX * distX + distY * distY > _roamingRange * _roamingRange)
			{
				// 만약 현재 위치가 로밍 범위를 벗어났다면 초기위치로 움직임
				MoveToInitialPosition();
			}
			else
			{
				// 로밍 범위를 벗어나지 않았다면 랜덤 위치로 움직인다.
				MoveRandom();
			}
		}
	}
	// 플레이어를 쫓는 중일 경우
	else if (_eActionType == EMonsterActionType::CHASING)
	{
		// 플레이어가 죽었다면 타겟을 해제하고 액션 타입을 리턴으로 바꿈
		if (_pTargetPlayer->IsAlive() == false)
		{
			_bHasTarget = false;
			_pTargetPlayer = nullptr;
			_eActionType = EMonsterActionType::RETURN;
			_lastMoveTime = currentTime + 2000;
		}
		// 마지막으로 이동한 시간에서 1000 밀리세컨드가 지났다면 움직임
		else if (_lastMoveTime + 1000 < currentTime)
		{
			_bMove = true;  // 이동 플래그 set
			_lastMoveTime = currentTime;

			// 현재 위치와 초기 위치 사이의 거리 계산
			float distX = _posX - _initialPosX;
			float distY = _posY - _initialPosY;
			// 최대 이동거리보다 멀리왔다면 타겟을 해제하고 액션 타입을 리턴으로 변경
			if (distX * distX + distY * distY > _maxMoveDistance * _maxMoveDistance)
			{
				_bHasTarget = false;
				_pTargetPlayer = nullptr;
				_eActionType = EMonsterActionType::RETURN;
				MoveToInitialPosition();
			}
			// 그렇지 않다면 플레이어를 쫓음
			else
			{
				MoveChasePlayer();
				
				// 플레이어와의 거리가 _attackRange 이하라면 액션 타입을 공격으로 변경
				if (dfGET_SQUARE_DISTANCE(_posX, _posY, _pTargetPlayer->GetPosX(), _pTargetPlayer->GetPosY()) < _attackRange * _attackRange)
				{
					_eActionType = EMonsterActionType::ATTACK;
				}
			}
		}
	}
	// 처음위치로 돌아가는 중일 경우
	else if (_eActionType == EMonsterActionType::RETURN)
	{
		// 마지막으로 이동한 시간에서 1000 밀리세컨드가 지났다면 움직임
		if (_lastMoveTime + 1000 < currentTime)
		{
			_bMove = true;  // 이동 플래그 set
			_lastMoveTime = currentTime;

			// 초기 위치로 이동
			MoveToInitialPosition();

			// 현재 위치와 초기 위치 사이의 거리가 1.0 이하라면 액션타입을 로밍으로 변경
			float distX = _posX - _initialPosX;
			float distY = _posY - _initialPosY;
			if (distX * distX + distY * distY < 1.f)
			{
				_eActionType = EMonsterActionType::ROAMING;
			}
		}
	}
	// 공격 하려는 경우
	else if (_eActionType == EMonsterActionType::ATTACK)
	{
		// 플레이어가 죽었다면 타겟을 해제하고 액션 타입을 리턴으로 바꿈
		if (_pTargetPlayer->IsAlive() == false)
		{
			_bHasTarget = false;
			_pTargetPlayer = nullptr;
			_eActionType = EMonsterActionType::RETURN;
		}
		// 마지막으로 이동한 시간에서 1000 밀리세컨드가 지났고, 마지막으로 공격한 시간에서 _attackDelay 밀리세컨드가 지났다면 공격을 시도함
		else if (_lastMoveTime + 1000 < currentTime && _lastAttackTime + _attackDelay < currentTime)
		{
			// 플레이어와의 거리가 _attackRange 이상이라면 액션 타입을 쫓기로 변경
			if (dfGET_SQUARE_DISTANCE(_posX, _posY, _pTargetPlayer->GetPosX(), _pTargetPlayer->GetPosY()) > _attackRange * _attackRange)
			{
				_eActionType = EMonsterActionType::CHASING;
			}
			// 공격범위 내라면 공격
			else
			{
				_bAttack = true;  // 공격 플래그 set
				_lastAttackTime = currentTime;

				// 바라보는 방향을 플레이어쪽으로 설정
				float radian_direction = atan2(_pTargetPlayer->GetPosY() - _posY, _pTargetPlayer->GetPosX() - _posX);   // 몬스터 위치를 원점으로 했을 때 플레이어 위치의 각도 계산
				if (radian_direction < 0)
					radian_direction = 2.f * M_PI + radian_direction;
				int direction = (int)(radian_direction * 180.f / M_PI);
				_rotation = (360 - direction + 90) % 360;
			}
		}
	}
}




// 현재 위치에서 랜덤 거리만큼 움직인다
void CMonster::MoveRandom()
{
	int direction = rand() % 360;
	float radian_direction = (float)direction * M_PI / 180.f;
	_oldPosX = _posX;
	_oldPosY = _posY;
	_posX = _posX + cos(radian_direction) * _roamingMoveDistance;
	_posY = _posY + sin(radian_direction) * _roamingMoveDistance;
	_rotation = (360 - direction + 90) % 360;

	//printf("MoveRandom, dist:%.3f, init:(%.3f, %.3f), pos:(%.3f, %.3f) to (%.3f, %.3f), direction:%d, rotation:%d\n"
	//	, sqrt((_posX - _initialPosX) * (_posX - _initialPosX) + (_posY - _initialPosY) * (_posY - _initialPosY))
	//	, _initialPosX, _initialPosY, _oldPosX, _oldPosY, _posX, _posY, direction, _rotation);

	// 타일 좌표 업데이트
	_posX = min(max(0.f, _posX), dfFIELD_POS_MAX_X - 0.5f);
	_posY = min(max(0.f, _posY), dfFIELD_POS_MAX_Y - 0.5f);
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_tileX = dfFIELD_POS_TO_TILE(_posX);
	_tileY = dfFIELD_POS_TO_TILE(_posY);
	_oldSectorX = _sectorX;
	_oldSectorY = _sectorY;
	_sectorX = dfFIELD_TILE_TO_SECTOR(_tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(_tileY);
}


// 현재 위치에서 초기 위치로 움직인다.
void CMonster::MoveToInitialPosition()
{
	float distX = _posX - _initialPosX;
	float distY = _posY - _initialPosY;

	// 초기 위치를 원점으로 했을 때, 현재 위치와 초기 위치 사이의 라디안 계산
	float radian_direction = atan2(distY, distX);
	if (radian_direction < 0)
		radian_direction = 2.f * M_PI + radian_direction;

	// 현재 위치에서 초기 위치쪽으로 이동(라디안의 반대방향으로 이동)
	_oldPosX = _posX;
	_oldPosY = _posY;
	_posX = _posX - cos(radian_direction) * _roamingMoveDistance;
	_posY = _posY - sin(radian_direction) * _roamingMoveDistance;
	int direction = ((int)(radian_direction * 180.f / M_PI) + 180) % 360;
	_rotation = (360 - direction + 90) % 360;

	//printf("MoveToInitialPosition, dist:%.3f, init:(%.3f, %.3f), pos:(%.3f, %.3f) to (%.3f, %.3f), direction:%d, rotation:%d\n"
	//	, sqrt((distX* distX) + (distY * distY)), _initialPosX, _initialPosY, _oldPosX, _oldPosY, _posX, _posY, direction, _rotation);


	// 타일 좌표 업데이트
	_posX = min(max(0.f, _posX), dfFIELD_POS_MAX_X - 0.5f);
	_posY = min(max(0.f, _posY), dfFIELD_POS_MAX_Y - 0.5f);
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_tileX = dfFIELD_POS_TO_TILE(_posX);
	_tileY = dfFIELD_POS_TO_TILE(_posY);
	_oldSectorX = _sectorX;
	_oldSectorY = _sectorY;
	_sectorX = dfFIELD_TILE_TO_SECTOR(_tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(_tileY);
}



void CMonster::MoveChasePlayer()
{
	// 몬스터 위치를 원점으로 했을 때, 몬스터 위치와 플레이어 위치 사이의 라디안 계산
	float distX = _pTargetPlayer->GetPosX() - _posX;
	float distY = _pTargetPlayer->GetPosY() - _posY;
	float radian_direction = atan2(distY, distX);
	if (radian_direction < 0)
		radian_direction = 2.f * M_PI + radian_direction;

	// 현재 위치에서 플레이어 위치쪽으로 이동(라디안 방향으로 이동)
	_oldPosX = _posX;
	_oldPosY = _posY;
	_posX = _posX + cos(radian_direction) * _chasingMoveDistance;
	_posY = _posY + sin(radian_direction) * _chasingMoveDistance;
	int direction = (int)(radian_direction * 180.f / M_PI);
	_rotation = (360 - direction + 90) % 360;

	//printf("MoveChasePlayer, dist:%.3f, init:(%.3f, %.3f), pos:(%.3f, %.3f) to (%.3f, %.3f), direction:%d, rotation:%d\n"
	//	, distX * distX + distY * distY, _initialPosX, _initialPosY, _oldPosX, _oldPosY, _posX, _posY, direction, _rotation);


	// 타일 좌표 업데이트
	_posX = min(max(0.f, _posX), dfFIELD_POS_MAX_X - 0.5f);
	_posY = min(max(0.f, _posY), dfFIELD_POS_MAX_Y - 0.5f);
	_oldTileX = _tileX;
	_oldTileY = _tileY;
	_tileX = dfFIELD_POS_TO_TILE(_posX);
	_tileY = dfFIELD_POS_TO_TILE(_posY);
	_oldSectorX = _sectorX;
	_oldSectorY = _sectorY;
	_sectorX = dfFIELD_TILE_TO_SECTOR(_tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(_tileY);
}