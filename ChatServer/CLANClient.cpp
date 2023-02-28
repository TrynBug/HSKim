#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#include "CLANClient.h"


CLANClient::CLANClient()
	: _hIOCP(NULL), _socket(NULL)
	, _szServerIP{}, _serverPort(0), _bUseNagle(false), _bUsePacketEncryption(false)
	, _packetCode(0), _packetKey(0), _maxPacketSize(0)
	, _hThreadWorker(NULL), _idThreadWorker(0)
	, _sendOverlapped{ 0, }, _recvOverlapped{ 0, }, _arrPtrPacket{ 0, }, _numSendPacket(0)
	, _sendIoFlag(false), _isClosed(false)
	, _bOutputDebug(false), _bOutputSystem(true)
{
}

CLANClient::~CLANClient()
{

}

/* server */
bool CLANClient::StartUp(const wchar_t* serverIP, unsigned short serverPort, bool bUseNagle, BYTE packetCode, BYTE packetKey, int maxPacketSize, bool bUsePacketEncryption)
{
	timeBeginPeriod(1);

	wcscpy_s(_szServerIP, wcslen(serverIP) + 1, serverIP);
	_serverPort = serverPort;
	_bUseNagle = bUseNagle;
	_packetCode = packetCode;
	_packetKey = packetKey;
	_maxPacketSize = maxPacketSize;
	_bUsePacketEncryption = bUsePacketEncryption;

	// WSAStartup
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		OnError(L"[LAN Client] Failed to initiate Winsock DLL. error:%u\n", WSAGetLastError());
		return false;
	}

	// IOCP 생성
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 1);
	if (_hIOCP == NULL)
	{
		OnError(L"[LAN Client] Failed to create IOCP. error:%u\n", GetLastError());
		return false;
	}

	// 소켓 생성
	_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// SO_LINGER 옵션 설정
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	setsockopt(_socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	// nagle 옵션 해제 여부
	if (_bUseNagle == false)
	{
		DWORD optval = TRUE;
		int retSockopt = setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
		if (retSockopt == SOCKET_ERROR)
		{
			OnError(L"[LAN Client] Failed to set TCP_NODELAY option on listen socket. error:%u\n", WSAGetLastError());
		}
	}

	// 서버에 연결
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	InetPton(AF_INET, _szServerIP, &serverAddr.sin_addr);
	serverAddr.sin_port = htons(_serverPort);
	if (connect(_socket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		OnError(L"[LAN Client] Failed to connect server. error:%u\n", WSAGetLastError());
		return false;
	}

	// 소켓과 IOCP 연결
	if (CreateIoCompletionPort((HANDLE)_socket, _hIOCP, 0, 0) == NULL)
	{
		OnError(L"[LAN Client] failed to associate socket with IOCP\n");
		closesocket(_socket);
		return false;
	}


	// worker 스레드 생성
	_hThreadWorker = (HANDLE)_beginthreadex(NULL, 0, ThreadWorker, (PVOID)this, 0, &_idThreadWorker);
	if (_hThreadWorker == NULL)
	{
		OnError(L"[LAN Client] An error occurred when starting the worker thread. error:%u\n", GetLastError());
		return false;
	}


	OnOutputSystem(L"[LAN Client] Start Up LAN Client\n"
		L"\tLAN Server IP : % s\n"
		L"\tLAN Server Port: %d\n"
		L"\tEnable Nagle: %d\n"
		L"\tUse Packet Encryption: %d\n"
		L"\tPacket Header Code: 0x%x\n"
		L"\tPacket Encode Key: 0x%x\n"
		L"\tMaximum Packet Size: %d\n"
		, _szServerIP
		, _serverPort
		, _bUseNagle
		, _bUsePacketEncryption
		, _packetCode
		, _packetKey
		, _maxPacketSize);


	// recv 시작
	RecvPost();

	return true;
}





// 네트워크 종료
void CLANClient::Shutdown()
{
	// 서버와의 연결을 끊음
	Disconnect();

	// worker 스레드 종료 메시지를 보낸다.
	PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);

	// worker 스레드가 종료되기를 1초간 기다린다.
	ULONGLONG timeout = 1000;
	BOOL retWait = WaitForSingleObject(_hThreadWorker, (DWORD)timeout);
	if (retWait != WAIT_OBJECT_0)
	{
		LAN_OUTPUT_SYSTEM(L"[LAN Client] Timeout occurred while waiting for the thread to be terminated. force terminate it. error:%u\n", GetLastError());
		TerminateThread(_hThreadWorker, 0);
	}

	// 객체 삭제
	CPacket* pPacket;
	while (_sendQ.Dequeue(pPacket) == false);
	CloseHandle(_hThreadWorker);
	CloseHandle(_hIOCP);
	// WSACleanup 은 다른 네트워크에 영향을 미칠수 있기 때문에 호출하지 않음.

}


/* packet */
CPacket* CLANClient::AllocPacket()
{
	CPacket* pPacket = CPacket::AllocPacket();
	pPacket->Init(sizeof(LANPacketHeader));
	return pPacket;
}


bool CLANClient::SendPacket(CPacket* pPacket)
{
	LANPacketHeader header;
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
	_sendQ.Enqueue(pPacket);

	// Send를 시도함
	SendPost();

	return true;
}




bool CLANClient::SendPacketAsync(CPacket* pPacket)
{
	LANPacketHeader header;
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
	_sendQ.Enqueue(pPacket);

	// 현재 send가 진행중인지 확인
	if (_sendQ.Size() == 0) // 보낼 패킷이 없으면 종료
	{
		return true;
	}
	else if ((bool)InterlockedExchange8((char*)&_sendIoFlag, true) == true) // 내가 sendIoFlag를 false에서 true로 바꾸었을 때만 send함
	{
		return true;
	}

	// 현재 send가 진행중이 아니라면 SendPost 요청을 IOCP 큐에 삽입함
	BOOL retPost = PostQueuedCompletionStatus(_hIOCP, 0, 0, (LPOVERLAPPED)2);
	if (retPost == 0)
	{
		OnError(L"[LAN Client] Failed to post send request to IOCP. error:%u\n", GetLastError());
		return false;
	}

	return true;
}



// IOCP 완료통지 큐에 작업을 삽입한다. 작업이 획득되면 OnInnerRequest 함수가 호출된다.
bool CLANClient::PostInnerRequest(CPacket* pPacket)
{
	BOOL ret = PostQueuedCompletionStatus(_hIOCP, 0, (ULONG_PTR)pPacket, (LPOVERLAPPED)1);
	if (ret == 0)
	{
		OnError(L"[LAN Client] failed to post completion status to IOCP. error:%u\n", GetLastError());
		return false;
	}
	return true;
}



/* dynamic alloc */
// 64byte aligned 객체 생성을 위한 new, delete overriding
void* CLANClient::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}

void CLANClient::operator delete(void* p)
{
	_aligned_free(p);
}




/* client */
bool CLANClient::Disconnect()
{
	// isClosed를 true로 최초로 바꾸었을 때만 수행함
	if (InterlockedExchange8((char*)&_isClosed, true) == false)
	{
		// 소켓을 닫는다.
		closesocket(_socket);
		_socket = INVALID_SOCKET;
		InterlockedIncrement64(&_disconnectCount);  // 모니터링
	}

	return true;
}



/* Send, Recv */
void CLANClient::RecvPost()
{
	DWORD flag = 0;
	ZeroMemory(&_recvOverlapped, sizeof(LAN_OVERLAPPED_EX));
	_recvOverlapped.ioType = LAN_IO_RECV;

	WSABUF WSABuf[2];
	int directFreeSize = _recvQ.GetDirectFreeSize();
	WSABuf[0].buf = _recvQ.GetRear();
	WSABuf[0].len = directFreeSize;
	WSABuf[1].buf = _recvQ.GetBufPtr();
	WSABuf[1].len = _recvQ.GetFreeSize() - directFreeSize;

	int retRecv = WSARecv(_socket, WSABuf, 2, NULL, &flag, (OVERLAPPED*)&_recvOverlapped, NULL);
	InterlockedIncrement64(&_recvCount); // 모니터링

	if (retRecv == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			if (error == WSAECONNRESET                                     // 서버에서 연결을 끊음
				|| error == WSAECONNABORTED                                // 클라이언트에서 연결을 끊음?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))    // 클라이언트에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSARecv failed by known error. error:%d\n", error);
				InterlockedIncrement64(&_WSARecvKnownError);  // 모니터링
			}
			else
			{
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSARecv failed by known error. error:%d\n", error);
				InterlockedIncrement64(&_WSARecvUnknownError);  // 모니터링
			}

			// 오류 발생시 세션의 연결을 끊는다.
			Disconnect();
			return;
		}
	}

	return;
}



// WSASend를 시도한다. 
void CLANClient::SendPost()
{
	if (_sendQ.Size() == 0) // 보낼 패킷이 없으면 종료
	{
		return;
	}
	else if ((bool)InterlockedExchange8((char*)&_sendIoFlag, true) == true) // 내가 sendIoFlag를 false에서 true로 바꾸었을 때만 send함
	{
		return;
	}

	// sendQ에서 직렬화버퍼 주소를 빼내어 WSABUF 구조체에 넣는다.
	const int numMaxPacket = LAN_CLIENT_SIZE_ARR_PACKTE;
	WSABUF arrWSABuf[numMaxPacket];
	int numPacket = 0;
	for (int i = 0; i < numMaxPacket; i++)
	{
		if (_sendQ.Dequeue(_arrPtrPacket[i]) == false)
			break;

		arrWSABuf[i].buf = (CHAR*)_arrPtrPacket[i]->GetHeaderPtr();
		arrWSABuf[i].len = _arrPtrPacket[i]->GetUseSize();
		numPacket++;
	}
	// 세션의 numSendPacket 설정
	_numSendPacket = numPacket;
	// 보낼 데이터가 없으면 종료
	if (numPacket == 0)
	{
		_sendIoFlag = false;
		return;
	}

	// overlapped 구조체 설정
	ZeroMemory(&_sendOverlapped, sizeof(LAN_OVERLAPPED_EX));
	_sendOverlapped.ioType = LAN_IO_SEND;

	// send
	int retSend = WSASend(_socket, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&_sendOverlapped, NULL);
	InterlockedAdd64(&_sendCount, numPacket);  // 모니터링

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 완료통지가 가지않을 것이므로 여기서 직렬화버퍼의 count를 내린다.
			for (int i = 0; i < _numSendPacket; i++)
			{
				_arrPtrPacket[i]->SubUseCount();
			}

			_sendIoFlag = false;
			if (error == WSAECONNRESET                                              // 서버에서 연결을 끊음
				|| error == WSAECONNABORTED                                         // 클라이언트에서 연결을 끊음?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))     // 클라이언트에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 모니터링
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSASend failed with known error. error:%d\n", error);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError); // 모니터링
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSASend failed with unknown error. error:%d\n", error);
			}

			// 오류 발생시 클라이언트의 연결을 끊는다.
			Disconnect();
			return;
		}
	}

	return;
}


// SendPacketAsync 함수를 통해 비동기 send 요청을 받았을 때 worker 스레드 내에서 호출된다. WSASend를 호출한다.
void CLANClient::SendPostAsync()
{
	// 이 함수가 호출되는 시점은 SendPacketAsync 함수에서 sendIoFlag를 true로 변경한 상태이다. 그래서 sendIoFlag를 검사하지 않는다.

	// sendQ에서 직렬화버퍼 주소를 빼내어 WSABUF 구조체에 넣는다.
	const int numMaxPacket = LAN_CLIENT_SIZE_ARR_PACKTE;
	WSABUF arrWSABuf[numMaxPacket];
	int numPacket = 0;
	for (int i = 0; i < numMaxPacket; i++)
	{
		if (_sendQ.Dequeue(_arrPtrPacket[i]) == false)
			break;

		arrWSABuf[i].buf = (CHAR*)_arrPtrPacket[i]->GetHeaderPtr();
		arrWSABuf[i].len = _arrPtrPacket[i]->GetUseSize();
		numPacket++;
	}
	// 세션의 numSendPacket 설정
	_numSendPacket = numPacket;
	// 보낼 데이터가 없으면 종료
	if (numPacket == 0)
	{
		_sendIoFlag = false;
		return;
	}

	// overlapped 구조체 설정
	ZeroMemory(&_sendOverlapped, sizeof(LAN_OVERLAPPED_EX));
	_sendOverlapped.ioType = LAN_IO_SEND;

	// send
	int retSend = WSASend(_socket, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&_sendOverlapped, NULL);
	InterlockedAdd64(&_sendAsyncCount, numPacket);  // 모니터링

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 완료통지가 가지않을 것이므로 여기서 직렬화버퍼의 count를 내린다.
			for (int i = 0; i < _numSendPacket; i++)
			{
				_arrPtrPacket[i]->SubUseCount();
			}

			_sendIoFlag = false;
			if (error == WSAECONNRESET                                              // 서버에서 연결을 끊음
				|| error == WSAECONNABORTED                                         // 클라이언트에서 연결을 끊음?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))     // 클라이언트에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 모니터링
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSASend failed with known error. error:%d\n", error);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError); // 모니터링
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSASend failed with unknown error. error:%d\n", error);
			}

			// 오류 발생시 클라이언트의 연결을 끊는다.
			Disconnect();
			return;
		}
	}

	return;
}




/* 패킷 암호화 */
void CLANClient::EncryptPacket(CPacket* pPacket)
{
	if (pPacket->IsEncoded() == true)
		return;

	LANPacketHeader* pHeader = (LANPacketHeader*)pPacket->GetHeaderPtr();
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

bool CLANClient::DecipherPacket(CPacket* pPacket)
{
	LANPacketHeader* pHeader = (LANPacketHeader*)pPacket->GetHeaderPtr();
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










// worker 스레드
unsigned WINAPI CLANClient::ThreadWorker(PVOID pParam)
{
	CLANClient& client = *(CLANClient*)pParam;
	if (client._bOutputSystem)
		client.OnOutputSystem(L"[LAN Client] worker thread begin. id:%u\n", GetCurrentThreadId());
	client.UpdateWorkerThread();

	if (client._bOutputSystem)
		client.OnOutputSystem(L"[LAN Client] worker thread end. id:%u\n", GetCurrentThreadId());

	return 0;
}

// worker 스레드 업데이트
void CLANClient::UpdateWorkerThread()
{
	DWORD numByteTrans;
	ULONG_PTR compKey;
	LAN_OVERLAPPED_EX* pOverlapped;
	CPacket* pRecvPacket;
	// IOCP 완료통지 처리
	while (true)
	{
		numByteTrans = 0;
		compKey = NULL;
		pOverlapped = nullptr;
		BOOL retGQCS = GetQueuedCompletionStatus(_hIOCP, &numByteTrans, &compKey, (OVERLAPPED**)&pOverlapped, INFINITE);

		DWORD error = GetLastError();
		DWORD WSAError = WSAGetLastError();

		// IOCP dequeue 실패, 또는 timeout. 이 경우 numByteTrans, compKey 변수에는 값이 들어오지 않기 때문에 오류체크에 사용할 수 없다.
		if (retGQCS == 0 && pOverlapped == nullptr)
		{
			// timeout
			if (error == WAIT_TIMEOUT)
			{
				OnError(L"[LAN Client] GetQueuedCompletionStatus timeout. error:%u\n", error);
			}
			// error
			else
			{
				OnError(L"[LAN Client] GetQueuedCompletionStatus failed. error:%u\n", error);
			}
			break;
		}

		// 스레드 종료 메시지를 받음
		else if (retGQCS != 0 && pOverlapped == nullptr)
		{
			break;
		}

		// PostInnerRequest 함수에 의해 내부적으로 삽입된 완료통지를 받음
		else if (pOverlapped == (LAN_OVERLAPPED_EX*)1)
		{
			CPacket* pInnerPacket = (CPacket*)compKey;
			OnInnerRequest(*pInnerPacket);
		}

		// SendPacketAsync 함수에 의해서 SnedPost 요청을 받음
		else if (pOverlapped == (LAN_OVERLAPPED_EX*)2)
		{
			SendPostAsync();
		}

		// recv 완료통지 처리
		else if (pOverlapped->ioType == LAN_IO_RECV)
		{
			_recvCompletionCount++;

			bool bRecvSucceed = true;  // recv 성공 여부
			// recv IO가 실패한 경우
			if (retGQCS == 0)
			{
				bRecvSucceed = false;  // 추가
				if (error == ERROR_NETNAME_DELETED     // ERROR_NETNAME_DELETED 64 는 WSAECONNRESET 와 동일하다?
					|| error == ERROR_CONNECTION_ABORTED  // ERROR_CONNECTION_ABORTED 1236 은 클라이언트에서 연결을 끊었을 경우 발생함. WSAECONNABORTED 10053 와 동일?
					|| error == ERROR_OPERATION_ABORTED)  // ERROR_OPERATION_ABORTED 995 는 비동기 IO가 진행중에 클라이언트에서 연결을 끊었을 경우 발생함.
				{
					LAN_OUTPUT_DEBUG(L"[LAN Client] recv socket error. error:%u, WSAError:%u, numByteTrans:%d\n", error, WSAError, numByteTrans);
					_disconnByKnownRecvIoError++;
				}
				else
				{
					LAN_OUTPUT_SYSTEM(L"[LAN Client] recv socket unknown error. error:%u, WSAError:%u, numByteTrans:%d\n", error, WSAError, numByteTrans);
					_disconnByUnknownRecvIoError++;
				}
			}

			// 서버로부터 연결끊김 메시지를 받은 경우
			else if (numByteTrans == 0)
			{
				bRecvSucceed = false;
				LAN_OUTPUT_DEBUG(L"[LAN Client] recv closed by client's close request.\n");
				_disconnByNormalProcess++;
			}

			// 정상적인 메시지 처리
			else if (numByteTrans > 0)
			{
				// recvQ 내의 모든 메시지를 처리한다.
				char bufferHeader[sizeof(LANPacketHeader)];  // 헤더를 읽을 버퍼
				_recvQ.MoveRear(numByteTrans);
				while (true)
				{
					if (_recvQ.GetUseSize() < sizeof(LANPacketHeader)) // 데이터가 헤더길이보다 작음
						break;

					// 헤더를 읽음
					_recvQ.Peek(bufferHeader, sizeof(LANPacketHeader));
					LANPacketHeader header = *(LANPacketHeader*)bufferHeader;          // ? 여기서 header 변수에 버퍼내용을 다시집어넣는 이유는??
					if (header.code != _packetCode) // 패킷 코드가 잘못됐을 경우 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] header.code is not valid. code:%d\n", header.code);
						bRecvSucceed = false;
						_disconnByPacketCode++;
						break;
					}

					if (header.len > _maxPacketSize) // 데이터 크기가 최대 패킷크기보다 큰 경우 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] header.len is larger than max packet size. len:%d, max packet size:%d\n", header.len, _maxPacketSize);
						bRecvSucceed = false;
						_disconnByPacketLength++;
						break;
					}

					if (sizeof(LANPacketHeader) + header.len > LAN_SIZE_RECV_BUFFER) // 데이터의 크기가 버퍼 크기보다 클 경우 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] packet data length is longer than recv buffer. len:%d, size of ringbuffer:%d\n"
							, header.len, _recvQ.GetSize());
						bRecvSucceed = false;
						_disconnByPacketLength++;
						break;
					}

					// 버퍼내의 데이터 크기 확인
					if (_recvQ.GetUseSize() < sizeof(LANPacketHeader) + header.len) // 데이터가 모두 도착하지 않았음
					{
						break;
					}

					// 직렬화버퍼 준비
					pRecvPacket = CPacket::AllocPacket();
					pRecvPacket->Init(sizeof(LANPacketHeader));

					// 직렬화 버퍼로 데이터를 읽음
					_recvQ.MoveFront(sizeof(LANPacketHeader));
					_recvQ.Dequeue(pRecvPacket->GetDataPtr(), header.len);
					pRecvPacket->MoveWritePos(header.len);

					// 복호화
					if (_bUsePacketEncryption == true)
					{
						pRecvPacket->PutHeader((const char*)&header); // 복호화를 위해 헤더를 넣어줌
						bool retDecipher = DecipherPacket(pRecvPacket);
						if (retDecipher == false) // 복호화에 실패함(CheckSum이 잘못됨)
						{
							LAN_OUTPUT_DEBUG(L"[LAN Client] packet decoding failed.\n");
							pRecvPacket->SubUseCount();
							bRecvSucceed = false;
							_disconnByPacketDecode++;
							break;
						}
					}

					LAN_OUTPUT_DEBUG(L"[LAN Client] recved. data len:%d, packet type:%d\n", header.len, *(WORD*)pRecvPacket->GetDataPtr());

					// 사용자 패킷처리 함수 호출
					OnRecv(*pRecvPacket);

					// 사용카운트 감소
					pRecvPacket->SubUseCount();

				}
			}

			// numByteTrans가 0보다 작음. error?
			else
			{
				bRecvSucceed = false;  // 추가
				OnError(L"[LAN Client] recv error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u\n", numByteTrans, error, WSAError);
				Crash();
			}

			// recv를 성공적으로 끝냈으면 다시 recv 한다. 실패했으면 closesocket
			if (bRecvSucceed)
				RecvPost();
			else
				Disconnect();
		}

		// send IO 완료통지 처리
		else if (pOverlapped->ioType == LAN_IO_SEND)
		{
			_sendCompletionCount++;

			// send에 사용한 직렬화버퍼의 사용카운트를 감소시킨다.
			for (int i = 0; i < _numSendPacket; i++)
			{
				long useCount = _arrPtrPacket[i]->SubUseCount();
			}

			// sendIoFlag 를 해제한다.
			// 직렬화버퍼의 사용카운트를 감소시킨다음 sendIoFlag를 해제해야 _arrPtrPacket 에 데이터가 덮어씌워지는일이 없다.
			_sendIoFlag = false;

			// send IO가 실패한 경우
			if (retGQCS == 0)
			{
				if (error == ERROR_NETNAME_DELETED        // ERROR_NETNAME_DELETED 64 는 WSAECONNRESET 와 동일하다?
					|| error == ERROR_CONNECTION_ABORTED  // ERROR_CONNECTION_ABORTED 1236 은 클라이언트에서 연결을 끊었을 경우 발생함. WSAECONNABORTED 10053 와 동일?
					|| error == ERROR_OPERATION_ABORTED)  // ERROR_OPERATION_ABORTED 995 는 비동기 IO가 진행중에 클라이언트에서 연결을 끊었을 경우 발생함.
				{
					LAN_OUTPUT_DEBUG(L"[LAN Client] send socket known error. error:%u, WSAError:%u, numByteTrans:%d\n", error, WSAError, numByteTrans);
					_disconnByKnownSendIoError++;
				}
				else
				{
					LAN_OUTPUT_SYSTEM(L"[LAN Client] send socket unknown error. NumberOfBytesTransferred:%d, error:%u, WSAError:%u\n", numByteTrans, error, WSAError);
					_disconnByUnknownSendIoError++;
				}

				Disconnect();
			}
			// 완료통지 처리 
			else if (numByteTrans > 0)
			{
				// sendQ 내의 데이터 전송을 시도함
				SendPost();
			}
			// numByteTrans가 0보다 작음. error?
			else
			{
				OnError(L"[LAN Client] send error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u\n", numByteTrans, error, WSAError);
				Disconnect();
				Crash();
			}
		}
		else
		{
			// overlapped 구조체의 IO Type이 올바르지 않음
			OnError(L"[LAN Client] IO Type error. ioType:%d\n", pOverlapped->ioType);
			Disconnect();
			// crash
			Crash();
		}
	}
}




/* Crash */
void CLANClient::Crash()
{
	int* p = 0;
	*p = 0;
}











