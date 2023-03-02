#include "CNetServer.h"
#include "CTimeMgr.h"

using namespace game_netserver;

CContents::CContents(int FPS)
	:_pNetServer(nullptr), _hThreadContents(0), _bTerminated(false)
	, _sessionPacketProcessLimit(1000000)
	, _bEnableHeartBeatTimeout(false), _timeoutHeartBeat(0), _timeoutHeartBeatCheckPeriod(0), _lastHeartBeatCheckTime(0)
	, _FPS(FPS), _logicStartTime{ 0, }, _logicEndTime{ 0, }, _performanceFrequency{ 0, }, _catchUpTime(0)
	, _pTimeMgr(nullptr)
{
	QueryPerformanceFrequency(&_performanceFrequency);
	_oneFrameTime = _performanceFrequency.QuadPart / _FPS;
	_pTimeMgr = new CTimeMgr;

	_mode = 0;
}

CContents::CContents(int FPS, DWORD mode)
	:_pNetServer(nullptr), _hThreadContents(0), _bTerminated(false)
	, _sessionPacketProcessLimit(1000000)
	, _bEnableHeartBeatTimeout(false), _timeoutHeartBeat(0), _timeoutHeartBeatCheckPeriod(0), _lastHeartBeatCheckTime(0)
	, _FPS(FPS), _logicStartTime{ 0, }, _logicEndTime{ 0, }, _performanceFrequency{ 0, }, _catchUpTime(0)
	, _pTimeMgr(nullptr)
{
	QueryPerformanceFrequency(&_performanceFrequency);
	_oneFrameTime = _performanceFrequency.QuadPart / _FPS;
	_pTimeMgr = new CTimeMgr;

	_mode = mode;
}


CContents::~CContents()
{


	
}



/* 컨텐츠 */
// 스레드 시작
bool CContents::StartUp()
{
	_hThreadContents = (HANDLE)_beginthreadex(NULL, 0, ThreadContents, this, 0, NULL);
	if (_hThreadContents == NULL)
	{
		_pNetServer->OnError(L"[Contents] An error occurred when starting the contents thread. error:%u\n", GetLastError());
		return false;
	}

	return true;
}

// 스레드 종료
void CContents::Shutdown()
{
	// shutdown 메시지 삽입
	MsgContents* pMsgShutdown = _pNetServer->AllocMsg();
	pMsgShutdown->type = EMsgContentsType::SHUTDOWN;
	pMsgShutdown->pSession = nullptr;
	pMsgShutdown->data = nullptr;
	InsertMessage(pMsgShutdown);

	// 스레드가 종료되면 _bTerminated = true로 설정됨
}

// 스레드 메시지큐에 메시지 삽입
void CContents::InsertMessage(MsgContents* pMsg)
{
	_msgQ.Enqueue(pMsg);
}

// 로직 실행시간을 FPS에 맞춘다. 수행못한 프레임은 버린다. 최대 FPS가 지정한 값으로 유지된다.
void CContents::SyncLogicTime()
{
	QueryPerformanceCounter(&_logicEndTime);
	__int64 spentTime = max(0, _logicEndTime.QuadPart - _logicStartTime.QuadPart);
	__int64 sleepTime = _oneFrameTime - spentTime;

	DWORD dwSleepTime = 0;
	// sleep 해야할 시간이 있는 경우
	if (sleepTime > 0)
	{
		// sleep 시간을 ms 단위로 변환하여 sleep 한다.
		dwSleepTime = (DWORD)round((double)(sleepTime * 1000) / (double)_performanceFrequency.QuadPart);
		Sleep(dwSleepTime);
	}

	// 다음 로직의 시작시간은 [현재 로직 종료시간 + sleep한 시간] 이다.
	_logicStartTime.QuadPart = _logicEndTime.QuadPart + (dwSleepTime * (_performanceFrequency.QuadPart / 1000));
}


// 로직 실행시간을 FPS에 맞춘다. 만약 수행못한 프레임이 있을 경우 한가할때 따라잡는다. 최대 FPS가 지정한 값보다 높아질 수 있다.
void CContents::SyncLogicTimeAndCatchUp()
{
	QueryPerformanceCounter(&_logicEndTime);
	__int64 spentTime = max(0, _logicEndTime.QuadPart - _logicStartTime.QuadPart);
	__int64 sleepTime = _oneFrameTime - spentTime;

	// sleep 해야할 시간이 있는 경우
	if (sleepTime > 0)
	{
		// 따라잡아야할 시간이 sleep 시간보다 크다면 따라잡아야할 시간에서 sleep 시간을 차감한다.
		if (_catchUpTime > sleepTime)
		{
			_catchUpTime -= sleepTime;
			sleepTime = 0;
		}
		// 따라잡아야할 시간이 sleep 시간보다 작다면 sleep 시간에서 따라잡아야할 시간을 차감한다.
		else
		{
			sleepTime -= _catchUpTime;
			_catchUpTime = 0;
		}
	}
	// 로직 수행시간이 1프레임당 소요시간보다 더 걸렸을 경우
	else
	{
		// 따라잡아야할 시간에 더 걸린 시간을 추가한다.
		_catchUpTime += -sleepTime;
		sleepTime = 0;
	}

	__int64 sleepTimeRemainder = 0;
	DWORD dwSleepTime = 0;
	// sleep 해야할 시간이 있는 경우
	if (sleepTime > 0)
	{
		// sleep 시간을 ms 단위로 절삭하고, 나머지는 따라잡아야할 시간에 추가한다.
		sleepTimeRemainder = sleepTime % (_performanceFrequency.QuadPart / 1000);  
		sleepTime -= sleepTimeRemainder;
		_catchUpTime += sleepTimeRemainder;

		if (sleepTime > 0)
		{
			// sleep 시간을 ms 단위로 변환하여 sleep 한다.
			dwSleepTime = (DWORD)(sleepTime * 1000 / _performanceFrequency.QuadPart);
			Sleep(dwSleepTime);
		}
	}

	// 다음 로직의 시작시간은 [현재 로직 종료시간 + sleep한 시간] 이다.
	// 이 때 0.5초 만큼의 count를 로직 시작시간에 더해주는 보정을 해야 FPS가 정확해진다(이유는 모름;;).
	_logicStartTime.QuadPart = _logicEndTime.QuadPart + sleepTime + (_performanceFrequency.QuadPart / 1000 / 2);
}


/* 세션 (private) */
// 세션의 연결을 끊는다. 세션에 대한 메시지는 더이상 처리되지 않는다.
void CContents::DisconnectSession(CSession* pSession)
{
	_pNetServer->Disconnect(pSession);
}

// 세션 내의 컨텐츠 패킷 벡터 내의 패킷을 전송한다.
void CContents::_sendContentsPacket(CSession* pSession)
{	const wchar_t* GetSessionIP();
	unsigned short GetSessionPort();
	size_t numPacket = pSession->_vecContentsPacket.size();
	_pNetServer->SendPacketAsync(pSession->_sessionId, pSession->_vecContentsPacket.data(), numPacket);
	for (size_t i = 0; i < numPacket; i++)
	{
		pSession->_vecContentsPacket[i]->SubUseCount();
	}
	pSession->_vecContentsPacket.clear();
}


// 세션 내의 컨텐츠 패킷 벡터 내의 패킷을 전송하고 연결을 끊는다.
void CContents::_sendContentsPacketAndDisconnect(CSession* pSession)
{
	size_t numPacket = pSession->_vecContentsPacket.size();
	_pNetServer->SendPacketAsyncAndDisconnect(pSession->_sessionId, pSession->_vecContentsPacket.data(), numPacket);
	for (size_t i = 0; i < numPacket; i++)
	{
		pSession->_vecContentsPacket[i]->SubUseCount();
	}
	pSession->_vecContentsPacket.clear();
}




/* 세션 */
const wchar_t* CContents::GetSessionIP(__int64 sessionId)
{
	auto iter = _mapSession.find(sessionId);
	if (iter == _mapSession.end())
		return nullptr;

	return (iter->second)->GetIP();
}

unsigned short CContents::GetSessionPort(__int64 sessionId)
{
	auto iter = _mapSession.find(sessionId);
	if (iter == _mapSession.end())
		return 0;

	return (iter->second)->GetPort();
}



// 세션을 destination 컨텐츠로 옮긴다. 세션에 대한 메시지는 현재 스레드에서는 더이상 처리되지 않는다.
void CContents::TransferSession(__int64 sessionId, CContents* destination, PVOID data)
{
	auto iter = _mapSession.find(sessionId);
	CSession* pSession = iter->second;
	pSession->_bOnTransfer = true;
	pSession->_pDestinationContents = destination;
	pSession->_pTransferData = data;
}


// 세션의 연결을 끊는다. 세션에 대한 메시지는 더이상 처리되지 않는다.
void CContents::DisconnectSession(__int64 sessionId)
{
	auto iter = _mapSession.find(sessionId);
	CSession* pSession = iter->second;
	_pNetServer->Disconnect(pSession);
}



/* packet */
CPacket* CContents::AllocPacket()
{ 
	return _pNetServer->AllocPacket(); 
}

bool CContents::SendPacket(__int64 sessionId, CPacket* pPacket)
{
	if (_mode & CONTENTS_MODE_SEND_PACKET_IMMEDIATELY)
	{
		_pNetServer->SendPacketAsync(sessionId, pPacket);
	}
	else
	{
		auto iter = _mapSession.find(sessionId);
		if (iter == _mapSession.end())
			return false;

		CSession* pSession = iter->second;
		pPacket->AddUseCount();
		pSession->_vecContentsPacket.push_back(pPacket);
	}

	return true;
}


bool CContents::SendPacket(__int64 sessionId, CPacket** arrPtrPacket, int numPacket)
{
	if (_mode & CONTENTS_MODE_SEND_PACKET_IMMEDIATELY)
	{
		_pNetServer->SendPacketAsync(sessionId, arrPtrPacket, numPacket);
	}
	else
	{
		auto iter = _mapSession.find(sessionId);
		if (iter == _mapSession.end())
			return false;

		CSession* pSession = iter->second;
		for (int i = 0; i < numPacket; i++)
		{
			arrPtrPacket[i]->AddUseCount();
			pSession->_vecContentsPacket.push_back(arrPtrPacket[i]);
		}
	}

	return true;
}

bool CContents::SendPacketAndDisconnect(__int64 sessionId, CPacket* pPacket)
{
	if (_mode & CONTENTS_MODE_SEND_PACKET_IMMEDIATELY)
	{
		_pNetServer->SendPacketAsyncAndDisconnect(sessionId, pPacket);
	}
	else
	{
		auto iter = _mapSession.find(sessionId);
		if (iter == _mapSession.end())
			return false;

		CSession* pSession = iter->second;
		pPacket->AddUseCount();
		pSession->_vecContentsPacket.push_back(pPacket);
		pSession->_bContentsWaitToDisconnect = true;
	}
	

	return true;
}




/* 하트비트 enable */
void CContents::EnableHeartBeatTimeout(int timeoutCriteria)
{
	_bEnableHeartBeatTimeout = true;
	_timeoutHeartBeat = timeoutCriteria;
	_timeoutHeartBeatCheckPeriod = timeoutCriteria / 2;
	_lastHeartBeatCheckTime = GetTickCount64();
}

void CContents::DisableHeartBeatTimeout()
{
	_bEnableHeartBeatTimeout = false;
}



/* Time */
double CContents::GetDT() const { return _pTimeMgr->GetDT(); }
float CContents::GetfDT() const { return _pTimeMgr->GetfDT(); }
double CContents::GetAvgDT1s() const { return _pTimeMgr->GetAvgDT1s(); }
double CContents::GetMinDT1st() const { return _pTimeMgr->GetMinDT1st(); }
double CContents::GetMinDT2nd() const { return _pTimeMgr->GetMinDT2nd(); }
double CContents::GetMaxDT1st() const { return _pTimeMgr->GetMaxDT1st(); }
double CContents::GetMaxDT2nd() const { return _pTimeMgr->GetMaxDT2nd(); }
int CContents::GetFPS() const { return _pTimeMgr->GetFPS(); }
float CContents::GetAvgFPS1m() const { return _pTimeMgr->GetAvgFPS1m(); }
int CContents::GetMinFPS1st() const { return _pTimeMgr->GetMinFPS1st(); }
int CContents::GetMinFPS2nd() const { return _pTimeMgr->GetMinFPS2nd(); }
int CContents::GetMaxFPS1st() const { return _pTimeMgr->GetMaxFPS1st(); }
int CContents::GetMaxFPS2nd() const { return _pTimeMgr->GetMaxFPS2nd(); }
bool CContents::OneSecondTimer() const { return _pTimeMgr->Is1sTimerSet(); }

/* dynamic alloc */
// 64byte aligned 객체 생성을 위한 new, delete overriding
void* CContents::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}

void CContents::operator delete(void* p)
{
	_aligned_free(p);
}


/* 스레드 */
// 컨텐츠 스레드 함수
unsigned WINAPI CContents::ThreadContents(PVOID pParam)
{
	wprintf(L"[Contents] Run Contents Thread. id:%u\n", GetCurrentThreadId());

	CContents& contents = *(CContents*)pParam;
	MsgContents* pMsg;
	CPacket* pRecvPacket;
	CSession* pSession;
	__int64 sessionId;

	// time manager 초기화
	contents._pTimeMgr->Init();
	contents._lastHeartBeatCheckTime = GetTickCount64();

	// 스레드 로직 시작
	QueryPerformanceCounter(&contents._logicStartTime);
	while (true)
	{
		// 메시지큐 내의 모든 메시지 처리
		while (contents._msgQ.Dequeue(pMsg))
		{
			switch (pMsg->type)
			{
			// 스레드에 최초 생성된 세션이 들어온 경우
			case EMsgContentsType::INITIAL_JOIN_SESSION:
			{
				pSession = pMsg->pSession;
				contents._mapSession.insert(std::make_pair(pSession->_sessionId, pSession));
				contents.OnSessionJoin(pSession->_sessionId, nullptr);  // 이 함수내에서 플레이어를 생성하고 플레이어맵에 등록한다.
				break;
			}
			// 스레드에 세션이 들어온 경우
			// 세션을 map에 추가하고, OnSessionJoin 함수를 호출한다. 이 때 플레이어 객체 주소도 같이 전달한다.
			case EMsgContentsType::JOIN_SESSION:
			{
				pSession = pMsg->pSession;
				pSession->_bOnTransfer = false;
				contents._mapSession.insert(std::make_pair(pSession->_sessionId, pSession));
				contents.OnSessionJoin(pSession->_sessionId, pMsg->data);  // 이 함수내에서 플레이어를 플레이어맵에 등록한다.
				break;
			}
			// 스레드 종료 메시지를 받음
			case EMsgContentsType::SHUTDOWN:
			{
				// 모든 세션의 연결을 끊는다.
				int disconnCount = 0;
				for (auto iter = contents._mapSession.begin(); iter != contents._mapSession.end(); ++iter)
				{
					pSession = iter->second;
					sessionId = pSession->_sessionId;
					contents.DisconnectSession(pSession);
					contents._pNetServer->DecreaseIoCount(pSession);
					contents.OnSessionDisconnected(sessionId);
					disconnCount++;
					if (disconnCount > 100)
					{
						Sleep(50);
						disconnCount = 0;
					}
				}
				contents._mapSession.clear();
				contents._pNetServer->_poolMsgContents.Free(pMsg);

				// 메시지큐의 모든 메시지를 삭제한다.
				MsgContents* pMsg;
				while (contents._msgQ.Dequeue(pMsg))
				{
					contents._pNetServer->_poolMsgContents.Free(pMsg);
				}

				// 스레드 종료
				contents._bTerminated = true;
				wprintf(L"[Contents] End Contents Thread. id:%u\n", GetCurrentThreadId());
				return 0;
				break;
			}
			// 잘못된 스레드 메시지를 받은 경우 오류메시지 출력
			default:
			{
				contents._pNetServer->OnError(L"[Contents] Contents thread got a invalid internal message. type:%d, session:%p, sessionId:%lld, data:%p\n"
					, pMsg->type, pMsg->pSession, pMsg->pSession == nullptr ? 0 : pMsg->pSession->_sessionId, pMsg->data);
				break;
			}
			}

			// 메시지 free
			contents._pNetServer->_poolMsgContents.Free(pMsg);
			contents._internalMsgCount++;  // 모니터링
		}

		// 모든 세션내의 모든 메시지 처리
		bool isSessionClosed;
		bool isSessionOnTransfer;
		bool isSessionReceivedMsg;
		for (auto iter = contents._mapSession.begin(); iter != contents._mapSession.end();)
		{
			isSessionClosed = false;
			isSessionOnTransfer = false;
			isSessionReceivedMsg = false;
			do {
				pSession = iter->second;
				// 네트워크 라이브러리에서, 또는 이전 OnUpdate 함수내에서 세션을 끊음
				if (pSession->_isClosed == true)
				{
					isSessionClosed = true;
					break;
				}
				// 이전 OnUpdate 함수내에서 세션을 이동시켰음
				if (pSession->_bOnTransfer == true)
				{
					isSessionOnTransfer = true;
					break;
				}

				// 세션내의 모든 메시지 처리
				while (pSession->_msgQ->Dequeue(pRecvPacket))
				{
					isSessionReceivedMsg = true;
					contents.OnRecv(pSession->_sessionId, pRecvPacket);
					pRecvPacket->SubUseCount();  // 패킷의 사용카운트를 감소시킨다.
					contents._sessionMsgCount++;  // 모니터링
					// OnRecv 함수 내에서 세션을 이동시킴
					if (pSession->_bOnTransfer == true)
					{
						isSessionOnTransfer = true;
						break;
					}
					// OnRecv 함수 내에서 DisconnectSession 함수가 호출됨, 또는 네트워크 라이브러리에서 세션을 끊음
					if (pSession->_isClosed == true)
					{
						isSessionClosed = true;
						break;
					}
				}

				// 세션이 받은 메시지가 있을 경우 하트비트 설정
				if (isSessionReceivedMsg == true)
				{
					pSession->SetHeartBeatTime();
				}
			} while (0);

			// 세션 이동이 호출되었을 세션을 map에서 제거하고, 목적지 contents에 세션 join 메시지를 보낸다.
			// (사용자가 세션을 이동시켰다면 플레이어 객체를 map에서 제거했을 것이다. 하지만 플레이어 객체가 아직 free되지는 않았다.)
			// (플레이어 객체가 free되는것을 보장하기 위해 사용자가 세션 이동을 요청했다면 반드시 이동되어야 한다. 그래서 세션 연결 끊김보다 이동요청을 먼저 처리해야 한다.)
			// (만약 플레이어 객체를 map에서 제거했는데 플레이어 객체가 이동되지 않고 OnSessionDisconnected 함수가 호출된다면 플레이어 객체는 free되지 않을 것이다.)
			if (isSessionOnTransfer == true)
			{
				if (!(contents._mode & CONTENTS_MODE_SEND_PACKET_IMMEDIATELY))
				{
					// 패킷 즉시 전송 모드가 아닐 경우 세션 패킷버퍼내의 패킷을 모두 전송한다.
					contents._sendContentsPacket(pSession);
				}

				iter = contents._mapSession.erase(iter);

				MsgContents* pMsg = contents._pNetServer->_poolMsgContents.Alloc();
				pMsg->type = EMsgContentsType::JOIN_SESSION;
				pMsg->pSession = pSession;
				pMsg->data = pSession->_pTransferData;
				pSession->_pDestinationContents->InsertMessage(pMsg);
			}
			// 세션 끊김이 감지되었을 경우 세션을 map에서 제거하고 IoCount를 감소시킨다. 그런다음 OnSessionDisconnected를 호출한다.
			else if (isSessionClosed == true)
			{
				iter = contents._mapSession.erase(iter);
				sessionId = pSession->_sessionId;
				contents._pNetServer->DecreaseIoCount(pSession);
				contents.OnSessionDisconnected(sessionId);
			}
			else
			{
				++iter;
			}
		}

		// 하트비트 타임아웃 체크(_bEnableHeartBeatTimeout == true 일 경우)
		ULONGLONG currentTime = GetTickCount64();
		if (contents._bEnableHeartBeatTimeout == true
			&& currentTime > contents._lastHeartBeatCheckTime + contents._timeoutHeartBeatCheckPeriod)
		{
			contents._lastHeartBeatCheckTime = currentTime;
			for (auto iter = contents._mapSession.begin(); iter != contents._mapSession.end();)
			{
				pSession = iter->second;
				if (pSession->_lastHeartBeatTime + contents._timeoutHeartBeat < currentTime)
				{
					// 하트비트 타임아웃 대상일 경우 세션을 map에서 제거하고 연결을 끊는다.
					iter = contents._mapSession.erase(iter);
					sessionId = pSession->_sessionId;
					contents.DisconnectSession(pSession);
					contents._pNetServer->DecreaseIoCount(pSession);
					contents.OnSessionDisconnected(sessionId);
					contents._disconnByHeartBeat++; // 모니터링
				}
				else
				{
					++iter;
				}
			}
		}

		// OnUpdate
		contents._updateCount++;    // 모니터링
		contents.OnUpdate();

		// 패킷 즉시전송 모드가 아닐 경우
		if (!(contents._mode & CONTENTS_MODE_SEND_PACKET_IMMEDIATELY))
		{
			// 컨텐츠 코드에서 전송 요청한 패킷을 전송한다.
			for (auto iter = contents._mapSession.begin(); iter != contents._mapSession.end(); ++iter)
			{
				pSession = iter->second;

				// 만약 세션에대해 보내고끊기 함수가 호출되었다면 네트워크의 보내고끊기 함수를 호출한다.
				// 보낼 패킷이 없어도 연결이 끊겨야 하기 때문에 보내고끊기 함수를 반드시 호출한다.
				if (pSession->_bContentsWaitToDisconnect == true)
				{
					contents._sendContentsPacketAndDisconnect(pSession);
				}
				// 세션의 패킷을 모두 전송한다.
				else if (pSession->_vecContentsPacket.size() > 0)
				{
					contents._sendContentsPacket(pSession);
				}
			}
		}

		// 로직 실행시간을 FPS에 맞추기 위해 sleep한다.
		contents.SyncLogicTime();

		// time manager update
		contents._pTimeMgr->Update();
	}


	wprintf(L"[Contents] End Contents Thread. id:%u\n", GetCurrentThreadId());

	return 0;
}






