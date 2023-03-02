/*
게임 서버용 네트워크 라이브러리 사용
인증스레드1, 필드스레드1
*/

#include "CGameServer.h"
#include "CGameContents.h"
#include "CGameContentsField.h"

#include <string>
#include <sstream>

#include "profiler.h"
#include "CCrashDump.h"
#include "logger.h"
#include "CCpuUsage.h"
#include "CPDH.h"

int main()
{
	CCrashDump::Init();
	timeBeginPeriod(1);
	CCpuUsage CPUTime;

	CPDH pdh;
	pdh.Init();

	CGameServer& gameServer = *new CGameServer();
	bool retStart = gameServer.StartUp();
	if (retStart == false)
	{
		wprintf(L"[main] failed to start game server\n");
		return 0;
	}

	int numWorkerThread = gameServer.GetNumWorkerThread();
	LARGE_INTEGER liFrequency;
	QueryPerformanceFrequency(&liFrequency);
	ULONGLONG tick = GetTickCount64();
	__int64 whileCount = 0;
	__int64 prevAcceptCount = 0;
	__int64 prevConnectCount = 0;
	__int64 prevDisconnectCount = 0;
	__int64 prevRecvCount = 0;
	__int64 prevSendCount = 0;
	__int64 prevRecvCompletionCount = 0;
	__int64 prevSendCompletionCount = 0;
	__int64 prevMsgHandleCount = 0;
	__int64 prevQueryRunCount = 0;
	__int64 prevAuthClientMsgCount = 0;
	__int64 prevAuthInternalMsgCount = 0;
	__int64 prevAuthQueryCount = 0;
	__int64 prevFieldClientMsgCount = 0;
	__int64 prevFieldInternalMsgCount = 0;
	__int64 prevFieldQueryCount = 0;
	int* arrPrevGQCSWaitTime = new int[numWorkerThread];
	int* arrCurrGQCSWaitTime = new int[numWorkerThread];

	Sleep(1000);
	while (true)
	{
		Sleep(50);

		if (GetConsoleWindow() == GetForegroundWindow())
		{
			if (GetAsyncKeyState('E') || GetAsyncKeyState('e'))
			{
				printf("[main] terminate game server\n");
				gameServer.Shutdown();
				while (gameServer.IsTerminated() == false)
					continue;
				break;
			}
			else if (GetAsyncKeyState('C') || GetAsyncKeyState('c'))
			{
				int* p = 0;
				*p = 0;
				break;
			}
			else if (GetAsyncKeyState('P') || GetAsyncKeyState('p'))
			{
				profiler::ResetProfilingData();
				wprintf(L"[main] reset profiler\n");
			}
			else if (GetAsyncKeyState('O') || GetAsyncKeyState('o'))
			{
				profiler::OutputProfilingData();
				printf("[main] output profiler\n");
			}
		}

		// 1초마다 로그 출력
		if (GetTickCount64() - tick < 1000)
			continue;
		tick += 1000;
		CPUTime.UpdateCpuTime();
		pdh.Update();


		__int64 currAcceptCount = gameServer.GetAcceptCount();                     // accept 횟수
		__int64 currConnectCount = gameServer.GetConnectCount();                   // connect 횟수 (accept 후 connect 승인된 횟수)
		__int64 currDisconnectCount = gameServer.GetDisconnectCount();             // disconnect 횟수 (세션 release 횟수)
		__int64 currRecvCount = gameServer.GetRecvCount();                         // WSARecv 함수 호출 횟수      
		__int64 currSendCount = gameServer.GetSendCount();                         // WSASend 함수 호출 횟수
		__int64 currRecvCompletionCount = gameServer.GetRecvCompletionCount();     // recv 완료통지 처리횟수
		__int64 currSendCompletionCount = gameServer.GetSendCompletionCount();     // send 완료통지 처리횟수
		__int64 currMsgHandleCount = gameServer.GetContentsSessionMsgCount();      // 컨텐츠 스레드 세션 메시지 처리 횟수
		//__int64 currQueryRunCount = gameServer.GetQueryRunCount();             // 쿼리 실행횟수
		gameServer.GetArrGQCSWaitTime(arrCurrGQCSWaitTime);                        // worker스레드 GQCS wait time 

		const CGameContents* pAuth = gameServer.GetGameContentsPtr(EContentsThread::AUTH);
		const CGameContents* pField = gameServer.GetGameContentsPtr(EContentsThread::FIELD);
		__int64 currAuthClientMsgCount = pAuth->GetSessionMsgCount();
		__int64 currAuthInternalMsgCount = pAuth->GetInternalMsgCount();
		__int64 currAuthQueryCount = pAuth->GetQueryRunCount();
		__int64 currFieldClientMsgCount = pField->GetSessionMsgCount();
		__int64 currFieldInternalMsgCount = pField->GetInternalMsgCount();
		__int64 currFieldQueryCount = pField->GetQueryRunCount();

		// 현재 세션 수, WSASend 호출횟수, WSARecv 호출횟수, recv 완료통지 처리 횟수, send 완료통지 처리횟수 출력
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [network]  session:%d,  accept:%lld (%lld/s),  conn:%lld (%lld/s),  disconn:%lld (%lld/s),  recv:%lld/s,  send:%lld/s,  recvComp:%lld/s,  sendComp:%lld/s\n"
			, whileCount, gameServer.GetNumSession()
			, currAcceptCount, currAcceptCount - prevAcceptCount
			, currConnectCount, currConnectCount - prevConnectCount
			, currDisconnectCount, currDisconnectCount - prevDisconnectCount
			, currRecvCount - prevRecvCount, currSendCount - prevSendCount
			, currRecvCompletionCount - prevRecvCompletionCount, currSendCompletionCount - prevSendCompletionCount);

		// 게임서버
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [game  ]  CPU usage [T:%.1f%%%%, U:%.1f%%%%, K:%.1f%%%%]  [Server:%.1f%%%%, U:%.1f%%%%, K:%.1f%%%%]\n"
			, whileCount
			, CPUTime.ProcessorTotal(), CPUTime.ProcessorKernel(), CPUTime.ProcessorUser(), CPUTime.ProcessTotal(), CPUTime.ProcessKernel(), CPUTime.ProcessUser());

		// 인증컨텐츠
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [auth  ]  player:%d, msg proc:%lld/s (internal:%lld/s),  query: (run:%d/s, time:(avg:%.1fms , max:%.1fms , min:%.1fms)),  FPS:%d (avg1m:%d, max:%d, min:%d), DT:%.3f (max:%.3f, min:%.3f)\n"
			, whileCount
			, pAuth->GetNumPlayer(), currAuthClientMsgCount - prevAuthClientMsgCount, currAuthInternalMsgCount - prevAuthInternalMsgCount
			, currAuthQueryCount - prevAuthQueryCount, pAuth->GetAvgQueryRunTime(), pAuth->GetMaxQueryRunTime(), pAuth->GetMinQueryRunTime()
			, pAuth->GetFPS(), (int)pAuth->GetAvgFPS1m(), pAuth->GetMaxFPS1st(), pAuth->GetMinFPS1st()
			, pAuth->GetAvgDT1s(), pAuth->GetMaxDT1st(), pAuth->GetMinDT1st());

		// 필드컨텐츠
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [field ]  player:%d, msg proc:%lld/s (internal:%lld/s), query: (run:%d/s, remain:%d, time:(avg:%.1fms , max:%.1fms , min:%.1fms)),  FPS:%d (avg1m:%d, max:%d, min:%d), DT:%.3f (max:%.3f, min:%.3f)\n"
			, whileCount
			, pField->GetNumPlayer(), currFieldClientMsgCount - prevFieldClientMsgCount, currFieldInternalMsgCount - prevFieldInternalMsgCount
			, currFieldQueryCount - prevFieldQueryCount, pField->GetRemainQueryCount(), pField->GetAvgQueryRunTime(), pField->GetMaxQueryRunTime(), pField->GetMinQueryRunTime()
			, pField->GetFPS(), (int)pField->GetAvgFPS1m(), pField->GetMaxFPS1st(), pField->GetMinFPS1st()
			, pField->GetAvgDT1s(), pField->GetMaxDT1st(), pField->GetMinDT1st());

		// 연결끊김 사유
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [disconn]  sess lim:%lld,  iocp assoc:%lld,  recv io err (known:%lld,  121:%lld,  unknown:%lld),  by normal:%lld"
			",  packet (code:%lld,  len:%lld,  decode:%lld)\n"
			, whileCount, gameServer.GetDisconnBySessionLimit(), gameServer.GetDisconnByIOCPAssociation()
			, gameServer.GetDisconnByKnownIoError(), gameServer.GetDisconnBy121RecvIoError(), gameServer.GetDisconnByUnknownIoError(), gameServer.GetDisconnByNormalProcess()
			, gameServer.GetDisconnByPacketCode(), gameServer.GetDisconnByPacketLength(), gameServer.GetDisconnByPacketDecode());

		LOGGING(LOGGING_LEVEL_INFO, L"%lld [disconn]  plyr lim:%lld,  invalid msg (auth:%lld, field:%lld),  heart beat timeout (auth:%lld, field:%lld)\n"
			, whileCount
			, 0//gameServer._disconnByPlayerLimit
			, pAuth->GetDisconnByInvalidMsgType(), pField->GetDisconnByInvalidMsgType()
			, pAuth->GetDisconnByHeartBeat(), pField->GetDisconnByHeartBeat());

		// 메모리풀. 패킷, 메시지, 플레이어, field컨텐츠 DB
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [pool   ]  packet:%d (alloc:%d, used:%d, free:%d),  message:%d (alloc:%d, used:%d, free:%d),  player:%d (alloc:%d, used:%d, free:%d),  field DB:%d (alloc:%d, free:%d)\n"
			, whileCount
			, gameServer.GetPacketPoolSize(), gameServer.GetPacketAllocCount(), gameServer.GetPacketActualAllocCount(), gameServer.GetPacketFreeCount()
			, gameServer.GetMsgPoolSize(), gameServer.GetMsgAllocCount(), gameServer.GetMsgActualAllocCount(), gameServer.GetMsgFreeCount()
			, gameServer.GetPlayerPoolSize(), gameServer.GetPlayerAllocCount(), gameServer.GetPlayerActualAllocCount(), gameServer.GetPlayerFreeCount()
			, ((CGameContentsField*)pField)->GetQueryRequestPoolSize(), ((CGameContentsField*)pField)->GetQueryRequestAllocCount(), ((CGameContentsField*)pField)->GetQueryRequestFreeCount());

		// DB
		//LOGGING(LOGGING_LEVEL_INFO, L"%lld [DB     ]  query count:%d/s,  remain query:%d,  query run time (avg:%.1fms , max:%.1fms , min:%.1fms)\n\n"
		//	, whileCount, currQueryRunCount - prevQueryRunCount
		//	, gameServer.GetUnprocessedQueryCount(), gameServer.GetAvgQueryRunTime(), gameServer.GetMaxQueryRunTime()
		//	, gameServer.GetMinQueryRunTime() == FLT_MAX ? 0.f : gameServer.GetMinQueryRunTime());

		// 시스템
		const PDHCount& pdhCount = pdh.GetPDHCount();
		LOGGING(LOGGING_LEVEL_INFO, L"%lld [system ]  TCP (segment:%lld/s, retrans:%lld/s, recv:%.2fMB/s, send:%.2fMB/s),  memory(MB) [System C:%.2f, P:%.2f, NP:%.2f] [Process C:%.2f, P:%.2f, NP:%.2f]\n"
			, whileCount
			, pdhCount.TCPSegmentsSent, pdhCount.TCPSegmentsRetransmitted, pdhCount.networkRecvBytes / 1048576.0, pdhCount.networkSendBytes / 1048576.0
			, (double)pdhCount.systemComittedBytes / 1048576.0, (double)pdhCount.systemPagedPoolBytes / 1048576.0, (double)pdhCount.systemNonpagedPoolBytes / 1048576.0
			, (double)pdhCount.processPrivateBytes / 1048576.0, (double)pdhCount.processPagedPoolBytes / 1048576.0, (double)pdhCount.processNonpagedPoolBytes / 1048576.0);

		// worker 스레드별 GQCS wait 시간, 트래픽 혼잡제어 횟수
		std::wostringstream oss;
		oss.str(L"");
		oss << whileCount << L" [etc    ]  GQCS wait time(ms): (";
		for (int i = 0; i < numWorkerThread; i++)
		{
			if (arrCurrGQCSWaitTime[i] - arrPrevGQCSWaitTime[i] == 0)
				oss << L"?, ";
			else
				oss << arrCurrGQCSWaitTime[i] - arrPrevGQCSWaitTime[i] << L", ";
		}
		oss.seekp(-2, std::ios_base::end); // 마지막 ", " 문자열 제거
		oss << L"),  traffic control:" << gameServer.GetTrafficCongestionControlCount();
		oss << L",  error:" << gameServer.GetOtherErrorCount() << L"\n\n";
		LOGGING(LOGGING_LEVEL_INFO, oss.str().c_str());


		prevAcceptCount = currAcceptCount;
		prevConnectCount = currConnectCount;
		prevDisconnectCount = currDisconnectCount;
		prevRecvCount = currRecvCount;
		prevSendCount = currSendCount;
		prevRecvCompletionCount = currRecvCompletionCount;
		prevSendCompletionCount = currSendCompletionCount;
		prevMsgHandleCount = currMsgHandleCount;
		//prevQueryRunCount = currQueryRunCount;
		prevAuthClientMsgCount = currAuthClientMsgCount;
		prevAuthInternalMsgCount = currAuthInternalMsgCount;
		prevAuthQueryCount = currAuthQueryCount;
		prevFieldClientMsgCount = currFieldClientMsgCount;
		prevFieldInternalMsgCount = currFieldInternalMsgCount;
		prevFieldQueryCount = currFieldQueryCount;
		memcpy(arrPrevGQCSWaitTime, arrCurrGQCSWaitTime, sizeof(int) * numWorkerThread);

		whileCount++;
	}

}
