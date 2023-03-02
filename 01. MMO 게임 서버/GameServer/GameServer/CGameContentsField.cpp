#include "CGameServer.h"
#include "CGameContents.h"
#include "CommonProtocol.h"

#include "CJsonParser.h"
#include "CDBAsyncWriter.h"
#include "CTile.h"
#include "CSector.h"
#include "logger.h"

//#define ENABLE_PROFILER
#include "profiler.h"

#include "CMonster.h"
#include "CCrystal.h"

#include "CGameContentsField.h"
#include <psapi.h>


#define FIELD_RANDOM_TILE_X(center_x, range_x)  min(max(center_x - range_x + (rand() % (range_x * 2)), 0), _tileMaxX - 1)
#define FIELD_RANDOM_TILE_Y(center_y, range_y)  min(max(center_y - range_y + (rand() % (range_y * 2)), 0), _tileMaxY - 1)
#define FIELD_PLAYER_RESPAWN_TILE_X             (85 + (rand() % 21))
#define FIELD_PLAYER_RESPAWN_TILE_Y             (93 + (rand() % 31))
#define FIELD_RANDOM_POS_X                      (_maxPosX / 32767.f * (float)rand())
#define FIELD_RANDOM_POS_Y                      (_maxPosY / 32767.f * (float)rand())

#define M_PI  3.1415926535f


CGameContentsField::CGameContentsField(EContentsThread myContentsType, CGameServer* pGameServer, int FPS)
	//: CGameContents(myContentsType, pGameServer, FPS, CONTENTS_MODE_SEND_PACKET_IMMEDIATELY)
	: CGameContents(myContentsType, pGameServer, FPS)
	, _sessionTransferLimit(1000000)
{

}

CGameContentsField::~CGameContentsField()
{
	_pDBConn->Close();
	delete _pDBConn;
}




/* Init */
void CGameContentsField::Init()
{
	// config 파일 json parser 얻기
	CJsonParser& configJsonParser = *GetConfigJsonParser();

	// _arrAreaInfo 배열에 area 정보를 입력한다.
	for (int i = 0; i < FIELD_NUMBER_OF_AREA; i++)
	{
		ReadAreaInfoFromConfigJson(configJsonParser, i);
	}

	// config 파일 읽기
	_numInitialCrystal = configJsonParser[L"ContentsField"][L"InitialCrystalNumber"].Int();      // 맵 상의 초기 크리스탈 수

	// 게임 멤버 설정
	_numRandomCrystalGeneration = 1;        // 필드에 초당 랜덤생성되는 크리스탈 수
	_maxRandomCrystalGeneration = 1000;     // 필드에 랜덤생성되는 크리스탈의 최대 수
	_tilePickRange = 2;          // 줍기 타일 범위
	_crystalLossWhenDie = 1000;  // 죽었을때 크리스탈 잃는 양


	_maxPosX = dfFIELD_POS_MAX_X;
	_maxPosY = dfFIELD_POS_MAX_Y;
	_tileMaxX = dfFIELD_TILE_MAX_X;
	_tileMaxY = dfFIELD_TILE_MAX_Y;
	_numTileXPerSector = dfFIELD_NUM_TILE_X_PER_SECTOR;
	_sectorMaxX = _tileMaxX / _numTileXPerSector;
	_sectorMaxY = _tileMaxY / _numTileXPerSector;
	_sectorViewRange = dfFIELD_SECTOR_VIEW_RANGE;
	_tileViewRange = _sectorViewRange * _numTileXPerSector + _numTileXPerSector / 2;
	
	
	


	// tile 배열 메모리 할당
	_tile = new CTile * [_tileMaxY];
	_tile[0] = (CTile*)malloc(sizeof(CTile) * _tileMaxY * _tileMaxX);
	for (int y = 0; y < _tileMaxY; y++)
		_tile[y] = _tile[0] + (y * _tileMaxX);

	// tile 생성자 호출
	for (int y = 0; y < _tileMaxY; y++)
	{
		for (int x = 0; x < _tileMaxX; x++)
		{
			new (&_tile[y][x]) CTile(x, y);
		}
	}

	// sector 배열 메모리 할당
	_sector = new CSector * [_sectorMaxY];
	_sector[0] = (CSector*)malloc(sizeof(CSector) * _sectorMaxY * _sectorMaxX);
	for (int y = 0; y < _sectorMaxY; y++)
		_sector[y] = _sector[0] + (y * _sectorMaxX);

	// sector 생성자 호출
	for (int y = 0; y < _sectorMaxY; y++)
	{
		for (int x = 0; x < _sectorMaxX; x++)
		{
			new (&_sector[y][x]) CSector(x, y);
		}
	}

	// sector의 주변 sector 등록(자신포함)
	// 반드시 y축 값이 작은 섹터가 우선으로, y축 값이 같다면 x축 값이 작은 섹터가 우선으로 등록되어야 함.
	for (int y = 0; y < _sectorMaxY; y++)
	{
		for (int x = 0; x < _sectorMaxX; x++)
		{
			for (int aroundY = y - _sectorViewRange; aroundY <= y + _sectorViewRange; aroundY++)
			{
				for (int aroundX = x - _sectorViewRange; aroundX <= x + _sectorViewRange; aroundX++)
				{
					if (aroundY < 0 || aroundY >= _sectorMaxY || aroundX < 0 || aroundX >= _sectorMaxX)
						continue;

					_sector[y][x].AddAroundSector(&_sector[aroundY][aroundX]);
				}
			}
		}
	}

	// pool 생성
	_poolCrystal = new CMemoryPool<CCrystal>(0, true, false);


	// DB 연결
	_pDBConn = new CDBAsyncWriter;
	_pDBConn->ConnectAndRunThread("127.0.0.1", "root", "vmfhzkepal!", "gamedb", 3306);


	// 몬스터 배열 생성
	_numMonster = 0;
	for (int i = 0; i < FIELD_NUMBER_OF_AREA; i++)
	{
		_numMonster += _arrAreaInfo[i].normalMonsterNumber + _arrAreaInfo[i].bossMonsterNumber;
	}
	_arrPtrMonster = new CMonster*[_numMonster];

	// 몬스터를 생성하고 배열과 타일에 입력한다
	CMonster* pMonster;
	int numMonster = 0;
	for (int area = 0; area < FIELD_NUMBER_OF_AREA; area++)
	{
		StAreaInfo& areaInfo = _arrAreaInfo[area];
		// 일반 몬스터 생성
		for (int i = 0; i < areaInfo.normalMonsterNumber; i++)
		{
			pMonster = new CMonster();
			pMonster->Init(EMonsterType::NORMAL, area
				, FIELD_RANDOM_TILE_X(areaInfo.x, areaInfo.range)
				, FIELD_RANDOM_TILE_Y(areaInfo.y, areaInfo.range)
				, areaInfo.normalMonsterHP
				, areaInfo.normalMonsterDamage
			);

			_arrPtrMonster[numMonster] = pMonster;
			_tile[pMonster->_tileY][pMonster->_tileX].AddObject(ETileObjectType::MONSTER, pMonster);
			_sector[pMonster->_sectorY][pMonster->_sectorX].AddObject(ESectorObjectType::MONSTER, pMonster);
			numMonster++;
		}
		// 보스 몬스터 생성
		for (int i = 0; i < areaInfo.bossMonsterNumber; i++)
		{
			pMonster = new CMonster();
			pMonster->Init(EMonsterType::BOSS, area
				, FIELD_RANDOM_TILE_X(areaInfo.x, areaInfo.range / 2)
				, FIELD_RANDOM_TILE_Y(areaInfo.y, areaInfo.range / 2)
				, areaInfo.bossMonsterHP
				, areaInfo.bossMonsterDamage
			);

			_arrPtrMonster[numMonster] = pMonster;
			_tile[pMonster->_tileY][pMonster->_tileX].AddObject(ETileObjectType::MONSTER, pMonster);
			_sector[pMonster->_sectorY][pMonster->_sectorX].AddObject(ESectorObjectType::MONSTER, pMonster);
			numMonster++;
		}
	}

	// 초기 크리스탈 생성
	CCrystal* pCrystal;
	for(int i=0; i<_numInitialCrystal; i++)
	{
		pCrystal = _poolCrystal->Alloc();
		pCrystal->Init(FIELD_RANDOM_POS_X, FIELD_RANDOM_POS_Y, rand() % 3 + 1);
		_tile[pCrystal->_tileY][pCrystal->_tileX].AddObject(ETileObjectType::CRYSTAL, pCrystal);
		_sector[pCrystal->_sectorY][pCrystal->_sectorX].AddObject(ESectorObjectType::CRYSTAL, pCrystal);
	}




	// utils
	for (int i = 0; i < 360; i++)
	{
		_cosine[i] = cos((float)i * M_PI / 180.f);
		_sine[i] = sin((float)i * M_PI / 180.f);
	}


}


/* Get */
int CGameContentsField::GetQueryRequestPoolSize() const { return _pDBConn->GetQueryRequestPoolSize(); }
int CGameContentsField::GetQueryRequestAllocCount() const { return _pDBConn->GetQueryRequestAllocCount(); }
int CGameContentsField::GetQueryRequestFreeCount() const { return _pDBConn->GetQueryRequestFreeCount(); }

/* virtual Get */
// @Override
__int64 CGameContentsField::GetQueryRunCount() const { return _pDBConn->GetQueryRunCount(); }
float CGameContentsField::GetMaxQueryRunTime() const { return _pDBConn->GetMaxQueryRunTime(); }
float CGameContentsField::GetMinQueryRunTime() const { return _pDBConn->GetMinQueryRunTime(); }
float CGameContentsField::GetAvgQueryRunTime() const { return _pDBConn->GetAvgQueryRunTime(); }
int CGameContentsField::GetRemainQueryCount() const { return _pDBConn->GetUnprocessedQueryCount(); }






/* 컨텐츠 클래스 callback 함수 구현 */

 // 컨텐츠 업데이트
void CGameContentsField::OnUpdate()
{
	// 모든 오브젝트 업데이트
	CCrystal* pCrystal;
	CMonster* pMonster;
	CPlayer* pPlayer;
	game_netserver::CPacket* pPacketMoveMonster;
	game_netserver::CPacket* pPacketMonsterDie;
	game_netserver::CPacket* pPacketCreateCrystal;
	game_netserver::CPacket* pPacketRespawnMonster;
	game_netserver::CPacket* pPacketPlayerHp;
	game_netserver::CPacket* pPacketPlayerDie;
	ULONGLONG currentTime = GetTickCount64();
	for (int y = 0; y < _sectorMaxY; y++)
	{
		for (int x = 0; x < _sectorMaxX; x++)
		{
			// 크리스탈 업데이트
			std::vector<CObject*>& vecCrystal = _sector[y][x].GetObjectVector(ESectorObjectType::CRYSTAL);
			for (int iCnt = 0; iCnt < vecCrystal.size();)
			{
				pCrystal = (CCrystal*)vecCrystal[iCnt];
				pCrystal->Update();

				// 삭제된 크리스탈을 타일과 섹터에서 제거
				if (pCrystal->_bAlive == false)
				{
					_tile[pCrystal->_tileY][pCrystal->_tileX].RemoveObject(ETileObjectType::CRYSTAL, pCrystal);
					_sector[y][x].RemoveObject(ESectorObjectType::CRYSTAL, pCrystal);
					_poolCrystal->Free(pCrystal);
					continue;
				}

				iCnt++;
			}

			// 섹터내의 몬스터 업데이트
			std::vector<CObject*>& vecMonster = _sector[y][x].GetObjectVector(ESectorObjectType::MONSTER);
			for (int iCnt = 0; iCnt < vecMonster.size();)
			{
				pMonster = (CMonster*)vecMonster[iCnt];
				pMonster->Update();

				// 몬스터가 살아있을 경우
				if (pMonster->_bAlive == true)
				{
					// 타겟이 없을 경우 주변에서 찾음
					if (pMonster->_bHasTarget == false)
					{
						MonsterFindTarget(pMonster);
					}

					// 몬스터가 공격했을 경우
					if (pMonster->_bAttack == true)
					{
						// 몬스터 공격판정 처리
						MonsterAttack(pMonster);
					}
				}
				// 몬스터가 사망했을경우
				else if (pMonster->_bAlive == false)
				{
					// 몬스터 사망 패킷 전송
					pPacketMonsterDie = AllocPacket();
					Msg_MonsterDie(pPacketMonsterDie, pMonster);
					SendAroundSector(pMonster->_sectorX, pMonster->_sectorY, pPacketMonsterDie, nullptr);
					pPacketMonsterDie->SubUseCount();
					
					// 크리스탈 생성
					pCrystal = _poolCrystal->Alloc();
					pCrystal->Init(pMonster->_posX, pMonster->_posY, rand() % 3);
					_tile[pCrystal->_tileY][pCrystal->_tileX].AddObject(ETileObjectType::CRYSTAL, pCrystal);
					_sector[y][x].AddObject(ESectorObjectType::CRYSTAL, pCrystal);

					// 크리스탈 생성 패킷 전송
					pPacketCreateCrystal = AllocPacket();
					Msg_CreateCrystal(pPacketCreateCrystal, pCrystal);
					SendAroundSector(pCrystal->_sectorX, pCrystal->_sectorY, pPacketCreateCrystal, nullptr);
					pPacketCreateCrystal->SubUseCount();

					// 몬스터를 타일과 섹터에서 제거
					_tile[pMonster->_tileY][pMonster->_tileX].RemoveObject(ETileObjectType::MONSTER, pMonster);
					_sector[y][x].RemoveObject(ESectorObjectType::MONSTER, pMonster);

					LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnUpdate. monster dead. clientId:%lld, tile:(%d, %d), hp:%d\n", pMonster->_clientId, pMonster->_tileX, pMonster->_tileY, pMonster->_hp);
					continue;
				}

				iCnt++;
			}

			// 플레이어 업데이트
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int iCnt = 0; iCnt < vecPlayer.size(); iCnt++)
			{
				pPlayer = (CPlayer*)vecPlayer[iCnt];
				pPlayer->Update();

				if (pPlayer->_bAlive)
				{
					// 플레이어가 방금 일어났을 경우 HP 패킷 전송
					if (pPlayer->_bJustStandUp == true)
					{
						pPacketPlayerHp = AllocPacket();
						Msg_PlayerHP(pPacketPlayerHp, pPlayer);
						SendUnicast(pPlayer, pPacketPlayerHp);
						pPacketPlayerHp->SubUseCount();

						DB_PlayerRenewHP(pPlayer);
					}
				}

				// 방금 죽었을경우
				if (pPlayer->_bJustDied == true)
				{
					pPlayer->AddCrystal(-_crystalLossWhenDie);

					// 플레이어 사망패킷 전송
					pPacketPlayerDie = AllocPacket();
					Msg_PlayerDie(pPacketPlayerDie, pPlayer, _crystalLossWhenDie);
					SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketPlayerDie, nullptr);
					pPacketPlayerDie->SubUseCount();

					// DB에 저장
					DB_PlayerDie(pPlayer);

					LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnUpdate. player die. sessionId:%lld, accountNo:%lld, hp:%d\n", pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_hp);
				}
			}
		}
	}



	// 타일에 없는 몬스터를 포함하여 전체 몬스터 체크 (몬스터의 타일이동이 필요한 로직을 여기서 처리)
	for (int iCnt = 0; iCnt < _numMonster; iCnt++)
	{
		pMonster = _arrPtrMonster[iCnt];

		// 몬스터가 살아있음
		if (pMonster->_bAlive == true)
		{
			// 몬스터가 이동했을 경우
			if (pMonster->_bMove == true)
			{
				// 타일 및 섹터 이동
				MoveMonsterTileAndSector(pMonster);

				// 몬스터 이동 패킷 전송
				pPacketMoveMonster = AllocPacket();
				Msg_MoveMonster(pPacketMoveMonster, pMonster);
				SendAroundSector(pMonster->_sectorX, pMonster->_sectorY, pPacketMoveMonster, nullptr);
				pPacketMoveMonster->SubUseCount();
			}
		}
		// 몬스터가 죽어있음
		else if (pMonster->_bAlive == false)
		{
			// 부활 시간만큼 대기했으면 부활
			if (pMonster->_deadTime + pMonster->_respawnWaitTime < currentTime)
			{
				// 부활 및 타일에 입력
				if (pMonster->_eMonsterType == EMonsterType::NORMAL)
				{
					pMonster->SetRespawn(FIELD_RANDOM_TILE_X(_arrAreaInfo[pMonster->_area].x, _arrAreaInfo[pMonster->_area].range)
						, FIELD_RANDOM_TILE_Y(_arrAreaInfo[pMonster->_area].y, _arrAreaInfo[pMonster->_area].range));
				}
				else
				{
					pMonster->SetRespawn(FIELD_RANDOM_TILE_X(_arrAreaInfo[pMonster->_area].x, _arrAreaInfo[pMonster->_area].range / 2)
						, FIELD_RANDOM_TILE_Y(_arrAreaInfo[pMonster->_area].y, _arrAreaInfo[pMonster->_area].range / 2));
				}

				_tile[pMonster->_tileY][pMonster->_tileX].AddObject(ETileObjectType::MONSTER, pMonster);
				_sector[pMonster->_sectorY][pMonster->_sectorX].AddObject(ESectorObjectType::MONSTER, pMonster);

				// 몬스터 리스폰 패킷 전송
				pPacketRespawnMonster = AllocPacket();
				Msg_CreateMonsterCharacter(pPacketRespawnMonster, pMonster, 1);
				SendAroundSector(pMonster->_sectorX, pMonster->_sectorY, pPacketRespawnMonster, nullptr);
				pPacketRespawnMonster->SubUseCount();

				LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnUpdate. monster respawn. clientId:%lld, tile:(%d, %d)\n", pMonster->_clientId, pMonster->_tileX, pMonster->_tileY);
			}
		}
	}



}


// 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
void CGameContentsField::OnRecv(__int64 sessionId, game_netserver::CPacket* pPacket)
{
	// 세션ID로 플레이어 객체를 얻는다.
	auto iter = _mapPlayer.find(sessionId);
	CPlayer* pPlayer = iter->second;

	// 메시지 타입 확인
	WORD msgType;
	*pPacket >> msgType;

	game_netserver::CPacket* pSendPacket = AllocPacket();

	// 메시지 타입에 따른 처리
	if (msgType < 2000)
	{
		switch (msgType)
		{
			// 캐릭터 이동 요청
		case en_PACKET_CS_GAME_REQ_MOVE_CHARACTER:
		{
			PacketProc_MoveCharacter(pPacket, pPlayer);
			break;
		}

		// 캐릭터 정지 요청
		case en_PACKET_CS_GAME_REQ_STOP_CHARACTER:
		{
			PacketProc_StopCharacter(pPacket, pPlayer);
			break;
		}

		// 캐릭터 공격 요청 처리1
		case en_PACKET_CS_GAME_REQ_ATTACK1:
		{
			PacketProc_Attack1(pPacket, pPlayer);
			break;
		}

		// 캐릭터 공격 요청 처리2
		case en_PACKET_CS_GAME_REQ_ATTACK2:
		{
			PacketProc_Attack2(pPacket, pPlayer);
			break;
		}

		// 줍기 요청
		case en_PACKET_CS_GAME_REQ_PICK:
		{
			PacketProc_Pick(pPacket, pPlayer);
			break;
		}

		// 바닥에 앉기 요청
		case en_PACKET_CS_GAME_REQ_SIT:
		{
			PacketProc_Sit(pPacket, pPlayer);
			break;
		}

		case en_PACKET_CS_GAME_REQ_PLAYER_RESTART:
		{
			PacketProc_PlayerRestart(pPacket, pPlayer);
			break;
		}
		// 잘못된 메시지를 받은 경우
		default:
		{
			// 세션의 연결을 끊는다. 연결을 끊으면 더이상 세션이 detach되어 더이상 메시지가 처리되지 않음
			DisconnectSession(pPlayer->_sessionId);
			_disconnByInvalidMessageType++; // 모니터링

			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::DEFAULT. sessionId:%lld, accountNo:%lld, msg type:%d\n", pPlayer->_sessionId, pPlayer->_accountNo, msgType);
			break;
		}
		}
	}
	else
	{
		switch (msgType)
		{
		// 테스트 에코 요청
		case en_PACKET_CS_GAME_REQ_ECHO:
		{
			PacketProc_Echo(pPacket, pPlayer);
			break;
		}


		// 하트비트
		case en_PACKET_CS_GAME_REQ_HEARTBEAT:
		{
			pPlayer->SetHeartBeatTime();
			break;
		}
		// 잘못된 메시지를 받은 경우
		default:
		{
			// 세션의 연결을 끊는다. 연결을 끊으면 더이상 세션이 detach되어 더이상 메시지가 처리되지 않음
			DisconnectSession(pPlayer->_sessionId);
			_disconnByInvalidMessageType++; // 모니터링

			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::DEFAULT. sessionId:%lld, accountNo:%lld, msg type:%d\n", pPlayer->_sessionId, pPlayer->_accountNo, msgType);
			break;
		}
		}
	}

	pSendPacket->SubUseCount();

}




// 스레드에 세션이 들어왔을 때 호출됨
void CGameContentsField::OnSessionJoin(__int64 sessionId, PVOID data)
{
	PROFILE_BEGIN("CGameContentsField::OnSessionJoin");

	// 파라미터에서 플레이어 객체를 얻는다.
	CPlayer* pPlayer = (CPlayer*)data;
	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnSessionJoin::MoveJoin. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);

	// 플레이어를 map에 등록한다.
	_mapPlayer.insert(std::make_pair(sessionId, pPlayer));

	// 만약 최초 접속한 유저일 경우 플레이어 데이터를 초기화하고 DB에 입력한다.
	if (pPlayer->_bNewPlayer == true)
	{
		int tileX = FIELD_PLAYER_RESPAWN_TILE_X;
		int tileY = FIELD_PLAYER_RESPAWN_TILE_Y;
		pPlayer->LoadCharacterInfo(pPlayer->_characterType, dfFIELD_TILE_TO_POS(tileX), dfFIELD_TILE_TO_POS(tileY), tileX, tileY, 0, 0, 5000, 0, 1, 0);

		// 플레이어 정보 DB에 저장
		DB_InsertPlayerInfo(pPlayer);
	}

	// DB에 로그인 로그 저장
	DB_Login(pPlayer);

	// 플레이어가 죽어있을 경우 위치를 리스폰위치로 지정한다.
	if (pPlayer->_die == 1)
	{
		int tileX = FIELD_PLAYER_RESPAWN_TILE_X;
		int tileY = FIELD_PLAYER_RESPAWN_TILE_Y;
		pPlayer->SetRespawn(tileX, tileY);
		DB_PlayerRespawn(pPlayer);
	}

	pPlayer->_sectorX = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileX);
	pPlayer->_sectorY = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileY);
	pPlayer->_oldSectorX = pPlayer->_sectorX;
	pPlayer->_oldSectorY = pPlayer->_sectorY;

	// 플레이어를 섹터와 타일에 등록하고, 캐릭터를 생성하고, 주변 오브젝트들을 로드한다.
	InitializePlayerEnvironment(pPlayer);

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnSessionJoin. player join. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);
}


// 스레드 내에서 세션의 연결이 끊겼을때 호출됨
void CGameContentsField::OnSessionDisconnected(__int64 sessionId)
{
	PROFILE_BEGIN("CGameContentsField::OnSessionDisconnected");

	// 플레이어 객체 얻기
	auto iter = _mapPlayer.find(sessionId);
	if (iter == _mapPlayer.end())
	{
		int* p = 0;
		*p = 0;
		return;
	}
		

	CPlayer* pPlayer = iter->second;
	pPlayer->_bAlive = false;

	// 플레이어를 섹터와 타일에서 제거하고 주변에 캐릭터 삭제 패킷을 보냄
	_tile[pPlayer->_tileY][pPlayer->_tileX].RemoveObject(ETileObjectType::PLAYER, pPlayer);
	_sector[pPlayer->_sectorY][pPlayer->_sectorX].RemoveObject(ESectorObjectType::PLAYER, pPlayer);

	game_netserver::CPacket* pPacketDeleteCharacter = AllocPacket();
	Msg_RemoveObject(pPacketDeleteCharacter, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketDeleteCharacter, pPlayer);
	pPacketDeleteCharacter->SubUseCount();


	// 현재 플레이어 정보를 DB에 저장
	DB_Logout(pPlayer);


	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnSessionDisconnected. player leave. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);

	// 플레이어를 map에서 삭제하고 free
	_mapPlayer.erase(iter);
	FreePlayer(pPlayer);
}




/* 플레이어 */

// 플레이어를 타일과 섹터에 등록하고, 캐릭터를 생성하고, 주변 오브젝트들을 로드한다. 플레이어 접속, 리스폰 시에 호출한다.
void CGameContentsField::InitializePlayerEnvironment(CPlayer* pPlayer)
{

	// 플레이어를 타일과 섹터에 등록한다.
	_tile[pPlayer->_tileY][pPlayer->_tileX].AddObject(ETileObjectType::PLAYER, pPlayer);
	_sector[pPlayer->_sectorY][pPlayer->_sectorX].AddObject(ESectorObjectType::PLAYER, pPlayer);
	

	// 내 캐릭터 생성 패킷 전송
	game_netserver::CPacket* pPacketCreateMyChr = AllocPacket();
	Msg_CreateMyCharacter(pPacketCreateMyChr, pPlayer);
	SendUnicast(pPlayer, pPacketCreateMyChr);
	pPacketCreateMyChr->SubUseCount();

	// 내 주변의 오브젝트 생성 패킷 전송
	game_netserver::CPacket* pPacketCreateOtherChr;
	game_netserver::CPacket* pPacketMoveOtherChr;
	game_netserver::CPacket* pPacketCreateMonster;
	game_netserver::CPacket* pPacketMoveMonster;
	game_netserver::CPacket* pPacketCreateCrystal;
	CPlayer* pPlayerOther;
	CMonster* pMonster;
	CCrystal* pCrystal;
	int minY = max(pPlayer->_sectorY - _sectorViewRange, 0);
	int maxY = min(pPlayer->_sectorY + _sectorViewRange, dfFIELD_SECTOR_MAX_Y - 1);
	int minX = max(pPlayer->_sectorX - _sectorViewRange, 0);
	int maxX = min(pPlayer->_sectorX + _sectorViewRange, dfFIELD_SECTOR_MAX_X - 1);
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];
				if (pPlayerOther == pPlayer)
					continue;

				// 주변의 다른 캐릭터 생성 패킷 전송
				pPacketCreateOtherChr = AllocPacket();
				Msg_CreateOtherCharacter(pPacketCreateOtherChr, pPlayerOther);
				SendUnicast(pPlayer, pPacketCreateOtherChr);
				pPacketCreateOtherChr->SubUseCount();

				// 다른 캐릭터가 이동중일경우 이동 패킷 전송
				if (pPlayerOther->_bMove == true)
				{
					pPacketMoveOtherChr = AllocPacket();
					Msg_MoveCharacter(pPacketMoveOtherChr, pPlayerOther);
					SendUnicast(pPlayer, pPacketMoveOtherChr);
					pPacketMoveOtherChr->SubUseCount();
				}
			}

			std::vector<CObject*>& vecMonster = _sector[y][x].GetObjectVector(ESectorObjectType::MONSTER);
			for (int i = 0; i < vecMonster.size(); i++)
			{
				pMonster = (CMonster*)vecMonster[i];
				if (pMonster->_bAlive == false)
					continue;

				// 나에게 몬스터 생성 패킷 전송
				pPacketCreateMonster = AllocPacket();
				Msg_CreateMonsterCharacter(pPacketCreateMonster, pMonster, 0);
				SendUnicast(pPlayer, pPacketCreateMonster);
				pPacketCreateMonster->SubUseCount();

				// 몬스터가 움직이고 있을 경우 나에게 몬스터 이동 패킷 전송
				if (pMonster->_bMove)
				{
					pPacketMoveMonster = AllocPacket();
					Msg_MoveMonster(pPacketMoveMonster, pMonster);
					SendUnicast(pPlayer, pPacketMoveMonster);
					pPacketMoveMonster->SubUseCount();
				}
			}

			std::vector<CObject*>& vecCrystal = _sector[y][x].GetObjectVector(ESectorObjectType::CRYSTAL);
			for (int i = 0; i < vecCrystal.size(); i++)
			{
				pCrystal = (CCrystal*)vecCrystal[i];

				// 나에게 크리스탈 생성 패킷 전송
				pPacketCreateCrystal = AllocPacket();
				Msg_CreateCrystal(pPacketCreateCrystal, pCrystal);
				SendUnicast(pPlayer, pPacketCreateCrystal);
				pPacketCreateCrystal->SubUseCount();
			}
		}
	}

	// 내 캐릭터를 주변 플레이어들에게 전송
	game_netserver::CPacket* pPacketCreateMyChrToOther;
	pPacketCreateMyChrToOther = AllocPacket();
	Msg_CreateOtherCharacter(pPacketCreateMyChrToOther, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketCreateMyChrToOther, pPlayer);
	pPacketCreateMyChrToOther->SubUseCount();
}




// 플레이어를 이전 타일, 섹터에서 현재 타일, 섹터로 옮긴다.
void CGameContentsField::MovePlayerTileAndSector(CPlayer* pPlayer)
{
	// 타일이 달라졌으면 타일 이동
	if (pPlayer->_tileY != pPlayer->_oldTileY || pPlayer->_tileX != pPlayer->_oldTileX)
	{
		_tile[pPlayer->_oldTileY][pPlayer->_oldTileX].RemoveObject(ETileObjectType::PLAYER, pPlayer);
		_tile[pPlayer->_tileY][pPlayer->_tileX].AddObject(ETileObjectType::PLAYER, pPlayer);
	}

	// 섹터가 같다면 더이상 할일은 없다
	if (pPlayer->_sectorY == pPlayer->_oldSectorY && pPlayer->_sectorX == pPlayer->_oldSectorX)
	{
		return;
	}

	PROFILE_BEGIN("CGameContentsField::MovePlayerTileAndSector");

	// 섹터 이동
	_sector[pPlayer->_oldSectorY][pPlayer->_oldSectorX].RemoveObject(ESectorObjectType::PLAYER, pPlayer);
	_sector[pPlayer->_sectorY][pPlayer->_sectorX].AddObject(ESectorObjectType::PLAYER, pPlayer);

	// 이전 섹터 기준의 X범위, Y범위 구하기
	int oldXLeft = max(pPlayer->_oldSectorX - _sectorViewRange, 0);
	int oldXRight = min(pPlayer->_oldSectorX + _sectorViewRange, _sectorMaxX - 1);
	int oldYDown = max(pPlayer->_oldSectorY - _sectorViewRange, 0);
	int oldYUp = min(pPlayer->_oldSectorY + _sectorViewRange, _sectorMaxY - 1);

	// 현재 섹터 기준의 X범위, Y범위 구하기
	int newXLeft = max(pPlayer->_sectorX - _sectorViewRange, 0);
	int newXRight = min(pPlayer->_sectorX + _sectorViewRange, _sectorMaxX - 1);
	int newYDown = max(pPlayer->_sectorY - _sectorViewRange, 0);
	int newYUp = min(pPlayer->_sectorY + _sectorViewRange, _sectorMaxY - 1);

	CPlayer* pPlayerOther;
	CMonster* pMonster;
	CCrystal* pCrystal;
	
	// 범위에 새로 포함된 섹터에 대한 처리
	game_netserver::CPacket* pPacketCreateMyChr = AllocPacket();
	Msg_CreateOtherCharacter(pPacketCreateMyChr, pPlayer);

	game_netserver::CPacket* pPacketCreateOtherChr;
	game_netserver::CPacket* pPacketMoveOtherChr;
	game_netserver::CPacket* pPacketCreateMonster;
	game_netserver::CPacket* pPacketMoveMonster;
	game_netserver::CPacket* pPacketCreateCrystal;
	for (int y = newYDown; y <= newYUp; y++)
	{
		for (int x = newXLeft; x <= newXRight; x++)
		{
			if (y <= oldYUp && y >= oldYDown && x <= oldXRight && x >= oldXLeft)
				continue;

			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];
				if (pPlayerOther == pPlayer)
					continue;

				// 내 캐릭터를 상대에게 전송
				SendUnicast(pPlayerOther, pPacketCreateMyChr);

				// 상대 캐릭터를 나에게 전송
				pPacketCreateOtherChr = AllocPacket();
				Msg_CreateOtherCharacter(pPacketCreateOtherChr, pPlayerOther);
				SendUnicast(pPlayer, pPacketCreateOtherChr);
				pPacketCreateOtherChr->SubUseCount();

				// 상대 캐릭터가 움직이고 있을 경우 나에게 캐릭터 이동 패킷 전송
				if (pPlayerOther->_bMove)
				{
					pPacketMoveOtherChr = AllocPacket();
					Msg_MoveCharacter(pPacketMoveOtherChr, pPlayerOther);
					SendUnicast(pPlayer, pPacketMoveOtherChr);
					pPacketMoveOtherChr->SubUseCount();
				}
			}

			std::vector<CObject*>& vecMonster = _sector[y][x].GetObjectVector(ESectorObjectType::MONSTER);
			for (int i = 0; i < vecMonster.size(); i++)
			{
				pMonster = (CMonster*)vecMonster[i];
				if (pMonster->_bAlive == false)
					continue;

				// 나에게 몬스터 생성 패킷 전송
				pPacketCreateMonster = AllocPacket();
				Msg_CreateMonsterCharacter(pPacketCreateMonster, pMonster, 0);
				SendUnicast(pPlayer, pPacketCreateMonster);
				pPacketCreateMonster->SubUseCount();

				// 몬스터가 움직이고 있을 경우 나에게 몬스터 이동 패킷 전송
				if (pMonster->_bMove)
				{
					pPacketMoveMonster = AllocPacket();
					Msg_MoveMonster(pPacketMoveMonster, pMonster);
					SendUnicast(pPlayer, pPacketMoveMonster);
					pPacketMoveMonster->SubUseCount();
				}
			}

			std::vector<CObject*>& vecCrystal = _sector[y][x].GetObjectVector(ESectorObjectType::CRYSTAL);
			for (int i = 0; i < vecCrystal.size(); i++)
			{
				pCrystal = (CCrystal*)vecCrystal[i];

				// 나에게 크리스탈 생성 패킷 전송
				pPacketCreateCrystal = AllocPacket();
				Msg_CreateCrystal(pPacketCreateCrystal, pCrystal);
				SendUnicast(pPlayer, pPacketCreateCrystal);
				pPacketCreateCrystal->SubUseCount();
			}

		}
	}
	pPacketCreateMyChr->SubUseCount();





	// 범위에서 제외된 타일에 대한 처리
	game_netserver::CPacket* pPacketDeleteMyChr = AllocPacket();
	game_netserver::CPacket* pPacketDeleteOtherChr;
	game_netserver::CPacket* pPacketDeleteMonster;
	game_netserver::CPacket* pPacketDeleteCrystal;
	Msg_RemoveObject(pPacketDeleteMyChr, pPlayer);
	for (int y = oldYDown; y <= oldYUp; y++)
	{
		for (int x = oldXLeft; x <= oldXRight; x++)
		{
			if (y <= newYUp && y >= newYDown && x <= newXRight && x >= newXLeft)
				continue;

			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];
				
				// 내 캐릭터 삭제패킷을 상대에게 전송
				SendUnicast(pPlayerOther, pPacketDeleteMyChr);

				// 상대 캐릭터 삭제패킷을 나에게 전송
				pPacketDeleteOtherChr = AllocPacket();
				Msg_RemoveObject(pPacketDeleteOtherChr, pPlayerOther);
				SendUnicast(pPlayer, pPacketDeleteOtherChr);
				pPacketDeleteOtherChr->SubUseCount();
			}

			std::vector<CObject*>& vecMonster = _sector[y][x].GetObjectVector(ESectorObjectType::MONSTER);
			for (int i = 0; i < vecMonster.size(); i++)
			{
				pMonster = (CMonster*)vecMonster[i];
				if (pMonster->_bAlive == false)
					continue;

				// 나에게 몬스터 삭제 패킷 전송
				pPacketDeleteMonster = AllocPacket();
				Msg_RemoveObject(pPacketDeleteMonster, pMonster);
				SendUnicast(pPlayer, pPacketDeleteMonster);
				pPacketDeleteMonster->SubUseCount();
			}

			std::vector<CObject*>& vecCrystal = _sector[y][x].GetObjectVector(ESectorObjectType::CRYSTAL);
			for (int i = 0; i < vecCrystal.size(); i++)
			{
				pCrystal = (CCrystal*)vecCrystal[i];

				// 나에게 크리스탈 삭제 패킷 전송
				pPacketDeleteCrystal = AllocPacket();
				Msg_RemoveObject(pPacketDeleteCrystal, pCrystal);
				SendUnicast(pPlayer, pPacketDeleteCrystal);
				pPacketDeleteCrystal->SubUseCount();
			}
		}
	}
	pPacketDeleteMyChr->SubUseCount();
}








/* 몬스터 */

// 몬스터를 이전 타일, 섹터에서 현재 타일, 섹터로 옮긴다.
void CGameContentsField::MoveMonsterTileAndSector(CMonster* pMonster)
{
	// 타일이 다르다면 타일이동
	if (pMonster->_oldTileX != pMonster->_tileX || pMonster->_oldTileY != pMonster->_tileY)
	{
		_tile[pMonster->_oldTileY][pMonster->_oldTileX].RemoveObject(ETileObjectType::MONSTER, pMonster);
		_tile[pMonster->_tileY][pMonster->_tileX].AddObject(ETileObjectType::MONSTER, pMonster);
	}

	// 섹터가 같다면 더이상 할일은 없음
	if (pMonster->_oldSectorX == pMonster->_sectorX && pMonster->_oldSectorY == pMonster->_sectorY)
	{
		return;
	}

	PROFILE_BEGIN("CGameContentsField::MoveMonsterTileAndSector");

	// 섹터 이동
	_sector[pMonster->_oldSectorY][pMonster->_oldSectorX].RemoveObject(ESectorObjectType::MONSTER, pMonster);
	_sector[pMonster->_sectorY][pMonster->_sectorX].AddObject(ESectorObjectType::MONSTER, pMonster);

	// 이전 섹터 기준의 X범위, Y범위 구하기
	int oldXLeft = max(pMonster->_oldSectorX - _sectorViewRange, 0);
	int oldXRight = min(pMonster->_oldSectorX + _sectorViewRange, _sectorMaxX - 1);
	int oldYDown = max(pMonster->_oldSectorY - _sectorViewRange, 0);
	int oldYUp = min(pMonster->_oldSectorY + _sectorViewRange, _sectorMaxY - 1);

	// 현재 섹터 기준의 X범위, Y범위 구하기
	int newXLeft = max(pMonster->_sectorX - _sectorViewRange, 0);
	int newXRight = min(pMonster->_sectorX + _sectorViewRange, _sectorMaxX - 1);
	int newYDown = max(pMonster->_sectorY - _sectorViewRange, 0);
	int newYUp = min(pMonster->_sectorY + _sectorViewRange, _sectorMaxY - 1);


	CPlayer* pPlayerOther;

	// 범위에 새로 포함된 섹터에 대한 처리
	game_netserver::CPacket* pPacketCreateMonster = AllocPacket();
	Msg_CreateMonsterCharacter(pPacketCreateMonster, pMonster, 0);
	for (int y = newYDown; y <= newYUp; y++)
	{
		for (int x = newXLeft; x <= newXRight; x++)
		{
			if (y <= oldYUp && y >= oldYDown && x <= oldXRight && x >= oldXLeft)
				continue;

			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];

				// 플레이어에게 몬스터 생성 패킷 전송
				SendUnicast(pPlayerOther, pPacketCreateMonster);
			}
		}
	}
	pPacketCreateMonster->SubUseCount();


	// 범위에서 제외된 타일에 대한 처리
	game_netserver::CPacket* pPacketDeleteMonster = AllocPacket();
	Msg_RemoveObject(pPacketDeleteMonster, pMonster);
	for (int y = oldYDown; y <= oldYUp; y++)
	{
		for (int x = oldXLeft; x <= oldXRight; x++)
		{
			if (y <= newYUp && y >= newYDown && x <= newXRight && x >= newXLeft)
				continue;

			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];

				// 몬스터 삭제 패킷을 플레이어에게 전송
				SendUnicast(pPlayerOther, pPacketDeleteMonster);
			}
		}
	}
	pPacketDeleteMonster->SubUseCount();
}


// 몬스터가 공격함
void CGameContentsField::MonsterAttack(CMonster* pMonster)
{
	if (pMonster->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::MonsterAttack");

	
	// 주변에 몬스터 이동 패킷을 전송하여 방향을 바꾼다. 그리고 주변에 공격 패킷 전송
	game_netserver::CPacket* pPacketMoveMonster = AllocPacket();    // 이동 패킷
	Msg_MoveMonster(pPacketMoveMonster, pMonster);
	SendAroundSector(pMonster->_sectorX, pMonster->_sectorY, pPacketMoveMonster, nullptr);
	pPacketMoveMonster->SubUseCount();

	game_netserver::CPacket* pPacketMonsterAttack = AllocPacket();  // 공격 패킷
	Msg_MonsterAttack(pPacketMonsterAttack, pMonster);
	SendAroundSector(pMonster->_sectorX, pMonster->_sectorY, pPacketMonsterAttack, nullptr);
	pPacketMonsterAttack->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::MonsterAttack. monster attack. id:%lld, tile:(%d, %d), pos:(%.3f, %.3f), rotation:%d\n"
		, pMonster->_clientId, pMonster->_tileX, pMonster->_tileY, pMonster->_posX, pMonster->_posY, pMonster->_rotation);

	float monsterPosX = pMonster->_posX;
	float monsterPosY = pMonster->_posY;
	int degree = (360 - pMonster->_rotation + 90) % 360;  // rotation을 360도 각도로 변환

	// 점을 0도 방향으로 회전시키기 위한 cos, sin값
	float R[2] = { GetCosine(360 - degree), GetSine(360 - degree) };


	// 공격 범위에 해당하는 타일 조사하기
	// 공격범위 타일: 몬스터 타일 기준 상하좌우 +2칸
	// 공격범위 좌표: 몬스터가 바라보는 방향으로 지름 _attackRange 인 반원
	CPlayer* pPlayerOther;
	game_netserver::CPacket* pPacketDamage;
	int minY = max(pMonster->_tileY - 2, 0);
	int maxY = min(pMonster->_tileY + 2, dfFIELD_TILE_MAX_Y - 1);
	int minX = max(pMonster->_tileX - 2, 0);
	int maxX = min(pMonster->_tileX + 2, dfFIELD_TILE_MAX_X - 1);
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			std::vector<CObject*>& vecPlayer = _tile[y][x].GetObjectVector(ETileObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayerOther = (CPlayer*)vecPlayer[i];
				if (pPlayerOther->_bAlive == false)
					continue;

				// 몬스터 위치를 원점으로 했을 때, 플레이어의 좌표를 원점으로 옮기고 0도 방향으로 회전시킴
				float playerPosX = (pPlayerOther->_posX - monsterPosX) * R[0] - (pPlayerOther->_posY - monsterPosY) * R[1];
				float playerPosY = (pPlayerOther->_posX - monsterPosX) * R[1] + (pPlayerOther->_posY - monsterPosY) * R[0];

				// 플레이어의 좌표가 가로 pMonster->_attackRange 세로 pMonster->_attackRange * 2 사각형 안이면 hit
				if (playerPosX > 0 && playerPosX < pMonster->_attackRange && playerPosY > -pMonster->_attackRange && playerPosY < pMonster->_attackRange)
				{
					pPlayerOther->Hit(pMonster->_damage);

					// 대미지 패킷 전송
					pPacketDamage = AllocPacket();
					Msg_Damage(pPacketDamage, pMonster, pPlayerOther, pMonster->_damage);
					SendAroundSector(pPlayerOther->_sectorX, pPlayerOther->_sectorY, pPacketDamage, nullptr);
					pPacketDamage->SubUseCount();

					LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::MonsterAttack. hit player. sessionId:%lld, accountNo:%lld, target tile:(%d, %d), target pos:(%.3f, %.3f)\n"
						, pPlayerOther->_sessionId, pPlayerOther->_accountNo, pPlayerOther->_tileX, pPlayerOther->_tileY, pPlayerOther->_posX, pPlayerOther->_posY);
				}
			}

		}
	}
}

// 몬스터의 타겟을 찾음
void CGameContentsField::MonsterFindTarget(CMonster* pMonster)
{
	PROFILE_BEGIN("CGameContentsField::MonsterFindTarget");

	int viewSectorRange = dfFIELD_POS_TO_SECTOR(pMonster->_viewRange) + 1;
	int initialSectorX = dfFIELD_POS_TO_SECTOR(pMonster->_initialPosX);
	int initialSectorY = dfFIELD_POS_TO_SECTOR(pMonster->_initialPosY);

	int minX = max(pMonster->_sectorX - viewSectorRange, 0);
	int maxX = min(pMonster->_sectorX + viewSectorRange, _sectorMaxX - 1);
	int minY = max(pMonster->_sectorY - viewSectorRange, 0);
	int maxY = min(pMonster->_sectorY + viewSectorRange, _sectorMaxY - 1);

	// 섹터는 시야 범위의 랜덤한 위치부터 시작하여 찾도록 한다.
	int midX = minX + rand() % (maxX - minX + 1);
	int midY = minY + rand() % (maxY - minY + 1);
	
	CPlayer* pPlayer;
	for (int y = midY; y <= maxY; y++)
	{
		for (int x = midX; x <= maxX; x++)
		{
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayer = (CPlayer*)vecPlayer[i];
				if (pPlayer->_bAlive == false)
					continue;

				// 주변에 플레이어가 있다면 타겟으로 지정하도록 한다.
				pMonster->SetTargetPlayer(pPlayer);
			}
		}
		for (int x = minX; x < midX; x++)
		{
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayer = (CPlayer*)vecPlayer[i];
				if (pPlayer->_bAlive == false)
					continue;

				// 주변에 플레이어가 있다면 타겟으로 지정하도록 한다.
				pMonster->SetTargetPlayer(pPlayer);
			}
		}
	}
	for (int y = minY; y < midY; y++)
	{
		for (int x = midX; x <= maxX; x++)
		{
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayer = (CPlayer*)vecPlayer[i];
				if (pPlayer->_bAlive == false)
					continue;

				// 주변에 플레이어가 있다면 타겟으로 지정하도록 한다.
				pMonster->SetTargetPlayer(pPlayer);
			}
		}
		for (int x = minX; x < midX; x++)
		{
			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayer = (CPlayer*)vecPlayer[i];
				if (pPlayer->_bAlive == false)
					continue;

				// 주변에 플레이어가 있다면 타겟으로 지정하도록 한다.
				pMonster->SetTargetPlayer(pPlayer);
			}
		}
	}
}





/* 플레이어에게 패킷 전송 */

// 한명에게 보내기
int CGameContentsField::SendUnicast(CPlayer* pPlayer, game_netserver::CPacket* pPacket)
{

	//pPacket->AddUseCount();
	//pPlayer->_vecPacketBuffer.push_back(pPacket);
	SendPacket(pPlayer->_sessionId, pPacket);  // 임시

	return 1;
}






// 주변 섹터 전체에 보내기
int CGameContentsField::SendAroundSector(int sectorX, int sectorY, game_netserver::CPacket* pPacket, CPlayer* except)
{
	PROFILE_BEGIN("CGameContentsField::SendAroundSector");

	CSector** arrAroundSector = _sector[sectorY][sectorX].GetAroundSector();
	int numAroundSector = _sector[sectorY][sectorX].GetNumOfAroundSector();

	CPlayer* pPlayer;
	int sendCount = 0;
	for (int i = 0; i < numAroundSector; i++)
	{
		std::vector<CObject*>& vecPlayer = arrAroundSector[i]->GetObjectVector(ESectorObjectType::PLAYER);
		for (int i = 0; i < vecPlayer.size(); i++)
		{
			pPlayer = (CPlayer*)vecPlayer[i];
			if (pPlayer == except)
				continue;

			//pPacket->AddUseCount();
			//pPlayer->_vecPacketBuffer.push_back(pPacket);
			SendPacket(pPlayer->_sessionId, pPacket);  // 임시
			sendCount++;
		}
	}

	return sendCount;
}




// 2개 섹터의 주변 섹터 전체에 보내기
int CGameContentsField::SendTwoAroundSector(int sectorX1, int sectorY1, int sectorX2, int sectorY2, game_netserver::CPacket* pPacket, CPlayer* except)
{
	PROFILE_BEGIN("CGameContentsField::SendTwoAroundSector");

	if (sectorX1 == sectorX2 && sectorY1 == sectorY2)
	{
		return SendAroundSector(sectorX1, sectorY1, pPacket, except);
	}

	int leftX1 = max(sectorX1 - _sectorViewRange, 0);
	int rightX1 = min(sectorX1 + _sectorViewRange, _sectorMaxX - 1);
	int downY1 = max(sectorY1 - _sectorViewRange, 0);
	int upY1 = min(sectorY1 + _sectorViewRange, _sectorMaxY - 1);
	int leftX2 = max(sectorX2 - _sectorViewRange, 0);
	int rightX2 = min(sectorX2 + _sectorViewRange, _sectorMaxX - 1);
	int downY2 = max(sectorY2 - _sectorViewRange, 0);
	int upY2 = min(sectorY2 + _sectorViewRange, _sectorMaxY - 1);

	int minX = min(leftX1, rightX1);
	int maxX = max(rightX1, rightX2);
	int minY = min(downY1, downY2);
	int maxY = max(upY1, upY2);

	CPlayer* pPlayer;
	int sendCount = 0;
	// y 축은 2개 타일 범위 중 작은 값부터 큰 값까지, x 축도 2개 타일 범위 중 작은 값부터 큰 값까지 조사한다.
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			// 만약 (x,y)가 sector1 범위에도 속하지 않고 sector2 범위에도 속하지 않는다면 넘어감
			if (!(y >= downY1 && y <= upY1 && x >= leftX1 && x <= rightX1)
				&& !(y >= downY2 && y <= upY2 && x >= leftX2 && x <= rightX2))
			{
				continue;
			}

			std::vector<CObject*>& vecPlayer = _sector[y][x].GetObjectVector(ESectorObjectType::PLAYER);
			for (int i = 0; i < vecPlayer.size(); i++)
			{
				pPlayer = (CPlayer*)vecPlayer[i];
				if (pPlayer == except)
					continue;

				//pPacket->AddUseCount();
				//pPlayer->_vecPacketBuffer.push_back(pPacket);
				SendPacket(pPlayer->_sessionId, pPacket); // 임시
				sendCount++;
			}
		}
	}

	return sendCount;
}





/* 패킷 처리 */

// 캐릭터 이동 요청 처리
void CGameContentsField::PacketProc_MoveCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_MoveCharacter");

	INT64 clientId;
	float posX;
	float posY;
	USHORT rotation;
	BYTE VKey;
	BYTE HKey;

	*pPacket >> clientId >> posX >> posY >> rotation >> VKey >> HKey;

	pPlayer->_oldPosX = pPlayer->_posX;
	pPlayer->_oldPosY = pPlayer->_posY;
	pPlayer->_posX = posX;
	pPlayer->_posY = posY;
	pPlayer->_oldTileX = pPlayer->_tileX;
	pPlayer->_oldTileY = pPlayer->_tileY;
	pPlayer->_tileX = dfFIELD_POS_TO_TILE(posX);
	pPlayer->_tileY = dfFIELD_POS_TO_TILE(posY);
	pPlayer->_oldSectorX = pPlayer->_sectorX;
	pPlayer->_oldSectorY = pPlayer->_sectorY;
	pPlayer->_sectorX = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileX);
	pPlayer->_sectorY = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileY);
	pPlayer->_rotation = rotation;
	pPlayer->_VKey = VKey;
	pPlayer->_HKey = HKey;

	pPlayer->_bMove = true;

	// 앉았다 일어났을 때 플레이어 HP 보정
	if (pPlayer->_sit == 1)
	{
		pPlayer->_sit = 0;
		game_netserver::CPacket* pPacketPlayerHp = AllocPacket();
		Msg_PlayerHP(pPacketPlayerHp, pPlayer);
		SendUnicast(pPlayer, pPacketPlayerHp);
		pPacketPlayerHp->SubUseCount();

		// DB에 저장 요청
		DB_PlayerRenewHP(pPlayer);
	}


	// 타일 및 섹터 이동
	if (pPlayer->_tileY != pPlayer->_oldTileY || pPlayer->_tileX != pPlayer->_oldTileX)
	{
		MovePlayerTileAndSector(pPlayer);
	}


	// 내 주변에 캐릭터 이동 패킷 전송
	game_netserver::CPacket* pPacketMoveChr = AllocPacket();
	Msg_MoveCharacter(pPacketMoveChr, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketMoveChr, pPlayer);
	pPacketMoveChr->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_MoveCharacter. move player. sessionId:%lld, accountNo:%lld, tile (%d, %d) to (%d, %d), pos (%.3f, %.3f) to (%.3f, %.3f), rotation:%d\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_oldTileX, pPlayer->_oldTileY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_oldPosX, pPlayer->_oldPosY, pPlayer->_posX, pPlayer->_posY, pPlayer->_rotation);
}




// 캐릭터 정지 요청 처리
void CGameContentsField::PacketProc_StopCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_StopCharacter");

	INT64 clientId;
	float x;
	float y;
	USHORT rotation;

	*pPacket >> clientId >> x >> y >> rotation;

	pPlayer->_oldPosX = pPlayer->_posX;
	pPlayer->_oldPosY = pPlayer->_posY;
	pPlayer->_posX = x;
	pPlayer->_posY = y;
	pPlayer->_oldTileX = pPlayer->_tileX;
	pPlayer->_oldTileY = pPlayer->_tileY;
	pPlayer->_tileX = dfFIELD_POS_TO_TILE(x);
	pPlayer->_tileY = dfFIELD_POS_TO_TILE(y);
	pPlayer->_oldSectorX = pPlayer->_sectorX;
	pPlayer->_oldSectorY = pPlayer->_sectorY;
	pPlayer->_sectorX = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileX);
	pPlayer->_sectorY = dfFIELD_TILE_TO_SECTOR(pPlayer->_tileY);
	pPlayer->_rotation = rotation;
	pPlayer->_VKey = 0;
	pPlayer->_HKey = 0;

	pPlayer->_bMove = false;

	// 앉았다 일어났을 때 플레이어 HP 보정
	if (pPlayer->_sit == 1)
	{
		pPlayer->_sit = 0;
		game_netserver::CPacket* pPacketPlayerHp = AllocPacket();
		Msg_PlayerHP(pPacketPlayerHp, pPlayer);
		SendUnicast(pPlayer, pPacketPlayerHp);
		pPacketPlayerHp->SubUseCount();

		// DB에 저장 요청
		DB_PlayerRenewHP(pPlayer);
	}


	// 타일 좌표가 달라지면 타일 및 섹터 이동
	if (pPlayer->_tileY != pPlayer->_oldTileY || pPlayer->_tileX != pPlayer->_oldTileX)
	{
		MovePlayerTileAndSector(pPlayer);
	}


	// 내 주변에 캐릭터 정지 패킷 전송
	game_netserver::CPacket* pPacketStopChr = AllocPacket();
	Msg_StopCharacter(pPacketStopChr, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketStopChr, pPlayer);
	pPacketStopChr->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_StopCharacter. stop player. sessionId:%lld, accountNo:%lld, tile (%d, %d) to (%d, %d), pos (%.3f, %.3f) to (%.3f, %.3f), rotation:%d\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_oldTileX, pPlayer->_oldTileY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_oldPosX, pPlayer->_oldPosY, pPlayer->_posX, pPlayer->_posY, pPlayer->_rotation);
}



// 줍기 요청
void CGameContentsField::PacketProc_Pick(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_Pick");

	INT64 clientId;
	*pPacket >> clientId;

	// 주변 섹터에 먹기 액션 전송
	game_netserver::CPacket* pPacketPickAction = AllocPacket();
	Msg_Pick(pPacketPickAction, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketPickAction, nullptr);
	pPacketPickAction->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Pick. pick. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);

	// 주변 타일에 있는 모든 크리스탈 획득
	CCrystal* pCrystal = nullptr;
	game_netserver::CPacket* pPacketPickCrystal;
	game_netserver::CPacket* pPacketDeleteCrystal;
	int minY = max(pPlayer->_tileY - _tilePickRange, 0);
	int maxY = min(pPlayer->_tileY + _tilePickRange, dfFIELD_TILE_MAX_Y - 1);
	int minX = max(pPlayer->_tileX - _tilePickRange, 0);
	int maxX = min(pPlayer->_tileX + _tilePickRange, dfFIELD_TILE_MAX_X - 1);
	int pickAmount = 0;
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			std::vector<CObject*>& vecCrystal = _tile[y][x].GetObjectVector(ETileObjectType::CRYSTAL);
			for (int i = 0; i < vecCrystal.size(); i++)
			{
				pCrystal = (CCrystal*)vecCrystal[i];
				if (pCrystal->_bAlive == false)
					continue;

				pPlayer->AddCrystal(pCrystal->_amount);
				pickAmount += pCrystal->_amount;

				// 주변에 크리스탈 삭제 패킷 전송
				pPacketDeleteCrystal = AllocPacket();
				Msg_RemoveObject(pPacketDeleteCrystal, pCrystal);
				SendAroundSector(pCrystal->_sectorX, pCrystal->_sectorY, pPacketDeleteCrystal, nullptr);
				pPacketDeleteCrystal->SubUseCount();

				// 크리스탈 삭제처리
				pCrystal->SetDead();

				LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Pick. get crystal. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f), crystal:%d (+%d)\n"
					, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY, pPlayer->_crystal, pCrystal->_amount);

			}
		}
	}

	if (pickAmount > 0)
	{
		// 주변에 크리스탈 획득 패킷 전송
		pPacketPickCrystal = AllocPacket();
		Msg_PickCrystal(pPacketPickCrystal, pPlayer, pCrystal);
		SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketPickCrystal, nullptr);
		pPacketPickCrystal->SubUseCount();

		// DB에 저장
		DB_PlayerGetCrystal(pPlayer);
	}

}

// 캐릭터 공격 요청 처리1
void CGameContentsField::PacketProc_Attack1(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_Attack1");

	INT64 clientId;
	*pPacket >> clientId;

	// 주변에 공격 패킷 전송
	game_netserver::CPacket* pPacketAttack = AllocPacket();
	Msg_Attack1(pPacketAttack, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketAttack, pPlayer);
	pPacketAttack->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Attack1. attack1. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);


	float playerPosX = pPlayer->_posX;
	float playerPosY = pPlayer->_posY;
	int degree = (360 - pPlayer->_rotation + 90) % 360;  // rotation을 360도 각도로 변환

	// 점을 0도 방향으로 회전시키기 위한 cos, sin값
	float R[2] = { GetCosine(360 - degree), GetSine(360 - degree) };


	// 공격범위를 조사할 타일: 내 위치 기준 상하좌우 2개 타일
	// 공격범위: 내가 바라보는 방향으로 너비 0.5뒤~1.0앞, 높이 1.5의 사각형
	CMonster* pMonster;
	game_netserver::CPacket* pPacketDamage;
	int minTileX = max(pPlayer->_tileX - 2, 0);
	int maxTileX = min(pPlayer->_tileX + 2, dfFIELD_TILE_MAX_X - 1);
	int minTileY = max(pPlayer->_tileY - 2, 0);
	int maxTileY = min(pPlayer->_tileY + 2, dfFIELD_TILE_MAX_Y - 1);
	for (int y = minTileY; y <= maxTileY; y++)
	{
		for (int x = minTileX; x <= maxTileX; x++)
		{
			std::vector<CObject*>& vecMonster = _tile[y][x].GetObjectVector(ETileObjectType::MONSTER);
			for (int i = 0; i < vecMonster.size(); i++)
			{
				pMonster = (CMonster*)vecMonster[i];
				if (pMonster->_bAlive == false)
					continue;

				// 플레이어 위치를 원점으로 했을 때, 몬스터의 좌표를 원점으로 옮기고 0도 방향으로 회전시킴
				float monsterPosX = (pMonster->_posX - playerPosX) * R[0] - (pMonster->_posY - playerPosY) * R[1];
				float monsterPosY = (pMonster->_posX - playerPosX) * R[1] + (pMonster->_posY - playerPosY) * R[0];

				// 몬스터의 좌표가 가로 1.5 세로 1.5 사각형 안이면 hit
				if (monsterPosX > -0.5 && monsterPosX < 1.0 && monsterPosY > -0.75 && monsterPosY < 0.75)
				{
					pMonster->Hit(pPlayer->_damageAttack1);

					// 대미지 패킷 전송
					pPacketDamage = AllocPacket();
					Msg_Damage(pPacketDamage, pPlayer, pMonster, pPlayer->_damageAttack1);
					SendTwoAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pMonster->_sectorX, pMonster->_sectorY, pPacketDamage, nullptr);
					pPacketDamage->SubUseCount();

					LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Attack1. hit monster. sessionId:%lld, accountNo:%lld, target tile:(%d, %d), target pos:(%.3f, %.3f)\n"
						, pPlayer->_sessionId, pPlayer->_accountNo, pMonster->_tileX, pMonster->_tileY, pMonster->_posX, pMonster->_posY);
				}
			}

		}
	}
}




// 캐릭터 공격 요청 처리2
void CGameContentsField::PacketProc_Attack2(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_Attack2");

	INT64 clientId;
	*pPacket >> clientId;

	// 주변에 공격 패킷 전송
	game_netserver::CPacket* pPacketAttack = AllocPacket();
	Msg_Attack2(pPacketAttack, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketAttack, pPlayer);
	pPacketAttack->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Attack2. attack2. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);


	// 내가 바라보는 방향으로 0.5뒤, 1.5앞의 좌표를 찾음
	float playerPosX = pPlayer->_posX;
	float playerPosY = pPlayer->_posY;
	int degree = (360 - pPlayer->_rotation + 90) % 360;  // rotation을 360도 각도로 변환
	float tailPosX = GetCosine(degree + 180) * 0.5f + playerPosX;  // 1.5 위치뒤 x
	float tailPosY = GetSine(degree + 180) * 0.5f + playerPosY;    // 1.5 위치뒤 y
	float headPosX = GetCosine(degree) * 1.5f + playerPosX;        // 2.5 위치앞 x
	float headPosY = GetSine(degree) * 1.5f + playerPosY;          // 2.5 위치앞 y

	// 점을 0도 방향으로 회전시키기 위한 cos, sin값
	float R[2] = { GetCosine(360 - degree), GetSine(360 - degree) };

	// 공격 범위에 해당하는 타일 찾기
	// 공격범위를 조사할 타일: 내가 바라보는 방향으로 0.5뒤 ~ 1.5앞 위치를 포함하는 타일
	// 공격범위: 내가 바라보는 방향으로 너비 0.5뒤~1.5앞, 높이 1.0의 사각형
	int minTileX = max(dfFIELD_POS_TO_TILE(min(tailPosX, headPosX)) - 1, 0);
	int maxTileX = min(dfFIELD_POS_TO_TILE(max(tailPosX, headPosX)) + 2, dfFIELD_TILE_MAX_X - 1);
	int minTileY = max(dfFIELD_POS_TO_TILE(min(tailPosY, headPosY)) - 1, 0);
	int maxTileY = min(dfFIELD_POS_TO_TILE(max(tailPosY, headPosY)) + 2, dfFIELD_TILE_MAX_Y - 1);

	CMonster* pMonster;
	game_netserver::CPacket* pPacketDamage;
	for (int y = minTileY; y <= maxTileY; y++)
	{
		for (int x = minTileX; x <= maxTileX; x++)
		{
			std::vector<CObject*>& vecMonster = _tile[y][x].GetObjectVector(ETileObjectType::MONSTER);
			for (int i = 0; i < vecMonster.size(); i++)
			{
				pMonster = (CMonster*)vecMonster[i];
				if (pMonster->_bAlive == false)
					continue;

				// 플레이어 위치를 원점으로 했을 때, 몬스터의 좌표를 원점으로 옮기고 0도 방향으로 회전시킴
				float monsterPosX = (pMonster->_posX - playerPosX) * R[0] - (pMonster->_posY - playerPosY) * R[1];
				float monsterPosY = (pMonster->_posX - playerPosX) * R[1] + (pMonster->_posY - playerPosY) * R[0];

				// 몬스터의 좌표가 가로 2.0 세로 1.0 사각형 안이면 hit
				if (monsterPosX > -0.5 && monsterPosX < 1.5 && monsterPosY > -0.6 && monsterPosY < 0.6)
				{
					pMonster->Hit(pPlayer->_damageAttack2);

					// 대미지 패킷 전송
					pPacketDamage = AllocPacket();
					Msg_Damage(pPacketDamage, pPlayer, pMonster, pPlayer->_damageAttack2);
					SendTwoAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pMonster->_sectorX, pMonster->_sectorY, pPacketDamage, nullptr);
					pPacketDamage->SubUseCount();

					LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Attack2. hit monster. sessionId:%lld, accountNo:%lld, target tile:(%d, %d), target pos:(%.3f, %.3f)\n"
						, pPlayer->_sessionId, pPlayer->_accountNo, pMonster->_tileX, pMonster->_tileY, pMonster->_posX, pMonster->_posY);
				}
			}

		}
	}
}




// 바닥에 앉기 요청 처리
void CGameContentsField::PacketProc_Sit(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == false)
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_Sit");

	INT64 clientId;
	*pPacket >> clientId;

	// 플레이어 앉기 처리
	pPlayer->SetSit();

	// 주변 타일에 앉기 전송
	game_netserver::CPacket* pPacketSit = AllocPacket();
	Msg_Sit(pPacketSit, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketSit, pPlayer);
	pPacketSit->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Sit. sit. sessionId:%lld, accountNo:%lld, tile:(%d, %d), pos:(%.3f, %.3f)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_posX, pPlayer->_posY);
}


// 플레이어 죽은 후 다시하기 요청
void CGameContentsField::PacketProc_PlayerRestart(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	if (pPlayer->_bAlive == true)  // 사망하지 않았다면 무시
		return;

	PROFILE_BEGIN("CGameContentsField::PacketProc_PlayerRestart");

	// 플레이어를 타일과 섹터에서 제거하고 주변에 캐릭터 삭제 패킷을 보낸다.
	_tile[pPlayer->_tileY][pPlayer->_tileX].RemoveObject(ETileObjectType::PLAYER, pPlayer);
	_sector[pPlayer->_sectorY][pPlayer->_sectorX].RemoveObject(ESectorObjectType::PLAYER, pPlayer);
	game_netserver::CPacket* pPacketDeleteCharacter = AllocPacket();
	Msg_RemoveObject(pPacketDeleteCharacter, pPlayer);
	SendAroundSector(pPlayer->_sectorX, pPlayer->_sectorY, pPacketDeleteCharacter, nullptr);
	pPacketDeleteCharacter->SubUseCount();

	// 리스폰 세팅
	pPlayer->SetRespawn(FIELD_PLAYER_RESPAWN_TILE_X, FIELD_PLAYER_RESPAWN_TILE_Y);
	DB_PlayerRespawn(pPlayer);

	// 다시하기 패킷 전송
	game_netserver::CPacket* pPacketPlayerRestart = AllocPacket();
	Msg_PlayerRestart(pPacketPlayerRestart);
	SendUnicast(pPlayer, pPacketPlayerRestart);
	pPacketPlayerRestart->SubUseCount();

	// 플레이어를 타일과 섹터에 등록하고, 캐릭터를 생성하고, 주변 오브젝트들을 로드한다.
	InitializePlayerEnvironment(pPlayer);

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_PlayerRestart. respawn player. sessionId:%lld, accountNo:%lld, tile:(%d, %d) to (%d, %d)\n"
		, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_oldTileX, pPlayer->_oldTileY, pPlayer->_tileX, pPlayer->_tileY);

}


// 테스트 에코 요청 처리
void CGameContentsField::PacketProc_Echo(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	INT64 accountNo;
	LONGLONG sendTick;
	*pPacket >> accountNo >> sendTick;

	game_netserver::CPacket* pPacketEcho = AllocPacket();
	Msg_Echo(pPacketEcho, pPlayer, sendTick);
	SendUnicast(pPlayer, pPacketEcho);
	pPacketEcho->SubUseCount();

	LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsField::PacketProc_Echo. echo. sessionId:%lld, accountNo:%lld\n"
		, pPlayer->_sessionId, pPlayer->_accountNo);
}




/* 패킷에 데이터 입력 */

// 내 캐릭터 생성 패킷
void CGameContentsField::Msg_CreateMyCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_CREATE_MY_CHARACTER << pPlayer->_clientId << pPlayer->_characterType;
	pPacket->PutData((const char*)pPlayer->_userNick, sizeof(pPlayer->_userNick));
	*pPacket << pPlayer->_posX << pPlayer->_posY << pPlayer->_rotation << pPlayer->_crystal << pPlayer->_hp << pPlayer->_exp << pPlayer->_level;
}

// 다른 캐릭터 생성 패킷
void CGameContentsField::Msg_CreateOtherCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_CREATE_OTHER_CHARACTER << pPlayer->_clientId << pPlayer->_characterType;
	pPacket->PutData((const char*)pPlayer->_userNick, sizeof(pPlayer->_userNick));
	*pPacket << pPlayer->_posX << pPlayer->_posY << pPlayer->_rotation << pPlayer->_level << pPlayer->_respawn << pPlayer->_die << pPlayer->_sit;
}

// 몬스터 생성 패킷
void CGameContentsField::Msg_CreateMonsterCharacter(game_netserver::CPacket* pPacket, CMonster* pMonster, BYTE respawn)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_CREATE_MONSTER_CHARACTER << pMonster->_clientId << pMonster->_posX << pMonster->_posY << pMonster->_rotation << respawn;
}

// 캐릭터, 오브젝트 삭제 패킷
void CGameContentsField::Msg_RemoveObject(game_netserver::CPacket* pPacket, CObject* pObject)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_REMOVE_OBJECT << pObject->_clientId;
}

// 캐릭터 이동 패킷
void CGameContentsField::Msg_MoveCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_MOVE_CHARACTER << pPlayer->_clientId << pPlayer->_posX << pPlayer->_posY << pPlayer->_rotation << pPlayer->_VKey << pPlayer->_HKey;
}

// 캐릭터 정지 패킷
void CGameContentsField::Msg_StopCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_STOP_CHARACTER << pPlayer->_clientId << pPlayer->_posX << pPlayer->_posY << pPlayer->_rotation;
}

// 몬스터 이동 패킷
void CGameContentsField::Msg_MoveMonster(game_netserver::CPacket* pPacket, CMonster* pMonster)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_MOVE_MONSTER << pMonster->_clientId << pMonster->_posX << pMonster->_posY << pMonster->_rotation;
}

// 캐릭터 공격1 패킷
void CGameContentsField::Msg_Attack1(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_ATTACK1 << pPlayer->_clientId;
}

// 캐릭터 공격1 패킷
void CGameContentsField::Msg_Attack2(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_ATTACK2 << pPlayer->_clientId;
}

// 몬스터 공격 패킷
void CGameContentsField::Msg_MonsterAttack(game_netserver::CPacket* pPacket, CMonster* pMonster)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_MONSTER_ATTACK << pMonster->_clientId;
}

// 공격대상에게 대미지를 먹임 패킷
void CGameContentsField::Msg_Damage(game_netserver::CPacket* pPacket, CObject* pAttackObject, CObject* pTargetObject, int damage)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_DAMAGE << pAttackObject->_clientId << pTargetObject->_clientId << damage;
}

// 몬스터 사망 패킷
void CGameContentsField::Msg_MonsterDie(game_netserver::CPacket* pPacket, CMonster* pMonster)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_MONSTER_DIE << pMonster->_clientId;
}

// 크리스탈 생성 패킷
void CGameContentsField::Msg_CreateCrystal(game_netserver::CPacket* pPacket, CCrystal* pCrystal)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_CREATE_CRISTAL << pCrystal->_clientId << pCrystal->_crystalType << pCrystal->_posX << pCrystal->_posY;
}

// 크리스탈 먹기 요청
void CGameContentsField::Msg_Pick(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_PICK << pPlayer->_clientId;
}

// 바닥에 앉기 패킷
void CGameContentsField::Msg_Sit(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_SIT << pPlayer->_clientId;
}

// 크리스탈 획득 패킷
void CGameContentsField::Msg_PickCrystal(game_netserver::CPacket* pPacket, CPlayer* pPlayer, CCrystal* pCrystal)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_PICK_CRISTAL << pPlayer->_clientId << pCrystal->_clientId << pPlayer->_crystal;
}

// 플레이어 HP 보정 패킷
void CGameContentsField::Msg_PlayerHP(game_netserver::CPacket* pPacket, CPlayer* pPlayer)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_PLAYER_HP << pPlayer->_hp;
}

// 플레이어 죽음 패킷
void CGameContentsField::Msg_PlayerDie(game_netserver::CPacket* pPacket, CPlayer* pPlayer, int minusCrystal)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_PLAYER_DIE << pPlayer->_clientId << minusCrystal;
}

// 플레이어 죽은 후 다시하기 패킷
void CGameContentsField::Msg_PlayerRestart(game_netserver::CPacket* pPacket)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_PLAYER_RESTART;
}

// 테스트 에코 패킷
void CGameContentsField::Msg_Echo(game_netserver::CPacket* pPacket, CPlayer* pPlayer, LONGLONG sendTick)
{
	*pPacket << (WORD)en_PACKET_CS_GAME_RES_ECHO << pPlayer->_accountNo << sendTick;
}


/* DB에 데이터 저장 */
void CGameContentsField::DB_Login(CPlayer* pPlayer)
{
	std::wstring strIP(pPlayer->_szIP);
	strIP += L":";
	strIP += std::to_wstring(pPlayer->_port);
	_pDBConn->PostQueryRequest(
		L"INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`, `param3`, `param4`, `message`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d, %d, %d, '%s');"
		, 1, 11, pPlayer->_accountNo, L"Game", pPlayer->_tileX, pPlayer->_tileY, pPlayer->_crystal, pPlayer->_hp, strIP.c_str()
	);
}

void CGameContentsField::DB_Logout(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" UPDATE `gamedb`.`character`"
		" SET `posx`=%.3f, `posy`=%.3f, `tilex`=%d, `tiley`=%d, `rotation`=%d, `crystal`=%d, `hp`=%d, `exp`=%d, `level`=%d, `die`=%d"
		" WHERE `accountno`=%lld;"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`, `param3`, `param4`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d, %d, %d);"
		" COMMIT;"
		, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level, pPlayer->_die
		, pPlayer->_accountNo
		, 1, 12, pPlayer->_accountNo, L"Game", pPlayer->_tileX, pPlayer->_tileY, pPlayer->_crystal, pPlayer->_hp
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}

void CGameContentsField::DB_PlayerDie(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" UPDATE `gamedb`.`character`"
		" SET `posx`=%.3f, `posy`=%.3f, `tilex`=%d, `tiley`=%d, `rotation`=%d, `crystal`=%d, `hp`=%d, `exp`=%d, `level`=%d, `die`=%d"
		" WHERE `accountno`=%lld;"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`, `param3`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d, %d);"
		" COMMIT;"
		, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level, pPlayer->_die
		, pPlayer->_accountNo
		, 3, 31, pPlayer->_accountNo, L"Game", pPlayer->_tileX, pPlayer->_tileY, pPlayer->_crystal
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}

void CGameContentsField::DB_PlayerRespawn(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" UPDATE `gamedb`.`character`"
		" SET `posx`=%.3f, `posy`=%.3f, `tilex`=%d, `tiley`=%d, `rotation`=%d, `crystal`=%d, `hp`=%d, `exp`=%d, `level`=%d, `die`=%d"
		" WHERE `accountno`=%lld;"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`, `param3`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d, %d);"
		" COMMIT;"
		, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level, pPlayer->_die
		, pPlayer->_accountNo
		, 3, 31, pPlayer->_accountNo, L"Game", pPlayer->_tileX, pPlayer->_tileY, pPlayer->_crystal
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}

void CGameContentsField::DB_PlayerGetCrystal(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" UPDATE `gamedb`.`character`"
		" SET `posx`=%.3f, `posy`=%.3f, `tilex`=%d, `tiley`=%d, `rotation`=%d, `crystal`=%d, `hp`=%d, `exp`=%d, `level`=%d"
		" WHERE `accountno`=%lld;"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d);"
		" COMMIT;"
		, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level
		, pPlayer->_accountNo
		, 4, 41, pPlayer->_accountNo, L"Game", pPlayer->_crystal - pPlayer->_oldCrystal, pPlayer->_crystal
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}

void CGameContentsField::DB_PlayerRenewHP(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" UPDATE `gamedb`.`character`"
		" SET `posx`=%.3f, `posy`=%.3f, `tilex`=%d, `tiley`=%d, `rotation`=%d, `crystal`=%d, `hp`=%d, `exp`=%d, `level`=%d"
		" WHERE `accountno`=%lld;"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`, `param2`, `param3`)"
		" VALUES (%d, %d, %lld, '%s', %d, %d, %d);"
		" COMMIT;"
		, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level
		, pPlayer->_accountNo
		, 5, 51, pPlayer->_accountNo, L"Game", pPlayer->_sitStartHp, pPlayer->_hp, (int)((GetTickCount64() - pPlayer->_sitStartTime) / 1000)
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}



void CGameContentsField::DB_InsertPlayerInfo(CPlayer* pPlayer)
{
	_pDBConn->PostQueryRequest(
		L"START TRANSACTION;"
		" INSERT INTO `gamedb`.`character` (`accountno`, `charactertype`, `posx`, `posy`, `tilex`, `tiley`, `rotation`, `crystal`, `hp`, `exp`, `level`, `die`)"
		" VALUES(%lld, %d, %.3f, %.3f, %d, %d, %d, %d, %d, %lld, %d, %d);"
		" "
		" INSERT INTO `logdb`.`gamelog` (`type`, `code`, `accountno`, `servername`, `param1`)"
		" VALUES (%d, %d, %lld, '%s', %d);"
		" COMMIT;"
		, pPlayer->_accountNo, pPlayer->_characterType, pPlayer->_posX, pPlayer->_posY, pPlayer->_tileX, pPlayer->_tileY, pPlayer->_rotation, pPlayer->_crystal, pPlayer->_hp, pPlayer->_exp, pPlayer->_level, pPlayer->_die
		, 3, 32, pPlayer->_accountNo, L"Game", pPlayer->_characterType
	);
	pPlayer->_lastDBUpdateTime = GetTickCount64();
}





/* utils */
void CGameContentsField::ReadAreaInfoFromConfigJson(CJsonParser& configJson, int areaNumber)
{
	// json 파일의 "Area#" 요소에서 데이터를 읽어서 _arrAreaInfo 배열에 입력한다.
	std::wstring strArea = L"Area";
	strArea += std::to_wstring(areaNumber + 1);

	CJsonObject& configArea = configJson[L"ContentsField"][strArea.c_str()];
	_arrAreaInfo[areaNumber].x = configArea[L"X"].Int();
	_arrAreaInfo[areaNumber].y = configArea[L"Y"].Int();
	_arrAreaInfo[areaNumber].range = configArea[L"Range"].Int();
	_arrAreaInfo[areaNumber].normalMonsterNumber = configArea[L"NormalMonster"][L"Number"].Int();
	_arrAreaInfo[areaNumber].normalMonsterHP = configArea[L"NormalMonster"][L"HP"].Int();
	_arrAreaInfo[areaNumber].normalMonsterDamage = configArea[L"NormalMonster"][L"Damage"].Int();
	_arrAreaInfo[areaNumber].bossMonsterNumber = configArea[L"BossMonster"][L"Number"].Int();
	_arrAreaInfo[areaNumber].bossMonsterHP = configArea[L"BossMonster"][L"HP"].Int();
	_arrAreaInfo[areaNumber].bossMonsterDamage = configArea[L"BossMonster"][L"Damage"].Int();
}


float CGameContentsField::GetCosine(unsigned short degree)
{
	return _cosine[degree % 360];
}

float CGameContentsField::GetSine(unsigned short degree)
{
	return _sine[degree % 360];
}





