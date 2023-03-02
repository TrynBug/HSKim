#include "CommonProtocol.h"
#include "CChatServer.h"
#include "CSector.h"
#include "CPlayer.h"
#include "CDBAsyncWriter.h"

#include "cpp_redis/cpp_redis.h"
#pragma comment (lib, "NetServer.lib")
#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")
#pragma comment (lib, "ws2_32.lib")

#include "profiler.h"
#include "logger.h"
#include "CJsonParser.h"
#include "CCpuUsage.h"
#include "CPDH.h"

CChatServer::CChatServer()
	: _szCurrentPath{ 0, }, _szConfigFilePath{ 0, }, _szBindIP{ 0, }, _portNumber(0)
	, _numConcurrentThread(0), _numWorkerThread(0), _numMaxSession(0), _numMaxPlayer(0), _bUseNagle(true)
	, _packetCode(0), _packetKey(0), _maxPacketSize(0)
	, _monitoringServerPortNumber(0), _monitoringServerPacketCode(0), _monitoringServerPacketKey(0)
	, _pRedisClient(nullptr), _pDBConn(nullptr), _sector(nullptr)
	, _hThreadChatServer(0), _threadChatServerId(0), _hThreadMsgGenerator(0), _threadMsgGeneratorId(0)
	, _hThreadMonitoringCollector(0), _threadMonitoringCollectorId(0), _hEventMsg(0), _bEventSetFlag(false)
	, _bShutdown(false), _bTerminated(false)
	, _poolMsg(0, false, 100), _poolPlayer(0, false, 100), _poolAPCData(0, false, 100)  // 메모리풀 초기화
	, _pLANClientMonitoring(nullptr), _pCPUUsage(nullptr), _pPDH(nullptr)
{
	// DB Connector 생성
	_pDBConn = new CDBAsyncWriter();

	_serverNo = 1;  // 서버번호(채팅서버:1)

	_timeoutLogin = 10000000;
	_timeoutHeartBeat = 10000000;
	_timeoutLoginCheckPeriod = 10000;
	_timeoutHeartBeatCheckPeriod = 30000;

	QueryPerformanceFrequency(&_performanceFrequency);
}

CChatServer::~CChatServer() 
{


}


bool CChatServer::StartUp()
{
	// config 파일 읽기
	// 현재 경로와 config 파일 경로를 얻음
	GetCurrentDirectory(MAX_PATH, _szCurrentPath);
	swprintf_s(_szConfigFilePath, MAX_PATH, L"%s\\chat_server_config.json", _szCurrentPath);

	// config parse
	CJsonParser jsonParser;
	jsonParser.ParseJsonFile(_szConfigFilePath);

	const wchar_t* serverBindIP = jsonParser[L"ServerBindIP"].Str().c_str();
	wcscpy_s(_szBindIP, wcslen(serverBindIP) + 1, serverBindIP);
	_portNumber = jsonParser[L"ChatServerPort"].Int();

	_numConcurrentThread = jsonParser[L"NetRunningWorkerThread"].Int();
	_numWorkerThread = jsonParser[L"NetWorkerThread"].Int();

	_bUseNagle = (bool)jsonParser[L"EnableNagle"].Int();
	_numMaxSession = jsonParser[L"SessionLimit"].Int();
	_numMaxPlayer = jsonParser[L"PlayerLimit"].Int();
	_packetCode = jsonParser[L"PacketHeaderCode"].Int();
	_packetKey = jsonParser[L"PacketEncodeKey"].Int();
	_maxPacketSize = jsonParser[L"MaximumPacketSize"].Int();

	_monitoringServerPortNumber = jsonParser[L"MonitoringServerPort"].Int();
	_monitoringServerPacketCode = jsonParser[L"MonitoringServerPacketHeaderCode"].Int();
	_monitoringServerPacketKey = jsonParser[L"MonitoringServerPacketEncodeKey"].Int();


	// map 초기크기 지정
	_mapPlayer.max_load_factor(1.0f);
	_mapPlayer.rehash(_numMaxPlayer * 4);
	_mapPlayerAccountNo.max_load_factor(1.0f);
	_mapPlayerAccountNo.rehash(_numMaxPlayer * 4);


	// logger 초기화
	//logger::ex_Logger.SetConsoleLoggingLevel(LOGGING_LEVEL_WARN);
	logger::ex_Logger.SetConsoleLoggingLevel(LOGGING_LEVEL_INFO);
	//logger::ex_Logger.SetConsoleLoggingLevel(LOGGING_LEVEL_DEBUG);

	logger::ex_Logger.SetFileLoggingLevel(LOGGING_LEVEL_INFO);
	//logger::ex_Logger.SetFileLoggingLevel(LOGGING_LEVEL_DEBUG);
	//logger::ex_Logger.SetFileLoggingLevel(LOGGING_LEVEL_ERROR);

	//CNetServer::SetOutputDebug(true);


	LOGGING(LOGGING_LEVEL_INFO, L"\n********** StartUp Chat Server (single thread version) ************\n"
		L"Config File: %s\n"
		L"Server Bind IP: %s\n"
		L"Chat Server Port: %d\n"
		L"Monitoring Server Port: %d\n"
		L"Number of Network Worker Thread: %d\n"
		L"Number of Network Running Worker Thread: %d\n"
		L"Number of Maximum Session: %d\n"
		L"Number of Maximum Player: %d\n"
		L"Enable Nagle: %s\n"
		L"Packet Header Code: 0x%x\n"
		L"Packet Encode Key: 0x%x\n"
		L"Maximum Packet Size: %d\n"
		L"Monitoring Server Packet Header Code: 0x%x\n"
		L"Monitoring Server Packet Encode Key: 0x%x\n"
		L"*******************************************************************\n\n"
		, _szConfigFilePath
		, _szBindIP
		, _portNumber
		, _monitoringServerPortNumber
		, _numConcurrentThread
		, _numWorkerThread
		, _numMaxSession
		, _numMaxPlayer
		, _bUseNagle ? L"Yes" : L"No"
		, _packetCode
		, _packetKey
		, _maxPacketSize
		, _monitoringServerPacketCode
		, _monitoringServerPacketKey);


	// sector 배열 메모리 할당
	_sector = new CSector * [dfSECTOR_MAX_Y];
	_sector[0] = (CSector*)malloc(sizeof(CSector) * dfSECTOR_MAX_Y * dfSECTOR_MAX_X);
	for (int y = 0; y < dfSECTOR_MAX_Y; y++)
		_sector[y] = _sector[0] + (y * dfSECTOR_MAX_X);

	// sector 생성자 호출
	for (int y = 0; y < dfSECTOR_MAX_Y; y++)
	{
		for (int x = 0; x < dfSECTOR_MAX_X; x++)
		{
			new (&_sector[y][x]) CSector(x, y);
		}
	}

	// sector의 주변 sector 등록
	for (int y = 0; y < dfSECTOR_MAX_Y; y++)
	{
		for (int x = 0; x < dfSECTOR_MAX_X; x++)
		{
			for (int aroundY = y - 1; aroundY < y + 2; aroundY++)
			{
				for (int aroundX = x - 1; aroundX < x + 2; aroundX++)
				{
					if (aroundY < 0 || aroundY >= dfSECTOR_MAX_Y || aroundX < 0 || aroundX >= dfSECTOR_MAX_X)
					{
						_sector[y][x].AddAroundSector(aroundX, aroundY, CSector::GetDummySector());
					}
					else
					{
						_sector[y][x].AddAroundSector(aroundX, aroundY, &_sector[aroundY][aroundX]);
					}
				}
			}
		}
	}
	
	// event 객체 생성
	_hEventMsg = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (_hEventMsg == NULL)
	{
		wprintf(L"CreateEvent failed!!, error:%d\n", GetLastError());
		return false;
	}

	// 메모리 정렬 체크
	if ((unsigned long long)this % 64 != 0)
		LOGGING(LOGGING_LEVEL_ERROR, L"[chat server] chat server object is not aligned as 64\n");
	if ((unsigned long long) & _msgQ % 64 != 0)
		LOGGING(LOGGING_LEVEL_ERROR, L"[chat server] message queue object is not aligned as 64\n");


	// 채팅서버 스레드 start
	_hThreadChatServer = (HANDLE)_beginthreadex(NULL, 0, ThreadChatServer, (PVOID)this, 0, &_threadChatServerId);
	if (_hThreadChatServer == 0)
	{
		wprintf(L"failed to start chat thread. error:%u\n", GetLastError());
		return false;
	}


	// 메시지 생성 스레드 start
	_hThreadMsgGenerator = (HANDLE)_beginthreadex(NULL, 0, ThreadMsgGenerator, (PVOID)this, 0, &_threadMsgGeneratorId);
	if (_hThreadMsgGenerator == 0)
	{
		wprintf(L"failed to start message generator thread. error:%u\n", GetLastError());
		return false;
	}

	// DB Connect 및 DB Writer 스레드 start
	if (_pDBConn->ConnectAndRunThread("127.0.0.1", "root", "vmfhzkepal!", "accountdb", 3306) == false)
	{
		wprintf(L"DB Connector start failed!!\n");
		return false;
	}

	// 모니터링 서버와 연결하는 클라이언트 start
	_pLANClientMonitoring = new CLANClientMonitoring;
	if (_pLANClientMonitoring->StartUp(L"127.0.0.1", _monitoringServerPortNumber, true, _monitoringServerPacketCode, _monitoringServerPacketKey, 10000, true) == false)
	{
		wprintf(L"LAN client start failed\n");
		//return false;
	}

	// 모니터링 서버에 접속
	CPacket* pPacket = _pLANClientMonitoring->AllocPacket();
	*pPacket << (WORD)en_PACKET_SS_MONITOR_LOGIN << _serverNo;
	_pLANClientMonitoring->SendPacket(pPacket);
	pPacket->SubUseCount();

	// 모니터링 데이터 수집 스레드 start
	_pCPUUsage = new CCpuUsage;
	_pPDH = new CPDH;
	_pPDH->Init();
	_hThreadMonitoringCollector = (HANDLE)_beginthreadex(NULL, 0, ThreadMonitoringCollector, (PVOID)this, 0, &_threadMonitoringCollectorId);
	if (_hThreadMonitoringCollector == 0)
	{
		wprintf(L"failed to start monitoring collector thread. error:%u\n", GetLastError());
		return false;
	}


	// 네트워크 start
	bool retNetStart = CNetServer::StartUp(_szBindIP, _portNumber, _numConcurrentThread, _numWorkerThread, _bUseNagle, _numMaxSession, _packetCode, _packetKey, _maxPacketSize, true, true);
	if (retNetStart == false)
	{
		wprintf(L"Network Library Start failed\n");
		return false;
	}

	// redis start
	_pRedisClient = new cpp_redis::client;
	_pRedisClient->connect();

	return true;

}


/* Shutdown */
bool CChatServer::Shutdown()
{
	// ThreadMsgGenerator 는 이 값이 true가 되면 종료됨
	_bShutdown = true;  

	// 채팅서버 스레드에게 shutdown 메시지 삽입
	MsgChatServer* pMsg = _poolMsg.Alloc();
	pMsg->msgFrom = MSG_FROM_SERVER;
	pMsg->sessionId = 0;
	pMsg->pPacket = nullptr;
	pMsg->eServerMsgType = EServerMsgType::MSG_SHUTDOWN;
	_msgQ.Enqueue(pMsg);
	SetEvent(_hEventMsg);

	// accept 중지
	CNetServer::StopAccept();



	// 네트워크 shutdown
	CNetServer::Shutdown();

	// 채팅서버 스레드들이 종료되기를 60초간 기다림
	ULONGLONG timeout = 60000;
	ULONGLONG tick = GetTickCount64();
	DWORD retWait;
	retWait = WaitForSingleObject(_hThreadChatServer, (DWORD)timeout);
	if (retWait != WAIT_OBJECT_0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[chat server] terminate chat thread timeout. error:%u\n", GetLastError());
		TerminateThread(_hThreadChatServer, 0);
	}
	timeout = timeout - GetTickCount64() - tick;
	timeout = timeout < 1 ? 1 : timeout;
	retWait = WaitForSingleObject(_hThreadMsgGenerator, (DWORD)timeout);
	if (retWait != WAIT_OBJECT_0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[chat server] terminate message generator thread timeout. error:%u\n", GetLastError());
		TerminateThread(_hThreadMsgGenerator, 0);
	}

	// DB 종료
	_pDBConn->Close();
	return true;
}


/* DB 상태 */
int CChatServer::GetUnprocessedQueryCount() { return _pDBConn->GetUnprocessedQueryCount(); }
__int64 CChatServer::GetQueryRunCount() { return _pDBConn->GetQueryRunCount(); }
float CChatServer::GetMaxQueryRunTime() { return _pDBConn->GetMaxQueryRunTime(); }
float CChatServer::Get2ndMaxQueryRunTime() { return _pDBConn->Get2ndMaxQueryRunTime(); }
float CChatServer::GetMinQueryRunTime() { return _pDBConn->GetMinQueryRunTime(); }
float CChatServer::Get2ndMinQueryRunTime() { return _pDBConn->Get2ndMinQueryRunTime(); }
float CChatServer::GetAvgQueryRunTime() { return _pDBConn->GetAvgQueryRunTime(); }
int CChatServer::GetQueryRequestPoolSize() { return _pDBConn->GetQueryRequestPoolSize(); }
int CChatServer::GetQueryRequestAllocCount() { return _pDBConn->GetQueryRequestAllocCount(); }
int CChatServer::GetQueryRequestFreeCount() { return _pDBConn->GetQueryRequestFreeCount(); }


/* Get */
CPlayer* CChatServer::GetPlayerBySessionId(__int64 sessionId)
{
	auto iter = _mapPlayer.find(sessionId);
	if (iter == _mapPlayer.end())
		return nullptr;
	else
		return iter->second;
}

CPlayer* CChatServer::GetPlayerByAccountNo(__int64 accountNo)
{
	auto iter = _mapPlayerAccountNo.find(accountNo);
	if (iter == _mapPlayerAccountNo.end())
		return nullptr;
	else
		return iter->second;
}

void CChatServer::ReplacePlayerByAccountNo(__int64 accountNo, CPlayer* replacePlayer)
{
	auto iter = _mapPlayerAccountNo.find(accountNo);
	iter->second = replacePlayer;
}


/* 네트워크 전송 */
int CChatServer::SendUnicast(__int64 sessionId, netserver::CPacket* pPacket)
{
	//return SendPacket(sessionId, pPacket);
	return SendPacketAsync(sessionId, pPacket);
}

int CChatServer::SendUnicast(CPlayer* pPlayer, netserver::CPacket* pPacket)
{
	//return SendPacket(pPlayer->_sessionId, pPacket);
	return SendPacketAsync(pPlayer->_sessionId, pPacket);
}

int CChatServer::SendBroadcast(netserver::CPacket* pPacket)
{
	int sendCount = 0;
	for (const auto& iter : _mapPlayer)
	{
		//sendCount += SendPacket(iter.second->_sessionId, pPacket);
		sendCount += SendPacketAsync(iter.second->_sessionId, pPacket);
	}

	return sendCount;
}

int CChatServer::SendOneSector(CPlayer* pPlayer, netserver::CPacket* pPacket, CPlayer* except)
{
	int sendCount = 0;
	CSector* pSector = &_sector[pPlayer->_sectorY][pPlayer->_sectorX];
	for (const auto& player : pSector->GetPlayerVector())
	{
		if (player == except)
			continue;
		//sendCount += SendPacket(player->_sessionId, pPacket);
		sendCount += SendPacketAsync(player->_sessionId, pPacket);
	}

	return sendCount;
}

int CChatServer::SendAroundSector(CPlayer* pPlayer, netserver::CPacket* pPacket, CPlayer* except)
{
	int sendCount = 0;
	CSector** pArrSector = _sector[pPlayer->_sectorY][pPlayer->_sectorX].GetAllAroundSector();
	int numAroundSector = _sector[pPlayer->_sectorY][pPlayer->_sectorX].GetNumOfAroundSector();
	for (int i = 0; i < numAroundSector; i++)
	{
		for (const auto& player : pArrSector[i]->GetPlayerVector())
		{
			if (player == except)
				continue;
			//sendCount += SendPacket(player->_sessionId, pPacket);
			sendCount += SendPacketAsync(player->_sessionId, pPacket);
		}
	}

	return sendCount;
}


/* player */
void CChatServer::MoveSector(CPlayer* pPlayer, WORD x, WORD y)
{
	if (pPlayer->_sectorX == x && pPlayer->_sectorY == y)
		return;

	if (pPlayer->_sectorX >= 0 && pPlayer->_sectorX < dfSECTOR_MAX_X
		&& pPlayer->_sectorY >= 0 && pPlayer->_sectorY < dfSECTOR_MAX_Y)
	{
		_sector[pPlayer->_sectorY][pPlayer->_sectorX].RemovePlayer(pPlayer);
	}
	
	pPlayer->_sectorX = min(max(x, 0), dfSECTOR_MAX_X - 1);
	pPlayer->_sectorY = min(max(y, 0), dfSECTOR_MAX_Y - 1);
	_sector[pPlayer->_sectorY][pPlayer->_sectorX].AddPlayer(pPlayer);
}

void CChatServer::DisconnectPlayer(CPlayer* pPlayer)
{
	if (pPlayer->_bDisconnect == false)
	{
		pPlayer->_bDisconnect = true;
		CNetServer::Disconnect(pPlayer->_sessionId);
	}
}


void CChatServer::DeletePlayer(__int64 sessionId)
{
	auto iter = _mapPlayer.find(sessionId);
	if (iter == _mapPlayer.end())
		return;

	CPlayer* pPlayer = iter->second;
	if (pPlayer->_sectorX != SECTOR_NOT_SET && pPlayer->_sectorY != SECTOR_NOT_SET)
	{
		_sector[pPlayer->_sectorY][pPlayer->_sectorX].RemovePlayer(pPlayer);
	}

	auto iterAccountNo = _mapPlayerAccountNo.find(pPlayer->_accountNo);
	if(iterAccountNo != _mapPlayerAccountNo.end() 
		&& iterAccountNo->second->_sessionId == sessionId)  // 중복로그인이 발생하면 map의 player 객체가 교체되어 세션주소가 다를 수 있음. 이 경우 삭제하면 안됨
		_mapPlayerAccountNo.erase(iterAccountNo);

	_mapPlayer.erase(iter);
	_poolPlayer.Free(pPlayer);
}

std::unordered_map<__int64, CPlayer*>::iterator CChatServer::DeletePlayer(std::unordered_map<__int64, CPlayer*>::iterator& iterPlayer)
{
	CPlayer* pPlayer = iterPlayer->second;
	if (pPlayer->_sectorX != SECTOR_NOT_SET && pPlayer->_sectorY != SECTOR_NOT_SET)
	{
		_sector[pPlayer->_sectorY][pPlayer->_sectorX].RemovePlayer(pPlayer);
	}

	auto iterAccountNo = _mapPlayerAccountNo.find(pPlayer->_accountNo);
	if (iterAccountNo != _mapPlayerAccountNo.end()
		&& iterAccountNo->second->_sessionId == iterPlayer->second->_sessionId)  // 중복로그인이 발생하면 map의 player 객체가 교체되어 세션주소가 다를 수 있음. 이 경우 삭제하면 안됨
		_mapPlayerAccountNo.erase(iterAccountNo);

	auto iterNext = _mapPlayer.erase(iterPlayer);
	_poolPlayer.Free(pPlayer);
	return iterNext;
}


/* (static) 로그인 나머지 처리 APC 함수 */
void CChatServer::CompleteUnfinishedLogin(ULONG_PTR pStAPCData)
{
	CChatServer& chatServer = *((StAPCData*)pStAPCData)->pChatServer;
	netserver::CPacket* pRecvPacket = ((StAPCData*)pStAPCData)->pPacket;
	__int64 sessionId = ((StAPCData*)pStAPCData)->sessionId;
	__int64 accountNo = ((StAPCData*)pStAPCData)->accountNo;
	bool isNull = ((StAPCData*)pStAPCData)->isNull;
	char* redisSessionKey = ((StAPCData*)pStAPCData)->sessionKey;

	LOGGING(LOGGING_LEVEL_DEBUG, L"CompleteUnfinishedLogin start. session:%lld, accountNo:%lld\n", sessionId, accountNo);

	netserver::CPacket* pSendPacket = chatServer.AllocPacket();
	do  // do..while(0)
	{
		if (isNull == true)
		{
			// redis에 세션key가 없으므로 로그인 실패
			// 로그인실패 응답 발송
			*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << (BYTE)0 << accountNo;
			chatServer.SendUnicast(sessionId, pSendPacket);
			chatServer._disconnByNoSessionKey++; // 모니터링
			LOGGING(LOGGING_LEVEL_DEBUG, L"login failed by no session key. session:%lld, accountNo:%lld\n", sessionId, accountNo);

			// 연결 끊기
			chatServer.Disconnect(sessionId);
			break;
		}

		WCHAR id[20];
		WCHAR nickname[20];
		char  sessionKey[64];
		pRecvPacket->TakeData((char*)id, sizeof(id));
		pRecvPacket->TakeData((char*)nickname, sizeof(nickname));
		pRecvPacket->TakeData((char*)sessionKey, sizeof(sessionKey));

		if (memcmp(redisSessionKey, sessionKey, 64) != 0)
		{
			// 세션key가 다르기 때문에 로그인 실패
			// 로그인실패 응답 발송
			*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << (BYTE)0 << accountNo;
			chatServer.SendUnicast(sessionId, pSendPacket);
			chatServer._disconnByInvalidSessionKey++; // 모니터링
			LOGGING(LOGGING_LEVEL_DEBUG, L"login failed by invalid session key. session:%lld, accountNo:%lld\n", sessionId, accountNo);

			// 연결끊기
			chatServer.Disconnect(sessionId);
			break;
		}

		// 플레이어 얻기
		CPlayer* pPlayer = chatServer.GetPlayerBySessionId(sessionId);
		if (pPlayer == nullptr)   // 플레이어를 찾지못했다면 APC큐에 요청이 삽입된다음 leave된 플레이어임
			break;

		// 플레이어 중복로그인 체크
		CPlayer* pPlayerDup = chatServer.GetPlayerByAccountNo(accountNo);
		if (pPlayerDup != nullptr)
		{
			// 중복 로그인인 경우 기존 플레이어를 끊는다.
			chatServer.DisconnectPlayer(pPlayerDup);
			chatServer._disconnByDupPlayer++; // 모니터링

			// accountNo-player map에서 플레이어 객체를 교체한다.
			chatServer.ReplacePlayerByAccountNo(accountNo, pPlayer);
		}
		else
		{
			// 중복 로그인이 아닐 경우 accountNo-player 맵에 등록
			chatServer._mapPlayerAccountNo.insert(std::make_pair(accountNo, pPlayer));
		}


		// 클라이언트의 로그인 status 업데이트 (현재 DB 업데이트는 하지않고있음)
		//bool retDB = chatServer._pDBConn->PostQueryRequest(
		//	L" UPDATE `accountdb`.`status`"
		//	L" SET `status` = 2"
		//	L" WHERE `accountno` = %lld"
		//	, accountNo);
		//if (retDB == false)
		//{
		//	LOGGING(LOGGING_LEVEL_ERROR, L"posting DB status update request failed. session:%lld, accountNo:%lld\n", sessionId, accountNo);
		//}

		// 플레이어 정보 세팅
		pPlayer->SetPlayerInfo(accountNo, id, nickname, sessionKey);
		pPlayer->SetLogin();

		// 클라이언트에게 로그인 성공 패킷 발송
		*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << (BYTE)1 << accountNo;
		chatServer.SendUnicast(pPlayer, pSendPacket);
		LOGGING(LOGGING_LEVEL_DEBUG, L"send login succeed. session:%lld, accountNo:%lld\n", sessionId, accountNo);

		break;
	} while (0);


	chatServer._poolAPCData.Free((StAPCData*)pStAPCData);
	pRecvPacket->SubUseCount();
	pSendPacket->SubUseCount();
}


/* (static) 채팅서버 스레드 */
unsigned WINAPI CChatServer::ThreadChatServer(PVOID pParam)
{
	wprintf(L"begin chat server\n");
	CChatServer& chatServer = *(CChatServer*)pParam;
	MsgChatServer* pMsg;
	netserver::CPacket* pRecvPacket;
	WORD packetType;
	netserver::CPacket* pSendPacket;
	CPlayer* pPlayer;
	LARGE_INTEGER liEventWaitStart;
	LARGE_INTEGER liEventWaitEnd;
	while (true)
	{
		QueryPerformanceCounter(&liEventWaitStart);
		DWORD retWait = WaitForSingleObjectEx(chatServer._hEventMsg, INFINITE, TRUE);
		if (retWait != WAIT_OBJECT_0 && retWait != WAIT_IO_COMPLETION)
		{
			LOGGING(LOGGING_LEVEL_FATAL, L"Wait for event failed!!, error:%d\n", GetLastError());
			InterlockedExchange8((char*)&chatServer._bEventSetFlag, false);
			return 0;
		}
		InterlockedExchange8((char*)&chatServer._bEventSetFlag, false);
		QueryPerformanceCounter(&liEventWaitEnd);
		chatServer._eventWaitTime += (liEventWaitEnd.QuadPart - liEventWaitStart.QuadPart);  // 모니터링

		while (chatServer._msgQ.Dequeue(pMsg))
		{
			// 클라이언트에게 받은 메시지일 경우
			if (pMsg->msgFrom == MSG_FROM_CLIENT)
			{
				pRecvPacket = pMsg->pPacket;
				*pRecvPacket >> packetType;

				pSendPacket = chatServer.AllocPacket();  // send용 패킷 alloc (switch문 끝난 뒤 free함)

				// 패킷 타입에 따른 메시지 처리
				switch (packetType)
				{
				// 채팅서버 로그인 요청
				case en_PACKET_CS_CHAT_REQ_LOGIN:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::en_PACKET_CS_CHAT_REQ_LOGIN");

					INT64 accountNo;
					*pRecvPacket >> accountNo;
					LOGGING(LOGGING_LEVEL_DEBUG, L"receive login. session:%lld, accountNo:%lld\n", pMsg->sessionId, accountNo);

					// 플레이어객체 존재 체크
					pPlayer = chatServer.GetPlayerBySessionId(pMsg->sessionId);
					if (pPlayer == nullptr)
					{
						// _mapPlayer에 플레이어객체가 없으므로 세션만 끊는다.
						chatServer.CNetServer::Disconnect(pMsg->sessionId);
						chatServer._disconnByLoginFail++; // 모니터링

						// 로그인실패 응답 발송
						*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << (BYTE)0 << accountNo;
						chatServer.SendUnicast(pMsg->sessionId, pSendPacket);
						LOGGING(LOGGING_LEVEL_DEBUG, L"send login failed. session:%lld, accountNo:%lld\n", pMsg->sessionId, accountNo);
						break;
					}
					pPlayer->SetLogin();

					// redis 세션key get 작업을 비동기로 요청한다. (집에서 테스트해본결과 동기로 redis get 하는데 평균 234us 걸림, 비동기 redis get은 평균 60us)
					// 비동기 get이 완료되면 CompleteUnfinishedLogin 함수가 APC queue에 삽입된다.
					PROFILE_BLOCK_BEGIN("CChatServer::ThreadChatServer::RedisGet");
					StAPCData* pAPCData = chatServer._poolAPCData.Alloc();
					pAPCData->pChatServer = &chatServer;
					pAPCData->pPacket = pRecvPacket;
					pAPCData->sessionId = pMsg->sessionId;
					pAPCData->accountNo = accountNo;
					pRecvPacket->AddUseCount();
					std::string redisKey = std::to_string(accountNo);
					CChatServer* pChatServer = &chatServer;
					chatServer._pRedisClient->get(redisKey, [pChatServer, pAPCData](cpp_redis::reply& reply) {
						if (reply.is_null() == true)
						{
							pAPCData->isNull = true;
						}
						else
						{
							pAPCData->isNull = false;
							memcpy(pAPCData->sessionKey, reply.as_string().c_str(), 64);
						}
						DWORD ret = QueueUserAPC(pChatServer->CompleteUnfinishedLogin
							, pChatServer->_hThreadChatServer
							, (ULONG_PTR)pAPCData);
						if (ret == 0)
						{
							LOGGING(LOGGING_LEVEL_ERROR, L"failed to queue asynchronous redis get user APC. error:%u, session:%lld, accountNo:%lld\n"
								, GetLastError(), pAPCData->sessionId, pAPCData->accountNo);
						}
						});
					chatServer._pRedisClient->commit();
					PROFILE_BLOCK_END;
					LOGGING(LOGGING_LEVEL_DEBUG, L"request asynchronous redis get. session:%lld, accountNo:%lld\n", pMsg->sessionId, accountNo);
					break;
				}

				// 채팅서버 섹터 이동 요청
				case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::en_PACKET_CS_CHAT_REQ_SECTOR_MOVE");

					INT64 accountNo;
					WORD  sectorX;
					WORD  sectorY;
					*pRecvPacket >> accountNo >> sectorX >> sectorY;

					// 플레이어 객체 얻기
					pPlayer = chatServer.GetPlayerBySessionId(pMsg->sessionId);
					if (pPlayer == nullptr)
						break; //chatServer.Crash();

					if (pPlayer->_bDisconnect == true)
						break;

					// 데이터 검증
					if (pPlayer->_accountNo != accountNo)
					{
						LOGGING(LOGGING_LEVEL_DEBUG, L"receive sector move. account number is invalid!! session:%lld, accountNo (origin:%lld, recved:%lld), sector from (%d,%d) to (%d,%d)\n"
							, pPlayer->_sessionId, pPlayer->_accountNo, accountNo, pPlayer->_sectorX, pPlayer->_sectorY, sectorX, sectorY);
						chatServer.DisconnectPlayer(pPlayer);
						chatServer._disconnByInvalidAccountNo++; // 모니터링
						break;
					}
					if (sectorX < 0 || sectorX >= dfSECTOR_MAX_X || sectorY < 0 || sectorY >= dfSECTOR_MAX_Y)
					{
						LOGGING(LOGGING_LEVEL_DEBUG, L"receive sector move. sector coordinate is invalid!! session:%lld, accountNo:%lld, sector from (%d,%d) to (%d,%d)\n"
							, pPlayer->_sessionId, pPlayer->_accountNo, pPlayer->_sectorX, pPlayer->_sectorY, sectorX, sectorY);
						chatServer.DisconnectPlayer(pPlayer);
						chatServer._disconnByInvalidSector++; // 모니터링
						break;
					}


					// 섹터 이동
					LOGGING(LOGGING_LEVEL_DEBUG, L"receive sector move. session:%lld, accountNo:%lld, sector from (%d,%d) to (%d,%d)\n"
						, pMsg->sessionId, accountNo, pPlayer->_sectorX, pPlayer->_sectorY, sectorX, sectorY);
					chatServer.MoveSector(pPlayer, sectorX, sectorY);
					pPlayer->SetHeartBeatTime();

					// 채팅서버 섹터 이동 결과 발송
					*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << accountNo << pPlayer->_sectorX << pPlayer->_sectorY;
					chatServer.SendUnicast(pPlayer, pSendPacket);
					LOGGING(LOGGING_LEVEL_DEBUG, L"send sector move. session:%lld, accountNo:%lld, sector to (%d,%d)\n"
						, pMsg->sessionId, accountNo, pPlayer->_sectorX, pPlayer->_sectorY);
					break;
				}


				// 채팅서버 채팅보내기 요청
				case en_PACKET_CS_CHAT_REQ_MESSAGE:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::en_PACKET_CS_CHAT_REQ_MESSAGE");

					INT64 accountNo;
					WORD  messageLen;
					WCHAR* message;
					*pRecvPacket >> accountNo >> messageLen;
					message = (WCHAR*)pRecvPacket->GetFrontPtr();

					pPlayer = chatServer.GetPlayerBySessionId(pMsg->sessionId);
					if (pPlayer == nullptr)
						break; //chatServer.Crash();

					if (pPlayer->_bDisconnect == true)
						break;

					// 데이터 검증
					if (pPlayer->_accountNo != accountNo)
					{
						LOGGING(LOGGING_LEVEL_DEBUG, L"receive chat message. account number is invalid!! session:%lld, accountNo (origin:%lld, recved:%lld)\n"
							, pPlayer->_sessionId, pPlayer->_accountNo, accountNo);
						chatServer.DisconnectPlayer(pPlayer);
						chatServer._disconnByInvalidAccountNo++; // 모니터링
						break;
					}
					pPlayer->SetHeartBeatTime();

					// 채팅서버 채팅보내기 응답
					LOGGING(LOGGING_LEVEL_DEBUG, L"receive chat message. session:%lld, accountNo:%lld, messageLen:%d\n", pMsg->sessionId, accountNo, messageLen);
					*pSendPacket << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << accountNo;
					pSendPacket->PutData((char*)pPlayer->_id, sizeof(pPlayer->_id));
					pSendPacket->PutData((char*)pPlayer->_nickname, sizeof(pPlayer->_nickname));
					*pSendPacket << messageLen;
					pSendPacket->PutData((char*)message, messageLen);
					int sendCount = chatServer.SendAroundSector(pPlayer, pSendPacket, nullptr);
					LOGGING(LOGGING_LEVEL_DEBUG, L"send chat message. to %d players, session:%lld, accountNo:%lld, messageLen:%d, sector:(%d,%d)\n"
						, sendCount, pMsg->sessionId, accountNo, messageLen, pPlayer->_sectorX, pPlayer->_sectorY);
					break;
				}

				// 하트비트
				case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::en_PACKET_CS_CHAT_REQ_HEARTBEAT");

					pPlayer = chatServer.GetPlayerBySessionId(pMsg->sessionId);
					if (pPlayer == nullptr)
						break;
					pPlayer->SetHeartBeatTime();
					LOGGING(LOGGING_LEVEL_DEBUG, L"receive heart beat. session:%lld, accountNo:%lld\n", pMsg->sessionId, pPlayer->_accountNo);
					break;
				}


				default:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::DEFAULT");

					pPlayer = chatServer.GetPlayerBySessionId(pMsg->sessionId);
					LOGGING(LOGGING_LEVEL_DEBUG, L"received invalid packet type. session:%lld, accountNo:%lld, packet type:%d\n"
						, pMsg->sessionId, pPlayer == nullptr ? -1 : pPlayer->_accountNo, packetType);
					
					if (pPlayer == nullptr)
					{
						chatServer.CNetServer::Disconnect(pMsg->sessionId);
					}
					else
					{
						chatServer.DisconnectPlayer(pPlayer);
					}

					chatServer._disconnByInvalidMessageType++; // 모니터링
					break;
				}
				} // end of switch(packetType)

				// 패킷의 사용카운트 감소
				pRecvPacket->SubUseCount();
				pSendPacket->SubUseCount();
			}

			// 채팅서버 내부 메시지인 경우
			else if (pMsg->msgFrom == MSG_FROM_SERVER)
			{
				switch (pMsg->eServerMsgType)
				{
				// 플레이어 생성 메시지 
				case EServerMsgType::MSG_JOIN_PLAYER:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::MSG_JOIN_PLAYER");

					pPlayer = chatServer._poolPlayer.Alloc();
					pPlayer->Init(pMsg->sessionId);
					chatServer._mapPlayer.insert(std::make_pair(pMsg->sessionId, pPlayer));
					LOGGING(LOGGING_LEVEL_DEBUG, L"join player. sessionId:%lld\n", pMsg->sessionId);

					break;
				}

				// 플레이어 삭제 메시지 
				case EServerMsgType::MSG_LEAVE_PLAYER:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::MSG_LEAVE_PLAYER");

					LOGGING(LOGGING_LEVEL_DEBUG, L"message leave player. sessionId:%lld\n", pMsg->sessionId);
					auto iter = chatServer._mapPlayer.find(pMsg->sessionId);
					if (iter == chatServer._mapPlayer.end())
						break;
					pPlayer = iter->second;

					LOGGING(LOGGING_LEVEL_DEBUG, L"leave player. sessionId:%lld, accountNo:%lld\n", pMsg->sessionId, pPlayer->_bLogin ? pPlayer->_accountNo : -1);

					// 플레이어의 DB status를 로그아웃으로 업데이트 (현재 DB 업데이트는 하지않고있음)
					//bool retDB = chatServer._pDBConn->PostQueryRequest(
					//	L" UPDATE `accountdb`.`status`"
					//	L" SET `status` = 0"
					//	L" WHERE `accountno` = %lld"
					//	, pPlayer->_accountNo);
					//if (retDB == false)
					//{
					//	LOGGING(LOGGING_LEVEL_ERROR, L"posting DB status update request failed. session:%lld, accountNo:%lld\n", pMsg->sessionId, pPlayer->_accountNo);
					//}

					// 플레이어 객체 삭제
					chatServer.DeletePlayer(iter);

					break;
				}

				// 로그인 타임아웃 확인
				case EServerMsgType::MSG_CHECK_LOGIN_TIMEOUT:
				{
					PROFILE_BEGIN("CChatServer::ThreadChatServer::MSG_CHECK_LOGIN_TIMEOUT");

					LOGGING(LOGGING_LEVEL_DEBUG, L"message check login timeout\n");
					ULONGLONG currentTime;
					for (auto iter = chatServer._mapPlayer.begin(); iter != chatServer._mapPlayer.end(); ++iter)
					{
						currentTime = GetTickCount64();
						pPlayer = iter->second;
						if (pPlayer->_bLogin == false && pPlayer->_bDisconnect == false && currentTime - pPlayer->_lastHeartBeatTime > chatServer._timeoutLogin)
						{
							LOGGING(LOGGING_LEVEL_DEBUG, L"timeout login. sessionId:%lld\n", pPlayer->_sessionId);
							chatServer.DisconnectPlayer(pPlayer);
							chatServer._disconnByLoginTimeout++; // 모니터링
						}
					}
					break;
				}

				// 하트비트 타임아웃 확인
				case EServerMsgType::MSG_CHECK_HEART_BEAT_TIMEOUT:
				{
					LOGGING(LOGGING_LEVEL_DEBUG, L"message check heart beat timeout\n");
					ULONGLONG currentTime = GetTickCount64();
					for (auto iter = chatServer._mapPlayer.begin(); iter != chatServer._mapPlayer.end(); ++iter)
					{
						pPlayer = iter->second;
						if (pPlayer->_bDisconnect == false && currentTime - pPlayer->_lastHeartBeatTime > chatServer._timeoutHeartBeat)
						{
							LOGGING(LOGGING_LEVEL_DEBUG, L"timeout heart beat. sessionId:%lld, accountNo:%lld\n", pPlayer->_sessionId, pPlayer->_accountNo);
							chatServer.DisconnectPlayer(pPlayer);
							chatServer._disconnByHeartBeatTimeout++; // 모니터링
						}
					}
					break;
				}

				// shutdown
				case EServerMsgType::MSG_SHUTDOWN:
				{
					LOGGING(LOGGING_LEVEL_DEBUG, L"message shutdown\n");
					// 모든 플레이어의 연결을 끊고 스레드를 종료한다.
					for (auto iter = chatServer._mapPlayer.begin(); iter != chatServer._mapPlayer.end(); ++iter)
					{
						pPlayer = iter->second;
						if (pPlayer->_bDisconnect == false)
						{
							chatServer.DisconnectPlayer(pPlayer);
						}
					}

					wprintf(L"end chat server\n");
					return 0;
					break;
				}

				default:
				{
					LOGGING(LOGGING_LEVEL_ERROR, L"invalid server message type. type:%d\n", pMsg->eServerMsgType);
					break;
				}
				} // end of switch(pMsg->eServerMsgType)

			}

			else
			{
				LOGGING(LOGGING_LEVEL_ERROR, L"invalid message type:%d\n", pMsg->msgFrom);
			}

			// free 메시지
			chatServer._poolMsg.Free(pMsg);

			chatServer._msgHandleCount++;  // 모니터링

		} // end of while (chatServer._msgQ.Dequeue(pMsg))

	} // end of while (true)


	wprintf(L"end chat server\n");
	return 0;
}




/* (static) timeout 확인 메시지 발생 스레드 */
unsigned WINAPI CChatServer::ThreadMsgGenerator(PVOID pParam)
{
	wprintf(L"begin message generator\n");
	CChatServer& chatServer = *(CChatServer*)pParam;

	const int numMessage = 2;       // 메시지 타입 수
	int msgPeriod[numMessage] = { chatServer._timeoutLoginCheckPeriod, chatServer._timeoutHeartBeatCheckPeriod };   // 메시지 타입별 발생주기
	EServerMsgType msg[numMessage] = { EServerMsgType::MSG_CHECK_LOGIN_TIMEOUT, EServerMsgType::MSG_CHECK_HEART_BEAT_TIMEOUT };  // 메시지 타입
	ULONGLONG lastMsgTime[numMessage] = { 0, 0 };  // 마지막으로 해당 타입 메시지를 보낸 시간

	ULONGLONG currentTime;  // 현재 시간
	int nextMsgIdx;         // 다음에 발송할 메시지 타입
	ULONGLONG nextSendTime; // 다음 메시지 발송 시간
	while (true)
	{
		currentTime = GetTickCount64();

		// 다음에 보낼 메시지를 선택한다.
		nextMsgIdx = -1;
		nextSendTime = UINT64_MAX;
		for (int i = 0; i < numMessage; i++)
		{
			// 다음 메시지 발송시간(마지막으로 메시지를 보낸 시간 + 메시지 발생 주기)이 currentTime보다 작으면 바로 메시지 발송
			ULONGLONG t = lastMsgTime[i] + msgPeriod[i];
			if (t <= currentTime)
			{
				nextMsgIdx = i;
				nextSendTime = currentTime;
				break;
			}
			// 그렇지 않으면 기다려야하는 최소시간을 찾음
			else
			{
				if (t < nextSendTime)
				{
					nextMsgIdx = i;
					nextSendTime = t;
				}
			}
		}

		if (nextMsgIdx < 0 || nextMsgIdx >= numMessage)
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"Message generator is trying to generate an invalid message. message index: %d\n", nextMsgIdx);
			chatServer.Crash();
			return 0;
		}

		if (chatServer._bShutdown == true)
			break;
		// 다음 메시지 발생 주기까지 기다림
		if (nextSendTime > currentTime)
		{
			Sleep((DWORD)(nextSendTime - currentTime));
		}
		if (chatServer._bShutdown == true)
			break;

		// 타입에 따른 메시지 생성
		MsgChatServer* pMsg = chatServer._poolMsg.Alloc();
		pMsg->msgFrom = MSG_FROM_SERVER;
		switch (msg[nextMsgIdx])
		{

		case EServerMsgType::MSG_CHECK_LOGIN_TIMEOUT:
		{
			pMsg->sessionId = 0;
			pMsg->pPacket = nullptr;
			pMsg->eServerMsgType = EServerMsgType::MSG_CHECK_LOGIN_TIMEOUT;
			break;
		}

		case EServerMsgType::MSG_CHECK_HEART_BEAT_TIMEOUT:
		{
			pMsg->sessionId = 0;
			pMsg->pPacket = nullptr;
			pMsg->eServerMsgType = EServerMsgType::MSG_CHECK_HEART_BEAT_TIMEOUT;
			break;
		}
		}

		// 메시지큐에 Enqueue
		chatServer._msgQ.Enqueue(pMsg);

		// 마지막으로 메시지 보낸시간 업데이트
		lastMsgTime[nextMsgIdx] = nextSendTime;

		// 이벤트 set
		SetEvent(chatServer._hEventMsg);
	}

	wprintf(L"end message generator\n");
	return 0;
}



/* (static) 모니터링 데이터 수집 스레드 */
unsigned WINAPI CChatServer::ThreadMonitoringCollector(PVOID pParam)
{
	wprintf(L"begin monitoring collector thread\n");
	CChatServer& chatServer = *(CChatServer*)pParam;

	CPacket* pPacket;
	PDHCount pdhCount;
	LARGE_INTEGER liFrequency;
	LARGE_INTEGER liStartTime;
	LARGE_INTEGER liEndTime;
	time_t collectTime;
	__int64 spentTime;
	__int64 sleepTime;
	DWORD dwSleepTime;


	__int64 prevMsgHandleCount = 0;
	__int64 currMsgHandleCount = 0;

	QueryPerformanceFrequency(&liFrequency);

	// 최초 update
	chatServer._pCPUUsage->UpdateCpuTime();
	chatServer._pPDH->Update();

	// 최초 sleep
	Sleep(990);

	// 1초마다 모니터링 데이터를 수집하여 모니터링 서버에게 데이터를 보냄
	QueryPerformanceCounter(&liStartTime);
	while (chatServer._bShutdown == false)
	{
		// 데이터 수집
		time(&collectTime);
		chatServer._pCPUUsage->UpdateCpuTime();
		chatServer._pPDH->Update();
		pdhCount = chatServer._pPDH->GetPDHCount();
		currMsgHandleCount = chatServer.GetMsgHandleCount();

		// 모니터링 서버에 send
		pPacket = chatServer._pLANClientMonitoring->AllocPacket();
		*pPacket << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE;
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN << (int)1 << (int)collectTime; // 에이전트 ChatServer 실행 여부 ON / OFF
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU << (int)chatServer._pCPUUsage->ProcessTotal() << (int)collectTime; // 에이전트 ChatServer CPU 사용률
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM << (int)((double)pdhCount.processPrivateBytes / 1048576.0) << (int)collectTime; // 에이전트 ChatServer 메모리 사용 MByte
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_SESSION << chatServer.GetNumSession() << (int)collectTime; // 채팅서버 세션 수 (컨넥션 수)
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_PLAYER << chatServer.GetNumAccount() << (int)collectTime; // 채팅서버 인증성공 사용자 수 (실제 접속자)
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS << (int)(currMsgHandleCount - prevMsgHandleCount) << (int)collectTime; // 채팅서버 UPDATE 스레드 초당 처리 횟수
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL << chatServer.GetPacketAllocCount() << (int)collectTime;    // 채팅서버 패킷풀 사용량
		//*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL << chatServer.GetMsgAllocCount() << (int)collectTime; // 채팅서버 UPDATE MSG 풀 사용량
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL << chatServer.GetUnhandeledMsgCount() << (int)collectTime; // 채팅서버 UPDATE MSG 풀 사용량
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL << (int)chatServer._pCPUUsage->ProcessorTotal() << (int)collectTime; // 서버컴퓨터 CPU 전체 사용률
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY << (int)((double)pdhCount.systemNonpagedPoolBytes / 1048576.0) << (int)collectTime; // 서버컴퓨터 논페이지 메모리 MByte
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV << (int)(pdhCount.networkRecvBytes / 1024.0) << (int)collectTime; // 서버컴퓨터 네트워크 수신량 KByte
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND << (int)(pdhCount.networkSendBytes / 1024.0) << (int)collectTime; // 서버컴퓨터 네트워크 송신량 KByte
		*pPacket << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY << (int)((double)pdhCount.systemAvailableBytes / 1048576.0) << (int)collectTime; // 서버컴퓨터 사용가능 메모리 MByte

		prevMsgHandleCount = currMsgHandleCount;

		chatServer._pLANClientMonitoring->SendPacket(pPacket);
		pPacket->SubUseCount();


		// 앞으로 sleep할 시간을 계산한다.
		QueryPerformanceCounter(&liEndTime);
		spentTime = max(0, liEndTime.QuadPart - liStartTime.QuadPart);
		sleepTime = liFrequency.QuadPart - spentTime;  // performance counter 단위의 sleep 시간

		// sleep 시간을 ms 단위로 변환하여 sleep 한다.
		dwSleepTime = 0;
		if (sleepTime > 0)
		{
			dwSleepTime = (DWORD)round((double)(sleepTime * 1000) / (double)liFrequency.QuadPart);
			Sleep(dwSleepTime);
		}

		// 다음 로직의 시작시간은 [현재 로직 종료시간 + sleep한 시간] 이다.
		liStartTime.QuadPart = liEndTime.QuadPart + (dwSleepTime * (liFrequency.QuadPart / 1000));

	}

	wprintf(L"end monitoring collector thread\n");
	return 0;
}




/* crash */
void CChatServer::Crash()
{
	int* p = 0;
	*p = 0;
}





/* 네트워크 라이브러리 callback 함수 구현 */
bool CChatServer::OnRecv(__int64 sessionId, netserver::CPacket& packet)
{
	PROFILE_BEGIN("CChatServer::OnRecv");

	// 메시지 생성
	packet.AddUseCount();
	MsgChatServer* pMsg = _poolMsg.Alloc();
	pMsg->msgFrom = MSG_FROM_CLIENT;
	pMsg->sessionId = sessionId;
	pMsg->pPacket = &packet;
	
	// 메시지큐에 Enqueue
	_msgQ.Enqueue(pMsg);

	// 이벤트 set
	if (InterlockedExchange8((char*)&_bEventSetFlag, true) == false)
		SetEvent(_hEventMsg);

	return true;
}

bool CChatServer::OnConnectionRequest(unsigned long IP, unsigned short port) 
{ 
	if (GetNumPlayer() >= _numMaxPlayer)
	{
		InterlockedIncrement64(&_disconnByPlayerLimit); // 모니터링
		return false;
	}

	return true; 
}


bool CChatServer::OnClientJoin(__int64 sessionId)
{
	PROFILE_BEGIN("CChatServer::OnClientJoin");

	// Create player 메시지 생성
	MsgChatServer* pMsg = _poolMsg.Alloc();
	pMsg->msgFrom = MSG_FROM_SERVER;
	pMsg->sessionId = sessionId;
	pMsg->pPacket = nullptr;
	pMsg->eServerMsgType = EServerMsgType::MSG_JOIN_PLAYER;

	// 메시지큐에 Enqueue
	_msgQ.Enqueue(pMsg);

	// 이벤트 set
	if (InterlockedExchange8((char*)&_bEventSetFlag, true) == false)
		SetEvent(_hEventMsg);

	return true;
}


bool CChatServer::OnClientLeave(__int64 sessionId) 
{ 
	PROFILE_BEGIN("CChatServer::OnClientLeave");

	// Delete player 메시지 생성
	MsgChatServer* pMsg = _poolMsg.Alloc();
	pMsg->msgFrom = MSG_FROM_SERVER;
	pMsg->sessionId = sessionId;
	pMsg->pPacket = nullptr;
	pMsg->eServerMsgType = EServerMsgType::MSG_LEAVE_PLAYER;

	// 메시지큐에 Enqueue
	_msgQ.Enqueue(pMsg);

	// 이벤트 set
	if (InterlockedExchange8((char*)&_bEventSetFlag, true) == false)
		SetEvent(_hEventMsg);

	return true; 
}


void CChatServer::OnError(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_ERROR, szError, vaList);
	va_end(vaList);
}


void CChatServer::OnOutputDebug(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_DEBUG, szError, vaList);
	va_end(vaList);
}

void CChatServer::OnOutputSystem(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_INFO, szError, vaList);
	va_end(vaList);
}

