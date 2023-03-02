#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include "CNetServer.h"
#include "CSession.h"

#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

using namespace netserver;

// worker 스레드 내부의 TLS 모니터링값 증가 매크로
#define INCREASE_WORKER_TLS_MONITOR(element)    _arrWorkerTlsMonitor[_stlsTlsIndex].element++;
#define ADD_WORKER_TLS_MONITOR(element, value)  _arrWorkerTlsMonitor[_stlsTlsIndex].element += value;

CNetServer::CNetServer()
	: _hIOCP(NULL), _listenSock(NULL)
	, _szBindIP{}, _bindIP(0)
	, _portNumber(0), _numConcurrentThread(0), _numWorkerThread(0)
	, _numMaxSession(0), _bUseNagle(false)
	, _arrSession(nullptr)
	, _packetCode(0), _packetKey(0), _maxPacketSize(0)
	, _bUsePacketEncryption(true), _bUseTrafficCongestionControl(false)
	, _bTrafficCongestion(false)
	, _hThreadAccept(NULL), _idThreadAccept(0), _hThreadTrafficCongestionControl(NULL), _idThreadTrafficCongestionControl(0)
	, _hThreadDeferredCloseSocket(NULL), _idThreadDeferredCloseSocket(0)
	, _hEventDeferredCloseSocket(NULL), _bEventSetDeferredCloseSocket(false)
	, _bShutdown(false)
	, _arrWorkerTlsMonitor(nullptr)
	, _bOutputDebug(false), _bOutputSystem(true)
{

	QueryPerformanceFrequency(&_liPerformanceFrequency);

#ifdef NET_ENABLE_MEMORY_LOGGING
	_sizeLogBuffer = 0xfffff;  // 로그 버퍼 크기. 2진수 값이 모두 1로 이루어져야 함
	_logBuffer = (MemLog*)malloc(sizeof(MemLog) * (_sizeLogBuffer + 1));
	memset(_logBuffer, 0, sizeof(MemLog)* (_sizeLogBuffer + 1));
	_logBufferIndex = -1;
#endif
}

CNetServer::~CNetServer()
{

}

/* server */
bool CNetServer::StartUp(const wchar_t* bindIP, unsigned short port, int numConcurrentThread, int numWorkerThread, bool bUseNagle, int numMaxSession, BYTE packetCode, BYTE packetKey, int maxPacketSize, bool bUsePacketEncryption, bool bUseTrafficCongestionControl)
{
	timeBeginPeriod(1);

	INT retPton = InetPton(AF_INET, bindIP, &_bindIP);
	if (retPton != 1)
	{
		OnError(L"[Network] Server IP is not valid. error:%u\n", WSAGetLastError());
		return false;
	}
	wcscpy_s(_szBindIP, wcslen(bindIP) + 1, bindIP);
	
	_portNumber = port;
	_numConcurrentThread = numConcurrentThread;
	_numWorkerThread = numWorkerThread;
	_bUseNagle = bUseNagle;
	_numMaxSession = numMaxSession;
	_packetCode = packetCode;
	_packetKey = packetKey;
	_maxPacketSize = maxPacketSize;
	_bUsePacketEncryption = bUsePacketEncryption;
	_bUseTrafficCongestionControl = bUseTrafficCongestionControl;

	
	// worker 스레드 내부 TLS 모니터링을 위한 모니터링객체 배열 생성
	_arrWorkerTlsMonitor = new _StWorkerTlsMonitor[numWorkerThread];


	// 세션 생성
	_arrSession = (CSession*)_aligned_malloc(sizeof(CSession) * _numMaxSession, 64);
	for (int i = 0; i < _numMaxSession; i++)
	{
		new (_arrSession + i) CSession(i);
	}

	for (int i = _numMaxSession - 1; i >= 0; i--)
	{
		_stackSessionIdx.Push(i);
	}


	// 메모리 정렬 체크
	if ((unsigned long long)this % 64 != 0)
		OnError(L"[Network] network object is not aligned as 64\n");
	for (int i = 0; i < _numMaxSession; i++)
	{
		if ((unsigned long long) & _arrSession[i] % 64 != 0)
			OnError(L"[Network] %d'th session is not aligned as 64\n", i);
	}
	if ((unsigned long long) & _stackSessionIdx % 64 != 0)
		OnError(L"[Network] session index stack is not aligned as 64\n");
	if ((unsigned long long) & _sendCount % 64 != 0)
		OnError(L"[Network] sendCount is not aligned as 64\n");
	if ((unsigned long long) & _sendAsyncCount % 64 != 0)
		OnError(L"[Network] sendAsyncCount is not aligned as 64\n");



	// WSAStartup
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		OnError(L"[Network] Failed to initiate Winsock DLL. error:%u\n", WSAGetLastError());
		return false;
	}

	// IOCP 생성
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, _numConcurrentThread);
	if (_hIOCP == NULL)
	{
		OnError(L"[Network] Failed to create IOCP. error:%u\n", GetLastError());
		return false;
	}

	// 리슨소켓 생성
	_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// SO_LINGER 옵션 설정
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	setsockopt(_listenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	// nagle 옵션 해제 여부
	if (_bUseNagle == false)
	{
		DWORD optval = TRUE;
		int retSockopt = setsockopt(_listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
		if (retSockopt == SOCKET_ERROR)
		{
			OnError(L"[Network] Failed to set TCP_NODELAY option on listen socket. error:%u\n", WSAGetLastError());
		}
	}

	// bind
	SOCKADDR_IN addrServ;
	ZeroMemory(&addrServ, sizeof(SOCKADDR_IN));
	addrServ.sin_family = AF_INET;
	addrServ.sin_addr.s_addr = _bindIP; //htonl(_bindIP);  InetPton 함수에서 미리 network order로 변환하여 반환해주기 때문에 htonl 함수를 사용할 필요가 없음
	addrServ.sin_port = htons(_portNumber);
	if (bind(_listenSock, (SOCKADDR*)&addrServ, sizeof(addrServ)) == SOCKET_ERROR)
	{
		OnError(L"[Network] Listen socket binding error. error:%u\n", WSAGetLastError());
		return false;
	}

	if (listen(_listenSock, SOMAXCONN_HINT(_numMaxSession)) == SOCKET_ERROR)
	{
		OnError(L"[Network] Failed to start listening. error:%u\n", WSAGetLastError());
		return false;
	}


	// worker 스레드 생성
	unsigned int threadId;
	HANDLE hThread;
	for (int i = 0; i < _numWorkerThread; i++)
	{
		hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadWorker, (PVOID)this, 0, &threadId);
		if (hThread == NULL)
		{
			OnError(L"[Network] An error occurred when starting the worker thread. error:%u\n", GetLastError());
			continue;
		}
		_vecHThreadWorker.push_back(hThread);
		_vecIdThreadWorker.push_back(threadId);
	}

	// 트래픽 혼잡제어 스레드 생성
	if (_bUseTrafficCongestionControl == true)
	{
		_hThreadTrafficCongestionControl = (HANDLE)_beginthreadex(NULL, 0, ThreadTrafficCongestionControl, (PVOID)this, 0, &_idThreadTrafficCongestionControl);
		if (_hThreadTrafficCongestionControl == NULL)
		{
			OnError(L"[Network] An error occurred when starting the traffic congestion control thread. error:%u\n", GetLastError());
			return false;
		}
	}

	// 지연된 소켓닫기 이벤트 생성
	_hEventDeferredCloseSocket = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (_hEventDeferredCloseSocket == NULL)
	{
		wprintf(L"[Network] failed to create event for deferred close socket thread, error:%d\n", GetLastError());
		return false;
	}
	// 지연된 소켓닫기 스레드 생성
	_hThreadDeferredCloseSocket = (HANDLE)_beginthreadex(NULL, 0, ThreadDeferredCloseSocket, (PVOID)this, 0, &_idThreadDeferredCloseSocket);
	if (_hThreadDeferredCloseSocket == NULL)
	{
		OnError(L"[Network] An error occurred when starting the deferred close socket thread. error:%u\n", GetLastError());
		return false;
	}


	// accept 스레드 생성
	_hThreadAccept = (HANDLE)_beginthreadex(NULL, 0, ThreadAccept, (PVOID)this, 0, &_idThreadAccept);
	if (_hThreadAccept == NULL)
	{
		OnError(L"[Network] An error occurred when starting the accept thread. error:%u\n", GetLastError());
		return false;
	}


	return true;
}




// accept 종료
void CNetServer::StopAccept()
{
	if (_listenSock != INVALID_SOCKET)
		closesocket(_listenSock);
	_listenSock = INVALID_SOCKET;
}


// 서버 종료(accept 종료 포함)
void CNetServer::Shutdown()
{
	_bShutdown = true;

	// accept 스레드를 종료시킨다.
	StopAccept();


	// 모든 소켓을 닫는다.
	// 세션의 반환은 worker 스레드에서 수행될 것임.
	for (int i=0; i<_numMaxSession; i++)
	{
		CSession* pSession = &_arrSession[i];
		if(pSession->_sock != INVALID_SOCKET)
			CancelIoEx((HANDLE)pSession->_sock, NULL);
	}
	Sleep(1000);  // IO 실패 완료통지가 모두 처리되어 모든 세션의 연결이 끊길동안 잠시 기다림

	// worker 스레드 종료 메시지를 보낸다.
	for (int i = 0; i < _numWorkerThread; i++)
	{
		PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);
	}

	// 모든 worker 스레드가 종료되기를 10초간 기다린다.
	ULONGLONG timeout = 10000;
	ULONGLONG tick;
	DWORD retWait;
	for (int i = 0; i < _vecHThreadWorker.size(); i++)
	{
		tick = GetTickCount64();
		retWait = WaitForSingleObject(_vecHThreadWorker[i], (DWORD)timeout);
		if (retWait != WAIT_OBJECT_0)
		{
			NET_OUTPUT_SYSTEM(L"[Network] Timeout occurred while waiting for the thread to be terminated. force terminate it. error:%u\n", GetLastError());
			TerminateThread(_vecHThreadWorker[i], 0);
		}
		
		timeout -= GetTickCount64() - tick;
	}

	// 객체 삭제
	delete[] _arrWorkerTlsMonitor;
	for (int i = 0; i < _numMaxSession; i++)
		(_arrSession[i]).~CSession();
	unsigned short pop;
	while (_stackSessionIdx.Pop(pop) == false);
	_aligned_free(_arrSession);
	CloseHandle(_hThreadAccept);
	if(_bUseTrafficCongestionControl == true)
		CloseHandle(_hThreadTrafficCongestionControl);
	CloseHandle(_hThreadDeferredCloseSocket);
	for (int i = 0; i < _vecHThreadWorker.size(); i++)
		CloseHandle(_vecHThreadWorker[i]);
	_vecHThreadWorker.clear();
	_vecIdThreadWorker.clear();
	CloseHandle(_hIOCP);
	// WSACleanup 은 다른 네트워크에 영향을 미칠수 있기 때문에 호출하지 않음.

}


/* packet */
CPacket* CNetServer::AllocPacket()
{
	CPacket* pPacket = CPacket::AllocPacket();
	pPacket->Init(sizeof(PacketHeader));
	return pPacket;
}


bool CNetServer::SendPacket(__int64 sessionId, CPacket* pPacket)
{
	CSession* pSession = FindSession(sessionId);  // increase IO count
	if (pSession == nullptr)
		return false;

	PacketHeader header;
	if (pPacket->IsHeaderSet() == false)
	{
		// 헤더 생성
		header.code = _packetCode;
		header.len = pPacket->GetDataSize();
		header.randKey = (BYTE)rand();

		char* packetDataPtr;
		if (_bUsePacketEncryption == true)
		{
			packetDataPtr = pPacket->GetDataPtr();  // CheckSum 계산
			header.checkSum = 0;
			for (int i = 0; i < header.len; i++)
				header.checkSum += packetDataPtr[i];
		}
		// 직렬화버퍼에 헤더 입력
		pPacket->PutHeader((char*)&header);
	}

	// 패킷 암호화
	if (_bUsePacketEncryption == true)
		EncryptPacket(pPacket);

	// send lcokfree queue에 직렬화버퍼 입력
	pPacket->AddUseCount();
	pSession->_sendQ->Enqueue(pPacket);

	// Send를 시도함
	SendPost(pSession);


	DecreaseIoCount(pSession);
	return true;
}




bool CNetServer::SendPacketAsync(__int64 sessionId, CPacket* pPacket)
{
	CSession* pSession = FindSession(sessionId);  // increase IO count
	if (pSession == nullptr)
		return false;

	PacketHeader header;
	if (pPacket->IsHeaderSet() == false)
	{
		// 헤더 생성
		header.code = _packetCode;
		header.len = pPacket->GetDataSize();
		header.randKey = (BYTE)rand();

		char* packetDataPtr;
		if (_bUsePacketEncryption == true)
		{
			packetDataPtr = pPacket->GetDataPtr();  // CheckSum 계산
			header.checkSum = 0;
			for (int i = 0; i < header.len; i++)
				header.checkSum += packetDataPtr[i];
		}
		// 직렬화버퍼에 헤더 입력
		pPacket->PutHeader((char*)&header);
	}

	// 패킷 암호화
	if (_bUsePacketEncryption == true)
		EncryptPacket(pPacket);

	// send lcokfree queue에 직렬화버퍼 입력
	pPacket->AddUseCount();
	pSession->_sendQ->Enqueue(pPacket);


	// 현재 send가 진행중인지 확인
	if (pSession->_sendQ->Size() == 0) // 보낼 패킷이 없으면 종료
	{
		DecreaseIoCount(pSession);
		return true;
	}
	else if ((bool)InterlockedExchange8((char*)&pSession->_sendIoFlag, true) == true) // 내가 sendIoFlag를 false에서 true로 바꾸었을 때만 send함
	{
		DecreaseIoCount(pSession);
		return true;
	}

	// 현재 send가 진행중이 아니라면 SendPost 요청을 IOCP 큐에 삽입함
	BOOL retPost = PostQueuedCompletionStatus(_hIOCP, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)2);
	if (retPost == 0)
	{
		OnError(L"[Network] Failed to post send request to IOCP. error:%u\n", GetLastError());
		pSession->_bError = true;
		CloseSocket(pSession);
		pSession->_sendIoFlag = false;
		DecreaseIoCount(pSession);
		InterlockedIncrement64(&_otherErrors);
		return false;
	}
	
	return true;
}


// send가 완료된 후 연결을 끊는다.
bool CNetServer::SendPacketAndDisconnect(__int64 sessionId, CPacket* pPacket)
{
	CSession* pSession = FindSession(sessionId);
	if (pSession == nullptr)
		return false;
	
	// 소켓의 SO_LINGER 옵션에 1ms timeout을 지정한다음 send한다.
	// SO_LINGER 옵션을 이 위치에서 지정하는 이유는 SO_LINGER 옵션을 지정한 다음 최소 1번의 send가 이루어져야 closesocket 할 때 timeout이 적용되기 때문이다.
	// SO_LINGER 옵션에 1ms timeout을 지정한다음 send를 하지않고 closesocket 하면 timeout이 0일때와 동일하게 작동한다.
	if (InterlockedExchange8((char*)&pSession->_bCloseWait, true) == false)
	{
		pSession->_lastPacket = pPacket;

		LINGER linger;
		linger.l_onoff = 1;
		linger.l_linger = 100;
		if (setsockopt(pSession->_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
		{
			OnError(L"setsockopt SO_LINGER error: %u\n", WSAGetLastError());
			InterlockedIncrement64(&_otherErrors);
		}
	}
	bool result = SendPacket(sessionId, pPacket);

	DecreaseIoCount(pSession);
	return result;
}

// 비동기 send를 요청하고, send가 완료되면 연결을 끊는다.
bool CNetServer::SendPacketAsyncAndDisconnect(__int64 sessionId, CPacket* pPacket)
{
	CSession* pSession = FindSession(sessionId);
	if (pSession == nullptr)
		return false;

	// 소켓의 SO_LINGER 옵션에 1ms timeout을 지정한다음 send한다.
	// SO_LINGER 옵션을 이 위치에서 지정하는 이유는 SO_LINGER 옵션을 지정한 다음 최소 1번의 send가 이루어져야 closesocket 할 때 timeout이 적용되기 때문이다.
	// SO_LINGER 옵션에 1ms timeout을 지정한다음 send를 하지않고 closesocket 하면 timeout이 0일때와 동일하게 작동한다.
	if (InterlockedExchange8((char*)&pSession->_bCloseWait, true) == false)
	{
		pSession->_lastPacket = pPacket;

		LINGER linger;
		linger.l_onoff = 1;
		linger.l_linger = 100;
		if (setsockopt(pSession->_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
		{
			OnError(L"setsockopt SO_LINGER error: %u\n", WSAGetLastError());
			InterlockedIncrement64(&_otherErrors);
		}
	}
	bool result = SendPacketAsync(sessionId, pPacket);

	DecreaseIoCount(pSession);
	return result;
}

// IOCP 완료통지 큐에 작업을 삽입한다. 작업이 획득되면 OnInnerRequest 함수가 호출된다.
bool CNetServer::PostInnerRequest(CPacket* pPacket)
{
	BOOL ret = PostQueuedCompletionStatus(_hIOCP, 0, (ULONG_PTR)pPacket, (LPOVERLAPPED)1);
	if (ret == 0)
	{
		OnError(L"failed to post completion status to IOCP. error:%u\n", GetLastError());
		InterlockedIncrement64(&_otherErrors);
		return false;
	}
	return true;
}



/* session */
bool CNetServer::Disconnect(__int64 sessionId)
{
	CSession* pSession = FindSession(sessionId);
	if (pSession == nullptr)
		return false;

	// 소켓을 닫는다.
	CloseSocket(pSession);

	DecreaseIoCount(pSession);
	return true;
}


/* dynamic alloc */
// 64byte aligned 객체 생성을 위한 new, delete overriding
void* CNetServer::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}

void CNetServer::operator delete(void* p)
{
	_aligned_free(p);
}




/* Send, Recv */
void CNetServer::RecvPost(CSession* pSession)
{
	IncreaseIoCount(pSession);  // ioCount++; 현재 recv IO가 있다는 것을 표시함

	DWORD flag = 0;
	ZeroMemory(&pSession->_recvOverlapped, sizeof(OVERLAPPED_EX));
	pSession->_recvOverlapped.ioType = IO_RECV;

	WSABUF WSABuf[2];
	int directFreeSize = pSession->_recvQ.GetDirectFreeSize();
	WSABuf[0].buf = pSession->_recvQ.GetRear();
	WSABuf[0].len = directFreeSize;
	WSABuf[1].buf = pSession->_recvQ.GetBufPtr();
	WSABuf[1].len = pSession->_recvQ.GetFreeSize() - directFreeSize;

	int retRecv = WSARecv(pSession->_sock, WSABuf, 2, NULL, &flag, (OVERLAPPED*)&pSession->_recvOverlapped, NULL);

	if (retRecv == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			if (error == WSAECONNRESET                                             // 클라이언트에서 연결을 끊음
				|| error == WSAECONNABORTED                                        // 서버에서 연결을 끊음?
				|| (error == WSAENOTSOCK && pSession->_sock == INVALID_SOCKET))    // 서버에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				NET_OUTPUT_DEBUG(L"[Network] WSARecv failed by known error. error:%d, session:%lld\n", error, pSession->_sessionId);
				InterlockedIncrement64(&_WSARecvKnownError);  // 모니터링
			}
			else
			{
				NET_OUTPUT_SYSTEM(L"[Network] WSARecv failed by known error. error:%d, session:%lld\n", error, pSession->_sessionId);
				InterlockedIncrement64(&_WSARecvUnknownError);  // 모니터링
			}

			// 오류 발생시 세션의 연결을 끊는다.
			CloseSocket(pSession);
			DecreaseIoCount(pSession);
			return;
		}
		else
		{
			// error가 발생하지 않았다면 WSARecv가 동기로 수행되어 WSARecv 함수가 종료될 때 I/O가 완료됐다는 것이다.
			// error가 발생했는데 WSA_IO_PENDING 이라면 비동기 I/O가 성공적으로 요청되었다는 것이다.
			// 그런데 비동기 I/O가 성공적으로 요청됐는데 WSARecv 함수 종료시점에 pSession->_sock 값이 INVALID_SOCKET 으로 변경되었을 수 있다.
			// 이렇게되는 경우는 WSARecv 함수 내부로 소켓값이 전달되었고 아직 I/O를 요청하지 않은 상태에서, 
			// 다른스레드에서 세션을 Disconnect 하여 소켓값을 INVALID_SOCKET 으로 바꾸고 CancelIO 함수로 소켓에 걸린 I/O를 취소한 뒤에, 
			// WSARecv 함수내부에서 저장해두었던 소켓값으로 I/O를 요청한 경우이다.
			// 그래서 WSARecv 함수에서 비동기 I/O가 성공적으로 요청됐는데 소켓값이 INVALID_SOCKET 으로 바뀌어 있었다면 현재 걸린 I/O를 취소해주어야 한다.
			if (pSession->_sock == INVALID_SOCKET)
			{
				CancelIoEx((HANDLE)pSession->_socketToBeFree, NULL);
			}
			return;
		}
	}

	return;
}



// WSASend를 시도한다. 
void CNetServer::SendPost(CSession* pSession)
{
	if (pSession->_sendQ->Size() == 0) // 보낼 패킷이 없으면 종료
	{
		// SendPacket 함수에서는 sendQ에 패킷을 넣은 다음 SendPost를 호출함. SendPost를 호출했는데 패킷이 없다면 다른 스레드가 이미 WSASend 한 것임.
		// Send IO 완료통지에서는 sendIoFlag를 해제한다음 SendPost 함. SendPost를 호출했는데 패킷이 없다는 것은, 
		// 다른 어떤 스레드에서도 패킷을 넣지 않았거나, sendIoFlag를 해제한 순간 다른 스레드에서 패킷을 넣은다음 WSASend 한 것임. 
		return;
	}
	else if ((bool)InterlockedExchange8((char*)&pSession->_sendIoFlag, true) == true) // 내가 sendIoFlag를 false에서 true로 바꾸었을 때만 send함
	{
		return;
	}

	IncreaseIoCount(pSession);

	// 트래픽이 혼잡할 경우 send하지 않고 send 요청을 IOCP 큐에 다시 집어넣는다.
	if (_bTrafficCongestion)
	{
		BOOL retPost = PostQueuedCompletionStatus(_hIOCP, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)2);
		if (retPost == 0)
		{
			OnError(L"[Network] Failed to post send request to IOCP. error:%u\n", GetLastError());
			pSession->_bError = true;
			CloseSocket(pSession);
			pSession->_sendIoFlag = false;
			DecreaseIoCount(pSession);
			InterlockedIncrement64(&_otherErrors);
		}
		return;
	}

	// sendQ에서 직렬화버퍼 주소를 빼내어 WSABUF 구조체에 넣는다.
	const int numMaxPacket = SESSION_SIZE_ARR_PACKET;
	WSABUF arrWSABuf[numMaxPacket];
	int numPacket = 0;
	for (int i = 0; i < numMaxPacket; i++)
	{
		if (pSession->_sendQ->Dequeue(pSession->_arrPtrPacket[i]) == false)
			break;

		arrWSABuf[i].buf = (CHAR*)pSession->_arrPtrPacket[i]->GetHeaderPtr();
		arrWSABuf[i].len = pSession->_arrPtrPacket[i]->GetUseSize();
		numPacket++;
	}
	// 세션의 numSendPacket 설정
	pSession->_numSendPacket = numPacket;
	// 보낼 데이터가 없으면 종료
	if (numPacket == 0)
	{
		//InterlockedExchange8((char*)&pSession->_sendIoFlag, false);  // ? 여기서 interlocked 써야되는지 안써도되는지..
		pSession->_sendIoFlag = false;
		DecreaseIoCount(pSession);
		return;
	}

	// overlapped 구조체 설정
	ZeroMemory(&pSession->_sendOverlapped, sizeof(OVERLAPPED_EX));
	pSession->_sendOverlapped.ioType = IO_SEND;

	// send
	int retSend = WSASend(pSession->_sock, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&pSession->_sendOverlapped, NULL);
	InterlockedAdd64(&_sendCount, numPacket);  // 모니터링

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 완료통지가 가지않을 것이므로 여기서 직렬화버퍼의 count를 내린다.
			for (int i = 0; i < pSession->_numSendPacket; i++)
			{
				pSession->_arrPtrPacket[i]->SubUseCount();
			}

			//InterlockedExchange8((char*)&pSession->_sendIoFlag, false);    // ? 여기서 interlocked 써야되는지 안써도되는지..
			pSession->_sendIoFlag = false;
			if (error == WSAECONNRESET                                              // 클라이언트에서 연결을 끊음
				|| error == WSAECONNABORTED                                         // 서버에서 연결을 끊음?
				|| (error == WSAENOTSOCK && pSession->_sock == INVALID_SOCKET))     // 서버에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 모니터링
				NET_OUTPUT_DEBUG(L"[Network] WSASend failed with known error. error:%d, session:%lld\n", error, pSession->_sessionId);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError); // 모니터링
				NET_OUTPUT_SYSTEM(L"[Network] WSASend failed with unknown error. error:%d, session:%lld\n", error, pSession->_sessionId);
			}

			// 오류 발생시 세션의 연결을 끊는다.
			CloseSocket(pSession);
			DecreaseIoCount(pSession);
			return;
		}
		else
		{
			if (pSession->_sock == INVALID_SOCKET)
			{
				CancelIoEx((HANDLE)pSession->_socketToBeFree, NULL);
			}
			return;
		}
	}

	return;
}


// SendPacketAsync 함수를 통해 비동기 send 요청을 받았을 때 worker 스레드 내에서 호출된다. WSASend를 호출한다.
void CNetServer::SendPostAsync(CSession* pSession)
{
	// 이 함수가 호출되는 시점은 SendPacketAsync 함수에서 sendIoFlag를 true로 변경하고, IoCount를 증가시킨 상태이다. 
	// 그래서 sendIoFlag를 검사하거나 IoCount를 증가시키지 않는다.

	// 트래픽이 혼잡할 경우 send하지 않고 send 요청을 IOCP 큐에 다시 집어넣는다.
	if (_bTrafficCongestion)
	{
		BOOL retPost = PostQueuedCompletionStatus(_hIOCP, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)2);
		if (retPost == 0)
		{
			OnError(L"[Network] Failed to post send request to IOCP. error:%u\n", GetLastError());
			pSession->_bError = true;
			CloseSocket(pSession);
			pSession->_sendIoFlag = false;
			DecreaseIoCount(pSession);
			InterlockedIncrement64(&_otherErrors);
		}
		return;
	}

	// sendQ에서 직렬화버퍼 주소를 빼내어 WSABUF 구조체에 넣는다.
	const int numMaxPacket = SESSION_SIZE_ARR_PACKET;
	WSABUF arrWSABuf[numMaxPacket];
	int numPacket = 0;
	for (int i = 0; i < numMaxPacket; i++)
	{
		if (pSession->_sendQ->Dequeue(pSession->_arrPtrPacket[i]) == false)
			break;

		arrWSABuf[i].buf = (CHAR*)pSession->_arrPtrPacket[i]->GetHeaderPtr();
		arrWSABuf[i].len = pSession->_arrPtrPacket[i]->GetUseSize();
		numPacket++;
	}
	// 세션의 numSendPacket 설정
	pSession->_numSendPacket = numPacket;
	// 보낼 데이터가 없으면 종료
	if (numPacket == 0)
	{
		pSession->_sendIoFlag = false;
		DecreaseIoCount(pSession);
		return;
	}

	// overlapped 구조체 설정
	ZeroMemory(&pSession->_sendOverlapped, sizeof(OVERLAPPED_EX));
	pSession->_sendOverlapped.ioType = IO_SEND;

	// send
	int retSend = WSASend(pSession->_sock, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&pSession->_sendOverlapped, NULL);
	InterlockedAdd64(&_sendAsyncCount, numPacket);  // 모니터링

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 완료통지가 가지않을 것이므로 여기서 직렬화버퍼의 count를 내린다.
			for (int i = 0; i < pSession->_numSendPacket; i++)
			{
				pSession->_arrPtrPacket[i]->SubUseCount();
			}
			pSession->_sendIoFlag = false;

			if (error == WSAECONNRESET                                              // 클라이언트에서 연결을 끊음
				|| error == WSAECONNABORTED                                         // 서버에서 연결을 끊음?
				|| (error == WSAENOTSOCK && pSession->_sock == INVALID_SOCKET))     // 서버에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 모니터링
				NET_OUTPUT_DEBUG(L"[Network] WSASend failed with known error. error:%d, session:%lld\n", error, pSession->_sessionId);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError);  // 모니터링
				NET_OUTPUT_SYSTEM(L"[Network] WSASend failed with unknown error. error:%d, session:%lld\n", error, pSession->_sessionId);
			}

			// 오류 발생시 세션의 연결을 끊는다.
			CloseSocket(pSession);
			DecreaseIoCount(pSession);
			return;
		}
		else
		{
			if (pSession->_sock == INVALID_SOCKET)
			{
				CancelIoEx((HANDLE)pSession->_socketToBeFree, NULL);
			}
			return;
		}
	}

	return;
}









/* 세션 */
// 주의: FindSession 함수로 세션을 얻었을 경우 세션 사용 후에 반드시 DecreaseIoCount 함수를 호출해야함
CSession* CNetServer::FindSession(__int64 sessionId)
{
	unsigned short sessionIdx = sessionId & 0xFFFF;
	CSession* pSession = &_arrSession[sessionIdx];

	IncreaseIoCount(pSession);
	if (pSession->_releaseFlag == true)
	{
		// ioCount를 증가시켰지만 release된 세션임
		DecreaseIoCount(pSession);
		return nullptr;
	}
	if (pSession->_sessionId != sessionId)
	{
		// release된 세션은 아니지만 내가 원하는 세션이 아님
		DecreaseIoCount(pSession);
		return nullptr;
	}

	return pSession;
}

void CNetServer::IncreaseIoCount(CSession* pSession)
{
	InterlockedIncrement(&pSession->_ioCount);
}

// 세션의 ioCount를 -1 하고, ioCount가 0이고 releaseFlag가 false라면 세션을 release한다.
void CNetServer::DecreaseIoCount(CSession* pSession)
{
	InterlockedDecrement(&pSession->_ioCount);

	// IoCount가 0이면 releaseFlag를 true로 바꾼다.
	long long ioCountAndReleaseFlag = InterlockedCompareExchange64(&pSession->_ioCountAndReleaseFlag, 1, 0);
	// IoCount가 0이고 releaseFlag를 true로 바꿨을 경우 세션을 release 한다.
	if (ioCountAndReleaseFlag == 0)
	{
		ReleaseSession(pSession);
	}
}






CSession* CNetServer::AllocSession(SOCKET clntSock, SOCKADDR_IN& clntAddr)
{
	unsigned short sessionIdx;
	bool retPop = _stackSessionIdx.Pop(sessionIdx);
	if (retPop == false)
	{
		OnError(L"[Network] pop stack failed\n");
		InterlockedIncrement64(&_otherErrors);
		Crash();
		return nullptr;
	}

	
	CSession* pSession = &_arrSession[sessionIdx];
	// 세션을 초기화하기 전 ioCount를 증가시켜야 새로운 세션id가 할당된 뒤 곧바로 누군가가(FindSession 함수 등)세션을 release하지 못한다. 
	// 여기서 증가한 ioCount는 accept 스레드가 RecvPost를 호출한뒤 감소시킨다.
	IncreaseIoCount(pSession); 
	pSession->Init(clntSock, clntAddr);
	return pSession;
}

// 소켓에 진행중인 모든 I/O를 취소하고, 소켓을 INVALID_SOCKET으로 변경한다.
// 소켓을 바로 반환하지는 않는데, 소켓을 반환해버리면 현재 세션이 사용중인 소켓을 다음 alloc되는 세션이 할당받을 수 있고,
// 그렇게되면 현재 세션이 아직 반환된것은 아니기 때문에 현재 세션에서 아직 끝나지않은 WSARecv, WSASend 함수 call 등이 다른 세션의 소켓에 적용될 수 있다.
// 세션과 소켓의 반환은 ReleaseSession 함수에서 수행한다.
void CNetServer::CloseSocket(CSession* pSession)
{
	NET_OUTPUT_DEBUG(L"[Network] CloseSocket. session:%lld\n", pSession->_sessionId);

	IncreaseIoCount(pSession);  // 소켓 IO 취소가 끝나기 전까지는 세션을 반환하지 못하도록 한다.
	// isClosed를 true로 최초로 바꾸었을 때만 수행함
	if (InterlockedExchange8((char*)&pSession->_isClosed, true) == false)
	{
		pSession->_socketToBeFree = pSession->_sock;
		pSession->_sock = INVALID_SOCKET;
		BOOL retCancel = CancelIoEx((HANDLE)pSession->_socketToBeFree, NULL);  // 소켓과 연관된 모든 비동기 I/O를 취소한다.
		if (retCancel == 0)
		{
			DWORD error = GetLastError();
			if (error == ERROR_NOT_FOUND)  // 소켓과 연관된 I/O가 없음
				;
			else
			{
				NET_OUTPUT_SYSTEM(L"[Network] CancelIoEx failed. error:%u, session:%lld\n", error, pSession->_sessionId);
				InterlockedIncrement64(&_otherErrors);
			}
		}
	}
	DecreaseIoCount(pSession);
}

// 세션과 소켓을 반환한다.
void CNetServer::ReleaseSession(CSession* pSession)
{
	NET_OUTPUT_DEBUG(L"[Network] ReleaseSession. session:%lld\n", pSession->_sessionId);
	SOCKET targetSocket = pSession->_sock != INVALID_SOCKET ? pSession->_sock : pSession->_socketToBeFree;

	// 세션이 지연된 소켓닫기 대상이고, 오류가 발생하지 않았다면 소켓을 지금 닫지 않고 전용 스레드에게 전달한다. 세션은 반환한다.
	if (pSession->_bCloseWait == true && pSession->_bError == false)
	{
		_msgQDeferredCloseSocket.Enqueue(targetSocket);
		if (InterlockedExchange8((char*)&_bEventSetDeferredCloseSocket, true) == false)
			SetEvent(_hEventDeferredCloseSocket);
	}
	else
	{
		// 세션이 지연된 소켓닫기 대상인데 오류가 발생했다면 SO_LINGER 옵션의 timeout을 해제한다.
		if (pSession->_bCloseWait == true && pSession->_bError == true)
		{
			LINGER linger;
			linger.l_onoff = 1;
			linger.l_linger = 0;
			if (setsockopt(targetSocket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
			{
				OnError(L"setsockopt SO_LINGER error: %u\n", WSAGetLastError());
				InterlockedIncrement64(&_otherErrors);
			}
		}

		// 소켓 닫기
		closesocket(targetSocket);
	}

	// 세션 반환
	__int64 sessionId = pSession->_sessionId;
	_stackSessionIdx.Push(pSession->_index);
	InterlockedIncrement64(&_disconnectCount);  // 모니터링

	// 세션을 반환하기 전에 OnClientLeave 함수가 호출될경우 다음과 같은 문제가 발생할 수 있다:
	// OnClientLeave 함수내에서 SendPacket 등으로 세션을 사용함 -> 현재 ioCount가 0이기 때문에 IncreaseIoCount, DecreaseIoCount 하는 과정에서 ioCount가 또다시 0이 되어 ReleaseSession 함수가 한번더 호출됨
	// 그래서 OnClientLeave 함수는 세션을 반환한 뒤에 호출되어야 한다.
	OnClientLeave(sessionId);
}








/* 패킷 암호화 */
void CNetServer::EncryptPacket(CPacket* pPacket)
{
	if (pPacket->IsEncoded() == true)
		return;

	PacketHeader* pHeader = (PacketHeader*)pPacket->GetHeaderPtr();
	char* pTargetData = pPacket->GetDataPtr() - 1;  // checksum을 포함하여 암호화

	BYTE valP = 0;
	BYTE prevData = 0;
	for (int i = 0; i < pHeader->len + 1; i++)
	{
		valP = pTargetData[i] ^ (valP + pHeader->randKey + i + 1);
		pTargetData[i] = valP ^ (prevData + _packetKey + i + 1);
		prevData = pTargetData[i];
	}

	pPacket->SetEncoded();
}

bool CNetServer::DecipherPacket(CPacket* pPacket)
{
	PacketHeader* pHeader = (PacketHeader*)pPacket->GetHeaderPtr();
	char* pTargetData = pPacket->GetDataPtr() - 1;  // checksum을 포함하여 복호화

	BYTE valP = 0;
	BYTE prevData = 0;
	BYTE prevValP = 0;
	for (int i = 0; i < pHeader->len + 1; i++)
	{
		valP = pTargetData[i] ^ (prevData + _packetKey + i + 1);
		prevData = pTargetData[i];
		pTargetData[i] = valP ^ (prevValP + pHeader->randKey + i + 1);
		prevValP = valP;
	}
	
	pPacket->SetDecoded();

	// CheckSum 계산
	char* payloadPtr = pPacket->GetDataPtr(); 
	BYTE checkSum = 0;
	for (int i = 0; i < pHeader->len; i++)
		checkSum += payloadPtr[i];

	// CheckSum 검증
	if (pHeader->checkSum == checkSum)
		return true;
	else
		return false;
}





/* Get 모니터링 */
void CNetServer::GetArrGQCSWaitTime(int* arrTime)
{
	for (int i = 0; i < _numWorkerThread; i++)
		arrTime[i] = (int)(_arrWorkerTlsMonitor[i].GQCSWaitTime / (_liPerformanceFrequency.QuadPart / 1000));
}

__int64 CNetServer::GetSendCompletionCount()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].sendCompletionCount;
	return sum;
}
__int64 CNetServer::GetRecvCompletionCount()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].recvCompletionCount;
	return sum;
}
__int64 CNetServer::GetRecvCount()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].recvCount;
	return sum;
}
__int64 CNetServer::GetDisconnByKnownIoError()
{
	__int64 sum = _WSARecvKnownError + _WSASendKnownError;
	for (int i = 0; i < _numWorkerThread; i++)
	{
		sum += _arrWorkerTlsMonitor[i].disconnByKnownRecvIoError;
		sum += _arrWorkerTlsMonitor[i].disconnByKnownSendIoError;
	}
	return sum;
}
__int64 CNetServer::GetDisconnByUnknownIoError()
{
	__int64 sum = _WSARecvUnknownError + _WSASendUnknownError;
	for (int i = 0; i < _numWorkerThread; i++)
	{
		sum += _arrWorkerTlsMonitor[i].disconnByUnknownRecvIoError;
		sum += _arrWorkerTlsMonitor[i].disconnByUnknownSendIoError;
	}
	return sum;
}
__int64 CNetServer::GetDisconnBy121RecvIoError()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].disconnBy121RecvIoError;
	return sum;
}
__int64 CNetServer::GetDisconnByNormalProcess()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].disconnByNormalProcess;
	return sum;
}
__int64 CNetServer::GetDisconnByPacketCode()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].disconnByPacketCode;
	return sum;
}
__int64 CNetServer::GetDisconnByPacketLength()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].disconnByPacketLength;
	return sum;
}
__int64 CNetServer::GetDisconnByPacketDecode()
{
	__int64 sum = 0;
	for (int i = 0; i < _numWorkerThread; i++)
		sum += _arrWorkerTlsMonitor[i].disconnByPacketDecode;
	return sum;
}







/* thread */
// accept 스레드
unsigned WINAPI CNetServer::ThreadAccept(PVOID pParam)
{
	printf("accept thread begin. id:%u\n", GetCurrentThreadId());
	CNetServer& server = *(CNetServer*)pParam;

	server.UpdateAcceptThread();

	printf("accept thread end. id:%u\n", GetCurrentThreadId());
	return 0;
}

// accept 스레드 update
void CNetServer::UpdateAcceptThread()
{
	while (true)
	{
		// accept
		SOCKADDR_IN clntAddr;
		ZeroMemory(&clntAddr, sizeof(SOCKADDR_IN));
		int lenClntAddr = sizeof(clntAddr);
		SOCKET clntSock = accept(_listenSock, (SOCKADDR*)&clntAddr, &lenClntAddr);
		if (clntSock == INVALID_SOCKET)
		{
			int error = WSAGetLastError();
			if (error == WSAEINTR || error == WSAENOTSOCK) // 리슨소켓을 닫음. accept 스레드 종료
			{
				break;
			}
			else
			{
				NET_OUTPUT_SYSTEM(L"[Network] accept failed. error:%u\n", WSAGetLastError());
				InterlockedIncrement64(&_otherErrors);
				continue;
			}
		}
		_acceptCount++; // 모니터링

		// session 수가 max이면 연결을 끊는다.
		if (GetNumSession() >= _numMaxSession)
		{
			NET_OUTPUT_DEBUG(L"[Network] there is no room for the new session!!\n");
			closesocket(clntSock);
			_disconnBySessionLimit++;  // 모니터링
			continue;
		}

		// connect 승인 확인
		bool bAccept = OnConnectionRequest(clntAddr.sin_addr.S_un.S_addr, clntAddr.sin_port);
		if (bAccept == false)
		{
			closesocket(clntSock);
			_disconnByOnConnReq++; // 모니터링
			continue;
		}
		_connectCount++; // 모니터링

		// 세션 생성
		CSession* pNewSession = AllocSession(clntSock, clntAddr);

		// 소켓과 IOCP 연결
		if (CreateIoCompletionPort((HANDLE)clntSock, _hIOCP, (ULONG_PTR)pNewSession, 0) == NULL)
		{
			OnError(L"[Network] failed to associate socket with IOCP\n");
			closesocket(clntSock);
			_disconnByIOCPAssociation++;
			continue;
		}

		// client join
		OnClientJoin(pNewSession->_sessionId);

		// recv
		RecvPost(pNewSession);

		// AllocSession 에서 증가시켰던 ioCount를 낮춘다.
		DecreaseIoCount(pNewSession);

		// 트래픽이 혼잡하면 accept를 잠시 멈춘다.
		if (_bTrafficCongestion == true)
		{
			Sleep(10);
		}
	}
}



// worker 스레드
unsigned WINAPI CNetServer::ThreadWorker(PVOID pParam)
{
	printf("worker thread begin. id:%u\n", GetCurrentThreadId());
	CNetServer& server = *(CNetServer*)pParam;

	server.UpdateWorkerThread();

	printf("worker thread end. id:%u\n", GetCurrentThreadId());
	return 0;
}

// worker 스레드 업데이트
void CNetServer::UpdateWorkerThread()
{
	// worker 스레드 내부 TLS 모니터링을 위한 static tls index 값 설정
	static long _sThreadNumber = -1;
	long _stlsTlsIndex = InterlockedIncrement(&_sThreadNumber);

	DWORD numByteTrans;
	ULONG_PTR compKey;
	OVERLAPPED_EX* pOverlapped;
	CSession* pSession;
	CPacket* pRecvPacket;
	LARGE_INTEGER liGQCSWaitStartTime;
	LARGE_INTEGER liGQCSWaitEndTime;
	// IOCP 완료통지 처리
	while (true)
	{
		numByteTrans = 0;
		compKey = NULL;
		pOverlapped = nullptr;
		QueryPerformanceCounter(&liGQCSWaitStartTime);
		BOOL retGQCS = GetQueuedCompletionStatus(_hIOCP, &numByteTrans, &compKey, (OVERLAPPED**)&pOverlapped, INFINITE);
		QueryPerformanceCounter(&liGQCSWaitEndTime);
		ADD_WORKER_TLS_MONITOR(GQCSWaitTime, liGQCSWaitEndTime.QuadPart - liGQCSWaitStartTime.QuadPart);

		DWORD error = GetLastError();
		DWORD WSAError = WSAGetLastError();

		// 세션 얻기
		pSession = (CSession*)compKey;

		// IOCP dequeue 실패, 또는 timeout. 이 경우 numByteTrans, compKey 변수에는 값이 들어오지 않기 때문에 오류체크에 사용할 수 없다.
		if (retGQCS == 0 && pOverlapped == nullptr)
		{
			// timeout
			if (error == WAIT_TIMEOUT)
			{
				OnError(L"[Network] GetQueuedCompletionStatus timeout. error:%u\n", error);
			}
			// error
			else
			{
				OnError(L"[Network] GetQueuedCompletionStatus failed. error:%u\n", error);
			}
			InterlockedIncrement64(&_otherErrors);
			break;
		}

		// 스레드 종료 메시지를 받음
		else if (retGQCS != 0 && pOverlapped == nullptr)
		{
			break;
		}

		// PostInnerRequest 함수에 의해 내부적으로 삽입된 완료통지를 받음
		else if (pOverlapped == (OVERLAPPED_EX*)1)
		{
			CPacket* pInnerPacket = (CPacket*)compKey;
			OnInnerRequest(*pInnerPacket);
		}

		// SendPacketAsync 함수에 의해서 SnedPost 요청을 받음
		else if (pOverlapped == (OVERLAPPED_EX*)2)
		{
			SendPostAsync(pSession);
		}

		// recv 완료통지 처리
		else if (pOverlapped->ioType == IO_RECV)
		{
			INCREASE_WORKER_TLS_MONITOR(recvCompletionCount);

			bool bRecvSucceed = true;  // recv 성공 여부

			// recv IO가 실패한 경우
			if (retGQCS == 0)
			{
				bRecvSucceed = false;
				if (error == ERROR_NETNAME_DELETED     // ERROR_NETNAME_DELETED 64 는 WSAECONNRESET 와 동일하다?
					|| error == ERROR_CONNECTION_ABORTED)  // ERROR_CONNECTION_ABORTED 1236 은 서버의 컨텐츠 코드에서 클라에 문제가 있다고 판단되어 끊었을 경우 발생함. WSAECONNABORTED 10053 와 동일?
				{
					pSession->_bError = true;
					NET_OUTPUT_DEBUG(L"[Network] recv socket error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d\n", error, WSAError, pSession->_sessionId, numByteTrans);
					INCREASE_WORKER_TLS_MONITOR(disconnByKnownRecvIoError);
				}
				else if (error == ERROR_OPERATION_ABORTED) // ERROR_OPERATION_ABORTED 995 는 비동기 IO가 진행중에 CancelIo 하였거나 클라이언트에서 연결을 끊었을 경우 발생함.
				{
					NET_OUTPUT_DEBUG(L"[Network] recv socket error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d\n", error, WSAError, pSession->_sessionId, numByteTrans);
					INCREASE_WORKER_TLS_MONITOR(disconnByKnownRecvIoError);
				}
				else if (error == ERROR_SEM_TIMEOUT)     // ERROR_SEM_TIMEOUT 121 은 네트워크가 혼잡하여 retransmission이 계속되다 timeout 되어 발생하는것
				{
					pSession->_bError = true;
					NET_OUTPUT_DEBUG(L"[Network] recv socket 121 error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d, ip:%s, port:%d\n", error, WSAError, pSession->_sessionId, numByteTrans, pSession->_szIP, pSession->_port);
					INCREASE_WORKER_TLS_MONITOR(disconnBy121RecvIoError);
				}
				else
				{
					pSession->_bError = true;
					NET_OUTPUT_SYSTEM(L"[Network] recv socket unknown error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d\n", error, WSAError, pSession->_sessionId, numByteTrans);
					INCREASE_WORKER_TLS_MONITOR(disconnByUnknownRecvIoError);
				}
			}

			// 클라이언트로부터 연결끊김 메시지를 받은 경우
			else if (numByteTrans == 0)
			{
				bRecvSucceed = false;
				NET_OUTPUT_DEBUG(L"[Network] recv closed by client's close request. session:%lld\n", pSession->_sessionId);
				INCREASE_WORKER_TLS_MONITOR(disconnByNormalProcess);
			}

			// 정상적인 메시지 처리
			else if (numByteTrans > 0)
			{
				// recvQ 내의 모든 메시지를 처리한다.
				PacketHeader header;
				pSession->_recvQ.MoveRear(numByteTrans);
				while (true)
				{
					if (pSession->_recvQ.GetUseSize() < sizeof(PacketHeader)) // 데이터가 헤더길이보다 작음
						break;

					// 헤더를 읽음
					pSession->_recvQ.Peek((char*)&header, sizeof(PacketHeader));
					if (header.code != _packetCode) // 패킷 코드가 잘못됐을 경우 error
					{
						NET_OUTPUT_DEBUG(L"[Network] header.code is not valid. sessionID:%llu, code:%d\n", pSession->_sessionId, header.code);
						pSession->_bError = true;
						bRecvSucceed = false;
						INCREASE_WORKER_TLS_MONITOR(disconnByPacketCode);
						break;
					}

					if (header.len > _maxPacketSize) // 데이터 크기가 최대 패킷크기보다 큰 경우 error
					{
						NET_OUTPUT_DEBUG(L"[Network] header.len is larger than max packet size. sessionID:%llu, len:%d, max packet size:%d\n", pSession->_sessionId, header.len, _maxPacketSize);
						pSession->_bError = true;
						bRecvSucceed = false;
						INCREASE_WORKER_TLS_MONITOR(disconnByPacketLength);
						break;
					}

					if (sizeof(PacketHeader) + header.len > SIZE_RECV_BUFFER) // 데이터의 크기가 버퍼 크기보다 클 경우 error
					{
						NET_OUTPUT_DEBUG(L"[Network] packet data length is longer than recv buffer. sessionID:%llu, len:%d, size of ringbuffer:%d\n"
							, pSession->_sessionId, header.len, pSession->_recvQ.GetSize());
						pSession->_bError = true;
						bRecvSucceed = false;
						INCREASE_WORKER_TLS_MONITOR(disconnByPacketLength);
						break;
					}

					// 버퍼내의 데이터 크기 확인
					if (pSession->_recvQ.GetUseSize() < sizeof(PacketHeader) + header.len) // 데이터가 모두 도착하지 않았음
					{
						break;
					}

					// 직렬화버퍼 준비
					pRecvPacket = CPacket::AllocPacket();
					pRecvPacket->Init(sizeof(PacketHeader));

					// 직렬화 버퍼로 데이터를 읽음
					pSession->_recvQ.MoveFront(sizeof(PacketHeader));
					pSession->_recvQ.Dequeue(pRecvPacket->GetDataPtr(), header.len);
					pRecvPacket->MoveWritePos(header.len);

					// 복호화
					if (_bUsePacketEncryption == true)
					{
						pRecvPacket->PutHeader((const char*)&header); // 복호화를 위해 헤더를 넣어줌
						bool retDecipher = DecipherPacket(pRecvPacket);
						if (retDecipher == false) // 복호화에 실패함(CheckSum이 잘못됨)
						{
							NET_OUTPUT_DEBUG(L"[Network] packet decoding failed. sessionID:%llu\n", pSession->_sessionId);
							pRecvPacket->SubUseCount();
							pSession->_bError = true;
							bRecvSucceed = false;
							INCREASE_WORKER_TLS_MONITOR(disconnByPacketDecode);
							break;
						}
					}

					NET_OUTPUT_DEBUG(L"[Network] recved. session:%lld, data len:%d, packet type:%d\n", pSession->_sessionId, header.len, *(WORD*)pRecvPacket->GetDataPtr());

					// 사용자 패킷처리 함수 호출
					INCREASE_WORKER_TLS_MONITOR(recvCount);
					OnRecv(pSession->_sessionId, *pRecvPacket);

					// 사용카운트 감소
					pRecvPacket->SubUseCount();

				}
			}

			// numByteTrans가 0보다 작음. error?
			else
			{
				bRecvSucceed = false;
				OnError(L"[Network] recv error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u, session:%lld\n", numByteTrans, error, WSAError, pSession->_sessionId);
				InterlockedIncrement64(&_otherErrors);
				Crash();
			}

			// recv를 성공적으로 끝냈으면 다시 recv 한다. 실패했으면 CloseSocket
			if (bRecvSucceed)
				RecvPost(pSession);
			else
				CloseSocket(pSession);

			// ioCount--
			DecreaseIoCount(pSession);
		}

		// send IO 완료통지 처리
		// 이 시점에서는 ioCount > 0 이기 때문에 세션이 유효하다.
		else if (pOverlapped->ioType == IO_SEND)
		{
			INCREASE_WORKER_TLS_MONITOR(sendCompletionCount);

			// send에 사용한 직렬화버퍼의 사용카운트를 감소시킨다.
			for (int i = 0; i < pSession->_numSendPacket; i++)
			{
				long useCount = pSession->_arrPtrPacket[i]->SubUseCount();
			}

			// 세션에 보내고끊기 함수가 호출되었을 경우, 세션의 lastPacket이 보내졌으면 연결을 끊는다.
			if (pSession->_bCloseWait == true)
			{
				for (int i = 0; i < pSession->_numSendPacket; i++)
				{
					if (pSession->_arrPtrPacket[i] == pSession->_lastPacket)
					{
						CloseSocket(pSession);
						break;
					}
				}
			}

			// sendIoFlag 를 해제한다.
			// 직렬화버퍼의 사용카운트를 감소시킨다음 sendIoFlag를 해제해야 pSession->_arrPtrPacket 에 데이터가 덮어씌워지는일이 없다.
			//InterlockedExchange8((char*)&pSession->_sendIoFlag, false);
			pSession->_sendIoFlag = false;

			// send IO가 실패한 경우
			if (retGQCS == 0)
			{
				if (error == ERROR_NETNAME_DELETED        // ERROR_NETNAME_DELETED 64 는 WSAECONNRESET 와 동일하다?
					|| error == ERROR_CONNECTION_ABORTED)  // ERROR_CONNECTION_ABORTED 1236 은 서버의 컨텐츠 코드에서 클라에 문제가 있다고 판단되어 끊었을 경우 발생함. WSAECONNABORTED 10053 와 동일?
				{
					pSession->_bError = true;
					NET_OUTPUT_DEBUG(L"[Network] send socket known error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d\n", error, WSAError, pSession->_sessionId, numByteTrans);
					INCREASE_WORKER_TLS_MONITOR(disconnByKnownSendIoError);
				}
				else if (error == ERROR_OPERATION_ABORTED) // ERROR_OPERATION_ABORTED 995 는 비동기 IO가 진행중에 CancelIo 하였거나 클라이언트에서 연결을 끊었을 경우 발생함.
				{
					NET_OUTPUT_DEBUG(L"[Network] send socket known error. error:%u, WSAError:%u, session:%lld, numByteTrans:%d\n", error, WSAError, pSession->_sessionId, numByteTrans);
					INCREASE_WORKER_TLS_MONITOR(disconnByKnownSendIoError);
				}
				else
				{
					pSession->_bError = true;
					NET_OUTPUT_SYSTEM(L"[Network] send socket unknown error. NumberOfBytesTransferred:%d, error:%u, WSAError:%u, session:%lld\n", numByteTrans, error, WSAError, pSession->_sessionId);
					INCREASE_WORKER_TLS_MONITOR(disconnByUnknownSendIoError);
				}

				CloseSocket(pSession);  // recv가 걸려있을것이기 때문에 소켓의 모든 IO를 취소한다.
			}
			// 소켓이 닫힌 경우
			else if (pSession->_isClosed)
			{
				;
			}
			// 완료통지 처리 
			else if (numByteTrans > 0)
			{
				// sendQ 내의 데이터 전송을 시도함
				SendPost(pSession);
			}
			// numByteTrans가 0보다 작음. error?
			else
			{
				OnError(L"[Network] send error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u, session:%lld\n", numByteTrans, error, WSAError, pSession->_sessionId);
				pSession->_bError = true;
				CloseSocket(pSession);
				InterlockedIncrement64(&_otherErrors);
				Crash();
			}

			// ioCount--
			DecreaseIoCount(pSession);
		}
		else
		{
			// overlapped 구조체의 IO Type이 올바르지 않음
			OnError(L"[Network] IO Type error. ioType:%d, session:%lld\n", pOverlapped->ioType, pSession->_sessionId);
			InterlockedIncrement64(&_otherErrors);
			// crash
			Crash();

			// ioCount--
			DecreaseIoCount(pSession);
		}
	}
}



// 트래픽 혼잡제어 스레드
unsigned WINAPI CNetServer::ThreadTrafficCongestionControl(PVOID pParam)
{
	printf("traffic congestion control thread begin. id:%u\n", GetCurrentThreadId());
	CNetServer& server = *(CNetServer*)pParam;

	server.UpdateTrafficCongestionControlThread();

	printf("traffic congestion control thread end. id:%u\n", GetCurrentThreadId());
	return 0;
}

void CNetServer::UpdateTrafficCongestionControlThread()
{
	MIB_TCPSTATS prevTCPStats = { 0, };
	MIB_TCPSTATS currTCPStats = { 0, };
	__int64 diffOutSegs = 0;
	__int64 diffRetransSegs = 0;
	__int64 retransCriterionWithin100ms;

	GetTcpStatistics(&prevTCPStats);

	while (_bShutdown == false)
	{
		Sleep(500);

		// TCP 통계 얻기
		GetTcpStatistics(&currTCPStats);

		// 차이값 계산
		if (currTCPStats.dwOutSegs < prevTCPStats.dwOutSegs)  // 값 overflow 예외처리
			diffOutSegs = UINT_MAX - prevTCPStats.dwOutSegs + currTCPStats.dwOutSegs;
		else
			diffOutSegs = currTCPStats.dwOutSegs - prevTCPStats.dwOutSegs;

		if (currTCPStats.dwRetransSegs < prevTCPStats.dwRetransSegs)  // 값 overflow 예외처리
			diffRetransSegs = UINT_MAX - prevTCPStats.dwRetransSegs + currTCPStats.dwRetransSegs;
		else
			diffRetransSegs = currTCPStats.dwRetransSegs - prevTCPStats.dwRetransSegs;

		prevTCPStats = currTCPStats;

		// 보낸 segment 수가 10000 이하이거나, 재전송된 segment 수가 보낸 segment 수의 10% 이하라면 넘어감
		if (diffOutSegs < 10000 || diffOutSegs > diffRetransSegs * 10)
		{
			continue;
		}
		// 보낸 segment 수가 10000 이상이고, 재전송된 segment 수가 보낸 segment 수의 10% 보다 크다면 accept와 send를 잠시 막음
		else
		{
			retransCriterionWithin100ms = diffOutSegs / 10;  // 100ms 동안의 재전송 기준치 설정(혼잡제어중에는 send를 하지않을것이기 때문에 재전송 기준치를 높게 잡음)
			_bTrafficCongestion = true;
			_trafficCongestionControlCount++;  // 모니터링

			// 만약 혼잡flag가 on 되었다면, 100ms마다 추가로 트래픽을 점검한다.
			// 10번의 연속적인 점검동안 트래픽이 정상수치로 유지될 경우 추가 점검을 종료한다.
			int normalWorkingCount = 0;
			while (true)
			{
				Sleep(100);
				GetTcpStatistics(&currTCPStats);

				// 차이값 계산
				if (currTCPStats.dwOutSegs < prevTCPStats.dwOutSegs)  // 값 overflow 예외처리
					diffOutSegs = UINT_MAX - prevTCPStats.dwOutSegs + currTCPStats.dwOutSegs;
				else
					diffOutSegs = currTCPStats.dwOutSegs - prevTCPStats.dwOutSegs;

				if (currTCPStats.dwRetransSegs < prevTCPStats.dwRetransSegs)  // 값 overflow 예외처리
					diffRetransSegs = UINT_MAX - prevTCPStats.dwRetransSegs + currTCPStats.dwRetransSegs;
				else
					diffRetransSegs = currTCPStats.dwRetransSegs - prevTCPStats.dwRetransSegs;

				prevTCPStats = currTCPStats;
				
				// 만약 혼잡제어 중이라면 재전송된 segment 수가 기준치 이하라면 accept와 send를 잠시 허용함
				if (_bTrafficCongestion == true)
				{
					if (diffRetransSegs < retransCriterionWithin100ms)
					{
						normalWorkingCount++;
						_bTrafficCongestion = false;
					}
					else
					{
						normalWorkingCount = 0;
						_trafficCongestionControlCount++;  // 모니터링
					}
				}
				// 만약 혼잡제어 중이 아니라면 보낸 segment 수가 1000 이상이고, 재전송된 segment 수가 보낸 segment 수의 10% 보다 크다면 accept와 send를 잠시 막음
				else
				{
					if (diffOutSegs > 1000 && diffRetransSegs * 10 > diffOutSegs)
					{
						normalWorkingCount = 0;
						_bTrafficCongestion = true;
						_trafficCongestionControlCount++;  // 모니터링
					}
					else
					{
						normalWorkingCount++;
					}
				}

				// 10번의 추가 점검동안 트래픽이 정상이었다면 추가 점검 종료
				if (normalWorkingCount >= 10)
					break;
			}
		}
	}
}


// 지연된 소켓닫기 스레드
unsigned WINAPI CNetServer::ThreadDeferredCloseSocket(PVOID pParam)
{
	printf("deferred close socket thread begin. id:%u\n", GetCurrentThreadId());
	CNetServer& server = *(CNetServer*)pParam;

	server.UpdateThreadDeferredCloseSocketThread();

	printf("deferred close socket thread end. id:%u\n", GetCurrentThreadId());
	return 0;
}

void CNetServer::UpdateThreadDeferredCloseSocketThread()
{
	SOCKET socket;
	while (true)
	{
		if (WaitForSingleObject(_hEventDeferredCloseSocket, INFINITE) != WAIT_OBJECT_0)
		{
			InterlockedExchange8((char*)&_bEventSetDeferredCloseSocket, false);
			return;
		}
		InterlockedExchange8((char*)&_bEventSetDeferredCloseSocket, false);

		while (_msgQDeferredCloseSocket.Dequeue(socket))
		{
			// 소켓의 송신버퍼내의 데이터가 모두 보내질때까지 기다린다음 소켓을 닫는다.
			closesocket(socket);
			InterlockedIncrement64(&_deferredDisconnectCount);  // 모니터링
		}
	}
}









/* Crash */
void CNetServer::Crash()
{
	int* p = 0;
	*p = 0;
}


