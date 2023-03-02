
#include "CGameServer.h"
#include "CGameContents.h"
#include "CommonProtocol.h"

#include "CDBConnector.h"

//#define ENABLE_PROFILER
#include "profiler.h"

#include "CGameContentsAuth.h"
#include "logger.h"

CGameContentsAuth::CGameContentsAuth(EContentsThread myContentsType, CGameServer* pGameServer, int FPS)
	//: CGameContents(myContentsType, pGameServer, FPS, CONTENTS_MODE_SEND_PACKET_IMMEDIATELY)
	: CGameContents(myContentsType, pGameServer, FPS)
	, _sessionTransferLimit(1000000)
{
	_pDBConn = new CDBConnector;
	_pDBConn->Connect("127.0.0.1", "root", "vmfhzkepal!", "gamedb", 3306);
}

CGameContentsAuth::~CGameContentsAuth()
{
	_pDBConn->Disconnect();
	delete _pDBConn;
}



/* virtual Get */
// @Override
__int64 CGameContentsAuth::GetQueryRunCount() const { return _pDBConn->GetQueryRunCount(); }
float CGameContentsAuth::GetMaxQueryRunTime() const { return _pDBConn->GetMaxQueryRunTime(); }
float CGameContentsAuth::GetMinQueryRunTime() const { return _pDBConn->GetMinQueryRunTime(); }
float CGameContentsAuth::GetAvgQueryRunTime() const { return _pDBConn->GetAvgQueryRunTime(); }





/* 컨텐츠 클래스 callback 함수 구현 */
 // 주기적으로 호출됨
void CGameContentsAuth::OnUpdate()
{

}

// 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
void CGameContentsAuth::OnRecv(__int64 sessionId, game_netserver::CPacket* pPacket)
{
	// 세션ID로 플레이어 객체를 얻는다.
	auto iter = _mapPlayer.find(sessionId);
	CPlayer* pPlayer = iter->second;
	pPlayer->SetHeartBeatTime();

	// 메시지 타입 확인
	WORD msgType;
	*pPacket >> msgType;

	game_netserver::CPacket* pSendPacket = AllocPacket();

	// 메시지 타입에 따른 처리
	switch (msgType)
	{
	// 로그인 요청일 경우
	case en_PACKET_CS_GAME_REQ_LOGIN:
	{
		PROFILE_BEGIN("ContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN");

		__int64 accountNo;
		char  sessionKey[64];
		int version;

		*pPacket >> accountNo;
		pPacket->TakeData(sessionKey, 64);
		*pPacket >> version;


		// redis에서 세션key get
		cpp_redis::client* pRedisClient = GetRedisClient();
		std::string strAccountNo = std::to_string(accountNo);
		std::future<cpp_redis::reply> redisFuture = pRedisClient->get(strAccountNo);
		pRedisClient->sync_commit();
		cpp_redis::reply redisReply = redisFuture.get();

		// 세션key 확인
		if (redisReply.is_null())
		{
			// 세션key가 없으면 로그인 실패 응답
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)0 << accountNo;
			SendPacketAndDisconnect(pPlayer->_sessionId, pSendPacket);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN. login failed by no session key. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}
		std::string redisSessionKey = redisReply.as_string();
		if (memcmp(redisSessionKey.c_str(), sessionKey, 64) != 0)
		{
			// 세션key가 다르면 로그인 실패 응답
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)0 << accountNo;
			SendPacketAndDisconnect(pPlayer->_sessionId, pSendPacket);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN. login failed by invalid session key. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}

		// 플레이어 로그인 처리
		pPlayer->SetLogin(accountNo, sessionKey);


		// DB에서 계정정보, 플레이어정보 조회
		MYSQL_RES* myRes = _pDBConn->ExecuteQueryAndGetResult(
			L"SELECT a.`userid`, a.`usernick`, b.`charactertype`, b.`posx`, b.`posy`, b.`tilex`, b.`tiley`, b.`rotation`, b.`crystal`, b.`hp`, b.`exp`, b.`level`, b.`die`"
			L" FROM `accountdb`.`account` a"
			L" LEFT JOIN `gamedb`.`character` b"
			L" ON a.`accountno` = b.`accountno`"
			L" WHERE a.`accountno` = %lld"
			, accountNo);

		// 조회된 정보가 없다면 오류
		int numRows = (int)_pDBConn->GetNumQueryRows(myRes);
		if (numRows == 0)
		{
			// 로그인 실패 응답
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)0 << accountNo;
			SendPacketAndDisconnect(pPlayer->_sessionId, pSendPacket);
			_pDBConn->FreeResult(myRes);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN. login failed by no account data. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}

		// fetch row
		MYSQL_ROW myRow = _pDBConn->FetchRow(myRes);
		// 플레이어 ID 세팅
		pPlayer->SetAccountInfo(myRow[0], myRow[1], GetSessionIP(sessionId), GetSessionPort(sessionId));

		// charactertype이 NULL이라면 신규캐릭터 선택 응답 후 종료
		if (myRow[2] == nullptr)
		{
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)2 << accountNo;
			SendPacket(pPlayer->_sessionId, pSendPacket);
			_pDBConn->FreeResult(myRes);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN. select character. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}

		// 플레이어 정보 로드
		pPlayer->LoadCharacterInfo((BYTE)std::stoi(myRow[2]), (float)std::stod(myRow[3]), (float)std::stod(myRow[4]), std::stoi(myRow[5]), std::stoi(myRow[6])
			, (USHORT)std::stoi(myRow[7]), std::stoi(myRow[8]), std::stoi(myRow[9]), std::stoll(myRow[10]), (USHORT)std::stoi(myRow[11]), std::stoi(myRow[12]));
		_pDBConn->FreeResult(myRes);

		// 로그인 성공 응답
		*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN << (BYTE)1 << accountNo;
		SendPacket(pPlayer->_sessionId, pSendPacket);

		// 필드 컨텐츠로 이동
		_mapPlayer.erase(iter);
		TransferSession(pPlayer->_sessionId, EContentsThread::FIELD, pPlayer);

		LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_LOGIN. login succeed. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
		break;
	}

	// 캐릭터 선택 요청
	case en_PACKET_CS_GAME_REQ_CHARACTER_SELECT:
	{
		BYTE characterType;
		*pPacket >> characterType;

		// 로그인을 안했을 경우 실패 응답
		if (pPlayer->_bLogin == false)
		{
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_CHARACTER_SELECT << (BYTE)0;
			SendPacketAndDisconnect(pPlayer->_sessionId, pSendPacket);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_CHARACTER_SELECT. disconnect by not login. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}

		// 캐릭터 타입이 1 ~ 5가 아닐 경우 실패 응답
		if (characterType < 1 || characterType > 5)
		{
			*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_CHARACTER_SELECT << (BYTE)0;
			SendPacketAndDisconnect(pPlayer->_sessionId, pSendPacket);
			LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_CHARACTER_SELECT. disconnect by invalid character type. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
			break;
		}

		// 플레이어 캐릭터 정보 세팅 (DB에 플레이어 정보를 저장하는것은 다음 컨텐츠에서 함)
		pPlayer->SetCharacterInfo(characterType);

		// 캐릭터 선택 성공 응답
		*pSendPacket << (WORD)en_PACKET_CS_GAME_RES_CHARACTER_SELECT << (BYTE)1;
		SendPacket(pPlayer->_sessionId, pSendPacket);

		// 필드 컨텐츠로 이동
		_mapPlayer.erase(iter);
		TransferSession(pPlayer->_sessionId, EContentsThread::FIELD, pPlayer);

		LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnRecv::en_PACKET_CS_GAME_REQ_CHARACTER_SELECT. succeed character select. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
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

		LOGGING(LOGGING_LEVEL_DEBUG, L"CContentsAuth::OnRecv::DEFAULT. sessionId:%lld, accountNo:%lld, msg type:%d\n", pPlayer->_sessionId, pPlayer->_accountNo, msgType);
		break;
	}
	}

	pSendPacket->SubUseCount();

}

// 스레드에 세션이 들어왔을 때 호출됨
void CGameContentsAuth::OnSessionJoin(__int64 sessionId, PVOID data)
{
	CPlayer* pPlayer;
	if (data == nullptr)
	{
		// 최초 접속한 세션일 경우 플레이어 객체를 생성한다.
		pPlayer = AllocPlayer();
		pPlayer->Init(sessionId);
		LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnSessionJoin. initial join. sessionId:%lld\n", pPlayer->_sessionId);
	}
	else
	{
		// 파라미터에서 플레이어 객체를 얻는다.
		pPlayer = (CPlayer*)data;
		LOGGING(LOGGING_LEVEL_DEBUG, L"CGameContentsAuth::OnSessionJoin. transfer join. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
	}
	
	// 플레이어를 map에 등록한다.
	_mapPlayer.insert(std::make_pair(sessionId, pPlayer));
}


// 스레드 내에서 세션의 연결이 끊겼을때 호출됨
void CGameContentsAuth::OnSessionDisconnected(__int64 sessionId)
{
	// 플레이어 객체 얻기
	auto iter = _mapPlayer.find(sessionId);
	if (iter == _mapPlayer.end())
		return;

	CPlayer* pPlayer = iter->second;

	LOGGING(LOGGING_LEVEL_DEBUG, L"CContentsAuth::OnSessionDisconnected. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);

	// 플레이어를 map에서 삭제하고 free
	_mapPlayer.erase(iter);
	FreePlayer(pPlayer);

}





