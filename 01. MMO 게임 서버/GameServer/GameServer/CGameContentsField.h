#pragma once



#define FIELD_NUMBER_OF_AREA  7

struct StAreaInfo
{
	int x;
	int y;
	int range;
	int normalMonsterNumber;
	int normalMonsterHP;
	int normalMonsterDamage;
	int bossMonsterNumber;
	int bossMonsterHP;
	int bossMonsterDamage;
};


class CDBAsyncWriter;
class CTile;
class CSector;
class CMonster;
class CCrystal;

class CGameContentsField : public CGameContents
{
private:
	int _sessionTransferLimit;

	/* position */
	float _maxPosX;
	float _maxPosY;

	/* 타일 */
	CTile** _tile;
	int _tileMaxX;
	int _tileMaxY;
	int _tileViewRange;  // 플레이어가 보는 타일 범위(5일 경우 플레이어 위치로부터 앞,뒤,좌,우로 5 타일씩, 즉 11*11=121개의 타일을 봄)

	/* 섹터 */
	CSector** _sector;
	int _numTileXPerSector;  // 섹터 하나에 타일이 가로로 몇 개 있는지 수. 값이 4라면 섹터 하나에 타일이 4*4=16개 있는 것이다.
	int _sectorMaxX;
	int _sectorMaxY;
	int _sectorViewRange;
	


	/* config */
	StAreaInfo _arrAreaInfo[FIELD_NUMBER_OF_AREA];   // area 정보
	int _numInitialCrystal;                       // 맵 상의 초기 크리스탈 수

	int _numRandomCrystalGeneration;     // 필드에 초당 랜덤생성되는 크리스탈 수
	int _maxRandomCrystalGeneration;     // 필드에 랜덤생성되는 크리스탈의 최대 수
	int _tilePickRange;                  // 줍기 타일 범위
	int _crystalLossWhenDie;             // 죽었을 때 크리스탈 잃는 양
	
	/* monster */
	int _numMonster;           // 전체 몬스터 수
	CMonster** _arrPtrMonster;     // 전체 몬스터 배열

	/* pool */
	CMemoryPool<CCrystal>* _poolCrystal;

	/* DB */
	CDBAsyncWriter* _pDBConn;

	/* utils */
	float _cosine[360];
	float _sine[360];

public:
	CGameContentsField(EContentsThread myContentsType, CGameServer* pGameServer, int FPS);
	virtual ~CGameContentsField();

public:
	/* Init */
	void Init();

	/* Get */
	int GetQueryRequestPoolSize() const;
	int GetQueryRequestAllocCount() const;
	int GetQueryRequestFreeCount() const;

	/* Get (가상함수) */
	// @Override
	virtual __int64 GetQueryRunCount() const;
	virtual float GetMaxQueryRunTime() const;
	virtual float GetMinQueryRunTime() const;
	virtual float GetAvgQueryRunTime() const;
	virtual int GetRemainQueryCount() const;

	/* Set */
	void SetSessionTransferLimit(int limit) { _sessionTransferLimit = limit; }

private:
	/* 컨텐츠 클래스 callback 함수 구현 */
	virtual void OnUpdate();                                     // 주기적으로 호출됨
	virtual void OnRecv(__int64 sessionId, game_netserver::CPacket* pPacket);    // 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
	virtual void OnSessionJoin(__int64 sessionId, PVOID data);   // 스레드에 세션이 들어왔을 때 호출됨
	virtual void OnSessionDisconnected(__int64 sessionId);       // 스레드 내에서 세션의 연결이 끊겼을때 호출됨

	
private:
	/* 플레이어 */
	void InitializePlayerEnvironment(CPlayer* pPlayer);  // 플레이어를 타일에 등록하고, 캐릭터를 생성하고, 주변 오브젝트들을 로드한다. 플레이어 접속, 리스폰 시에 호출한다.
	void MovePlayerTileAndSector(CPlayer* pPlayer);  // 플레이어를 이전 타일, 섹터에서 현재 타일, 섹터로 옮긴다.

	/* 몬스터 */
	void MoveMonsterTileAndSector(CMonster* pMonster); // 몬스터를 이전 타일, 섹터에서 현재 타일, 섹터로 옮긴다.
	void MonsterAttack(CMonster* pMonster);   // 몬스터가 공격함
	void MonsterFindTarget(CMonster* pMonster);  // 몬스터의 타겟을 찾음

private:
	/* 플레이어에게 패킷 전송 */
	int SendUnicast(CPlayer* pPlayer, game_netserver::CPacket* pPacket);                         // 한명에게 보내기
	int SendAroundSector(int sectorX, int sectorY, game_netserver::CPacket* pPacket, CPlayer* except);  // 주변 섹터 전체에 보내기
	int SendTwoAroundSector(int sectorX1, int sectorY1, int sectorX2, int sectorY2, game_netserver::CPacket* pPacket, CPlayer* except);  // 2개 섹터의 주변 섹터 전체에 보내기

	/* 패킷 처리 */
	void PacketProc_MoveCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);   // 캐릭터 이동 요청 처리
	void PacketProc_StopCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);   // 캐릭터 정지 요청 처리
	void PacketProc_Attack1(game_netserver::CPacket* pPacket, CPlayer* pPlayer);         // 캐릭터 공격 요청 처리1
	void PacketProc_Attack2(game_netserver::CPacket* pPacket, CPlayer* pPlayer);         // 캐릭터 공격 요청 처리2
	void PacketProc_Sit(game_netserver::CPacket* pPacket, CPlayer* pPlayer);             // 바닥에 앉기 요청 처리
	void PacketProc_Pick(game_netserver::CPacket* pPacket, CPlayer* pPlayer);            // 줍기 요청 처리
	void PacketProc_PlayerRestart(game_netserver::CPacket* pPacket, CPlayer* pPlayer);   // 플레이어 죽은 후 다시하기 요청
	void PacketProc_Echo(game_netserver::CPacket* pPacket, CPlayer* pPlayer);            // 테스트 에코 요청 처리

	/* 패킷에 데이터 입력 */
	void Msg_CreateMyCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);        // 내 캐릭터 생성 패킷
	void Msg_CreateOtherCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);     // 다른 캐릭터 생성 패킷
	void Msg_CreateMonsterCharacter(game_netserver::CPacket* pPacket, CMonster* pMonster, BYTE respawn); // 몬스터 생성 패킷
	void Msg_RemoveObject(game_netserver::CPacket* pPacket, CObject* pObject);             // 캐릭터, 오브젝트 삭제 패킷
	void Msg_MoveCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);            // 캐릭터 이동 패킷
	void Msg_StopCharacter(game_netserver::CPacket* pPacket, CPlayer* pPlayer);            // 캐릭터 정지 패킷
	void Msg_MoveMonster(game_netserver::CPacket* pPacket, CMonster* pMonster);            // 몬스터 이동 패킷
	void Msg_Attack1(game_netserver::CPacket* pPacket, CPlayer* pPlayer);                  // 캐릭터 공격1 패킷
	void Msg_Attack2(game_netserver::CPacket* pPacket, CPlayer* pPlayer);                  // 캐릭터 공격1 패킷
	void Msg_MonsterAttack(game_netserver::CPacket* pPacket, CMonster* pMonster);          // 몬스터 공격 패킷
	void Msg_Damage(game_netserver::CPacket* pPacket, CObject* pAttackObject, CObject* pTargetObject, int damage);    // 공격대상에게 대미지를 먹임 패킷
	void Msg_MonsterDie(game_netserver::CPacket* pPacket, CMonster* pMonster);             // 몬스터 사망 패킷
	void Msg_CreateCrystal(game_netserver::CPacket* pPacket, CCrystal* pCrystal);          // 크리스탈 생성 패킷
	void Msg_Pick(game_netserver::CPacket* pPacket, CPlayer* pPlayer);                     // 먹기 동작 패킷
	void Msg_Sit(game_netserver::CPacket* pPacket, CPlayer* pPlayer);                      // 바닥에 앉기 패킷
	void Msg_PickCrystal(game_netserver::CPacket* pPacket, CPlayer* pPlayer, CCrystal* pCrystal);   // 크리스탈 획득 패킷
	void Msg_PlayerHP(game_netserver::CPacket* pPacket, CPlayer* pPlayer);                 // 플레이어 HP 보정 패킷
	void Msg_PlayerDie(game_netserver::CPacket* pPacket, CPlayer* pPlayer, int minusCrystal);  // 플레이어 죽음 패킷
	void Msg_PlayerRestart(game_netserver::CPacket* pPacket);                              // 플레이어 죽은 후 다시하기 패킷
	void Msg_Echo(game_netserver::CPacket* pPacket, CPlayer* pPlayer, LONGLONG sendTick);  // 테스트 에코 패킷

	/* DB에 데이터 저장 */
	void DB_Login(CPlayer* pPlayer);
	void DB_Logout(CPlayer* pPlayer);
	void DB_PlayerDie(CPlayer* pPlayer);
	void DB_PlayerRespawn(CPlayer* pPlayer);
	void DB_PlayerGetCrystal(CPlayer* pPlayer);
	void DB_PlayerRenewHP(CPlayer* pPlayer);
	void DB_InsertPlayerInfo(CPlayer* pPlayer);

private:
	/* utils */
	void ReadAreaInfoFromConfigJson(CJsonParser& configJson, int areaNumber);
	float GetCosine(unsigned short degree);
	float GetSine(unsigned short degree);
};
