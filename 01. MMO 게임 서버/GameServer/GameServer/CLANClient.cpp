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

	// IOCP 积己
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 1);
	if (_hIOCP == NULL)
	{
		OnError(L"[LAN Client] Failed to create IOCP. error:%u\n", GetLastError());
		return false;
	}

	// 家南 积己
	_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// SO_LINGER 可记 汲沥
	LINGER linger;
	linger.l_onoff = 1;
	linger.l_linger = 0;
	setsockopt(_socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	// nagle 可记 秦力 咯何
	if (_bUseNagle == false)
	{
		DWORD optval = TRUE;
		int retSockopt = setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
		if (retSockopt == SOCKET_ERROR)
		{
			OnError(L"[LAN Client] Failed to set TCP_NODELAY option on listen socket. error:%u\n", WSAGetLastError());
		}
	}

	// 辑滚俊 楷搬
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

	// 家南苞 IOCP 楷搬
	if (CreateIoCompletionPort((HANDLE)_socket, _hIOCP, 0, 0) == NULL)
	{
		OnError(L"[LAN Client] failed to associate socket with IOCP\n");
		closesocket(_socket);
		return false;
	}


	// worker 胶饭靛 积己
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


	// recv 矫累
	RecvPost();

	return true;
}





// 匙飘况农 辆丰
void CLANClient::Shutdown()
{
	// 辑滚客狼 楷搬阑 谗澜
	Disconnect();

	// worker 胶饭靛 辆丰 皋矫瘤甫 焊辰促.
	PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);

	// worker 胶饭靛啊 辆丰登扁甫 1檬埃 扁促赴促.
	ULONGLONG timeout = 1000;
	BOOL retWait = WaitForSingleObject(_hThreadWorker, (DWORD)timeout);
	if (retWait != WAIT_OBJECT_0)
	{
		LAN_OUTPUT_SYSTEM(L"[LAN Client] Timeout occurred while waiting for the thread to be terminated. force terminate it. error:%u\n", GetLastError());
		TerminateThread(_hThreadWorker, 0);
	}

	// 按眉 昏力
	CPacket* pPacket;
	while (_sendQ.Dequeue(pPacket) == false);
	CloseHandle(_hThreadWorker);
	CloseHandle(_hIOCP);
	// WSACleanup 篮 促弗 匙飘况农俊 康氢阑 固磨荐 乐扁 锭巩俊 龋免窍瘤 臼澜.

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
		// 庆歹 积己
		header.code = _packetCode;
		header.len = pPacket->GetDataSize();
		header.randKey = (BYTE)rand();

		char* packetDataPtr;
		if (_bUsePacketEncryption == true)
		{
			packetDataPtr = pPacket->GetDataPtr();  // CheckSum 拌魂
			header.checkSum = 0;
			for (int i = 0; i < header.len; i++)
				header.checkSum += packetDataPtr[i];
		}

		// 流纺拳滚欺俊 庆歹 涝仿
		pPacket->PutHeader((char*)&header);
	}

	// 菩哦 鞠龋拳
	if (_bUsePacketEncryption == true)
		EncryptPacket(pPacket);

	// send lcokfree queue俊 流纺拳滚欺 涝仿
	pPacket->AddUseCount();
	_sendQ.Enqueue(pPacket);

	// Send甫 矫档窃
	SendPost();

	return true;
}




bool CLANClient::SendPacketAsync(CPacket* pPacket)
{
	LANPacketHeader header;
	if (pPacket->IsHeaderSet() == false)
	{
		// 庆歹 积己
		header.code = _packetCode;
		header.len = pPacket->GetDataSize();
		header.randKey = (BYTE)rand();

		char* packetDataPtr;
		if (_bUsePacketEncryption == true)
		{
			packetDataPtr = pPacket->GetDataPtr();  // CheckSum 拌魂
			header.checkSum = 0;
			for (int i = 0; i < header.len; i++)
				header.checkSum += packetDataPtr[i];
		}

		// 流纺拳滚欺俊 庆歹 涝仿
		pPacket->PutHeader((char*)&header);
	}

	// 菩哦 鞠龋拳
	if (_bUsePacketEncryption == true)
		EncryptPacket(pPacket);

	// send lcokfree queue俊 流纺拳滚欺 涝仿
	pPacket->AddUseCount();
	_sendQ.Enqueue(pPacket);

	// 泅犁 send啊 柳青吝牢瘤 犬牢
	if (_sendQ.Size() == 0) // 焊尘 菩哦捞 绝栏搁 辆丰
	{
		return true;
	}
	else if ((bool)InterlockedExchange8((char*)&_sendIoFlag, true) == true) // 郴啊 sendIoFlag甫 false俊辑 true肺 官操菌阑 锭父 send窃
	{
		return true;
	}

	// 泅犁 send啊 柳青吝捞 酒聪扼搁 SendPost 夸没阑 IOCP 钮俊 火涝窃
	BOOL retPost = PostQueuedCompletionStatus(_hIOCP, 0, 0, (LPOVERLAPPED)2);
	if (retPost == 0)
	{
		OnError(L"[LAN Client] Failed to post send request to IOCP. error:%u\n", GetLastError());
		return false;
	}

	return true;
}



// IOCP 肯丰烹瘤 钮俊 累诀阑 火涝茄促. 累诀捞 裙垫登搁 OnInnerRequest 窃荐啊 龋免等促.
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
// 64byte aligned 按眉 积己阑 困茄 new, delete overriding
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
	// isClosed甫 true肺 弥檬肺 官操菌阑 锭父 荐青窃
	if (InterlockedExchange8((char*)&_isClosed, true) == false)
	{
		// 家南阑 摧绰促.
		closesocket(_socket);
		_socket = INVALID_SOCKET;
		InterlockedIncrement64(&_disconnectCount);  // 葛聪磐傅
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
	InterlockedIncrement64(&_recvCount); // 葛聪磐傅

	if (retRecv == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			if (error == WSAECONNRESET                                     // 辑滚俊辑 楷搬阑 谗澜
				|| error == WSAECONNABORTED                                // 努扼捞攫飘俊辑 楷搬阑 谗澜?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))    // 努扼捞攫飘俊辑 家南阑 摧绊 家南蔼阑 INVALID_SOCKET栏肺 函版窃
			{
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSARecv failed by known error. error:%d\n", error);
				InterlockedIncrement64(&_WSARecvKnownError);  // 葛聪磐傅
			}
			else
			{
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSARecv failed by known error. error:%d\n", error);
				InterlockedIncrement64(&_WSARecvUnknownError);  // 葛聪磐傅
			}

			// 坷幅 惯积矫 技记狼 楷搬阑 谗绰促.
			Disconnect();
			return;
		}
	}

	return;
}



// WSASend甫 矫档茄促. 
void CLANClient::SendPost()
{
	if (_sendQ.Size() == 0) // 焊尘 菩哦捞 绝栏搁 辆丰
	{
		return;
	}
	else if ((bool)InterlockedExchange8((char*)&_sendIoFlag, true) == true) // 郴啊 sendIoFlag甫 false俊辑 true肺 官操菌阑 锭父 send窃
	{
		return;
	}

	// sendQ俊辑 流纺拳滚欺 林家甫 哗郴绢 WSABUF 备炼眉俊 持绰促.
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
	// 技记狼 numSendPacket 汲沥
	_numSendPacket = numPacket;
	// 焊尘 单捞磐啊 绝栏搁 辆丰
	if (numPacket == 0)
	{
		_sendIoFlag = false;
		return;
	}

	// overlapped 备炼眉 汲沥
	ZeroMemory(&_sendOverlapped, sizeof(LAN_OVERLAPPED_EX));
	_sendOverlapped.ioType = LAN_IO_SEND;

	// send
	int retSend = WSASend(_socket, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&_sendOverlapped, NULL);
	InterlockedAdd64(&_sendCount, numPacket);  // 葛聪磐傅

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 肯丰烹瘤啊 啊瘤臼阑 巴捞骨肺 咯扁辑 流纺拳滚欺狼 count甫 郴赴促.
			for (int i = 0; i < _numSendPacket; i++)
			{
				_arrPtrPacket[i]->SubUseCount();
			}

			_sendIoFlag = false;
			if (error == WSAECONNRESET                                              // 辑滚俊辑 楷搬阑 谗澜
				|| error == WSAECONNABORTED                                         // 努扼捞攫飘俊辑 楷搬阑 谗澜?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))     // 努扼捞攫飘俊辑 家南阑 摧绊 家南蔼阑 INVALID_SOCKET栏肺 函版窃
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 葛聪磐傅
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSASend failed with known error. error:%d\n", error);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError); // 葛聪磐傅
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSASend failed with unknown error. error:%d\n", error);
			}

			// 坷幅 惯积矫 努扼捞攫飘狼 楷搬阑 谗绰促.
			Disconnect();
			return;
		}
	}

	return;
}


// SendPacketAsync 窃荐甫 烹秦 厚悼扁 send 夸没阑 罐疽阑 锭 worker 胶饭靛 郴俊辑 龋免等促. WSASend甫 龋免茄促.
void CLANClient::SendPostAsync()
{
	// 捞 窃荐啊 龋免登绰 矫痢篮 SendPacketAsync 窃荐俊辑 sendIoFlag甫 true肺 函版茄 惑怕捞促. 弊贰辑 sendIoFlag甫 八荤窍瘤 臼绰促.

	// sendQ俊辑 流纺拳滚欺 林家甫 哗郴绢 WSABUF 备炼眉俊 持绰促.
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
	// 技记狼 numSendPacket 汲沥
	_numSendPacket = numPacket;
	// 焊尘 单捞磐啊 绝栏搁 辆丰
	if (numPacket == 0)
	{
		_sendIoFlag = false;
		return;
	}

	// overlapped 备炼眉 汲沥
	ZeroMemory(&_sendOverlapped, sizeof(LAN_OVERLAPPED_EX));
	_sendOverlapped.ioType = LAN_IO_SEND;

	// send
	int retSend = WSASend(_socket, arrWSABuf, numPacket, NULL, 0, (OVERLAPPED*)&_sendOverlapped, NULL);
	InterlockedAdd64(&_sendAsyncCount, numPacket);  // 葛聪磐傅

	if (retSend == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			// Send 肯丰烹瘤啊 啊瘤臼阑 巴捞骨肺 咯扁辑 流纺拳滚欺狼 count甫 郴赴促.
			for (int i = 0; i < _numSendPacket; i++)
			{
				_arrPtrPacket[i]->SubUseCount();
			}

			_sendIoFlag = false;
			if (error == WSAECONNRESET                                              // 辑滚俊辑 楷搬阑 谗澜
				|| error == WSAECONNABORTED                                         // 努扼捞攫飘俊辑 楷搬阑 谗澜?
				|| (error == WSAENOTSOCK && _socket == INVALID_SOCKET))     // 努扼捞攫飘俊辑 家南阑 摧绊 家南蔼阑 INVALID_SOCKET栏肺 函版窃
			{
				InterlockedIncrement64(&_WSASendKnownError);  // 葛聪磐傅
				LAN_OUTPUT_DEBUG(L"[LAN Client] WSASend failed with known error. error:%d\n", error);
			}
			else
			{
				InterlockedIncrement64(&_WSASendUnknownError); // 葛聪磐傅
				LAN_OUTPUT_SYSTEM(L"[LAN Client] WSASend failed with unknown error. error:%d\n", error);
			}

			// 坷幅 惯积矫 努扼捞攫飘狼 楷搬阑 谗绰促.
			Disconnect();
			return;
		}
	}

	return;
}




/* 菩哦 鞠龋拳 */
void CLANClient::EncryptPacket(CPacket* pPacket)
{
	if (pPacket->IsEncoded() == true)
		return;

	LANPacketHeader* pHeader = (LANPacketHeader*)pPacket->GetHeaderPtr();
	char* pTargetData = pPacket->GetDataPtr() - 1;  // checksum阑 器窃窍咯 鞠龋拳

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
	char* pTargetData = pPacket->GetDataPtr() - 1;  // checksum阑 器窃窍咯 汗龋拳

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

	// CheckSum 拌魂
	char* payloadPtr = pPacket->GetDataPtr();
	BYTE checkSum = 0;
	for (int i = 0; i < pHeader->len; i++)
		checkSum += payloadPtr[i];

	// CheckSum 八刘
	if (pHeader->checkSum == checkSum)
		return true;
	else
		return false;
}










// worker 胶饭靛
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

// worker 胶饭靛 诀单捞飘
void CLANClient::UpdateWorkerThread()
{
	DWORD numByteTrans;
	ULONG_PTR compKey;
	LAN_OVERLAPPED_EX* pOverlapped;
	CPacket* pRecvPacket;
	// IOCP 肯丰烹瘤 贸府
	while (true)
	{
		numByteTrans = 0;
		compKey = NULL;
		pOverlapped = nullptr;
		BOOL retGQCS = GetQueuedCompletionStatus(_hIOCP, &numByteTrans, &compKey, (OVERLAPPED**)&pOverlapped, INFINITE);

		DWORD error = GetLastError();
		DWORD WSAError = WSAGetLastError();

		// IOCP dequeue 角菩, 肚绰 timeout. 捞 版快 numByteTrans, compKey 函荐俊绰 蔼捞 甸绢坷瘤 臼扁 锭巩俊 坷幅眉农俊 荤侩且 荐 绝促.
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

		// 胶饭靛 辆丰 皋矫瘤甫 罐澜
		else if (retGQCS != 0 && pOverlapped == nullptr)
		{
			break;
		}

		// PostInnerRequest 窃荐俊 狼秦 郴何利栏肺 火涝等 肯丰烹瘤甫 罐澜
		else if (pOverlapped == (LAN_OVERLAPPED_EX*)1)
		{
			CPacket* pInnerPacket = (CPacket*)compKey;
			OnInnerRequest(*pInnerPacket);
		}

		// SendPacketAsync 窃荐俊 狼秦辑 SnedPost 夸没阑 罐澜
		else if (pOverlapped == (LAN_OVERLAPPED_EX*)2)
		{
			SendPostAsync();
		}

		// recv 肯丰烹瘤 贸府
		else if (pOverlapped->ioType == LAN_IO_RECV)
		{
			_recvCompletionCount++;

			bool bRecvSucceed = true;  // recv 己傍 咯何
			// recv IO啊 角菩茄 版快
			if (retGQCS == 0)
			{
				bRecvSucceed = false;  // 眠啊
				if (error == ERROR_NETNAME_DELETED     // ERROR_NETNAME_DELETED 64 绰 WSAECONNRESET 客 悼老窍促?
					|| error == ERROR_CONNECTION_ABORTED  // ERROR_CONNECTION_ABORTED 1236 篮 努扼捞攫飘俊辑 楷搬阑 谗菌阑 版快 惯积窃. WSAECONNABORTED 10053 客 悼老?
					|| error == ERROR_OPERATION_ABORTED)  // ERROR_OPERATION_ABORTED 995 绰 厚悼扁 IO啊 柳青吝俊 努扼捞攫飘俊辑 楷搬阑 谗菌阑 版快 惯积窃.
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

			// 辑滚肺何磐 楷搬谗辫 皋矫瘤甫 罐篮 版快
			else if (numByteTrans == 0)
			{
				bRecvSucceed = false;
				LAN_OUTPUT_DEBUG(L"[LAN Client] recv closed by client's close request.\n");
				_disconnByNormalProcess++;
			}

			// 沥惑利牢 皋矫瘤 贸府
			else if (numByteTrans > 0)
			{
				// recvQ 郴狼 葛电 皋矫瘤甫 贸府茄促.
				char bufferHeader[sizeof(LANPacketHeader)];  // 庆歹甫 佬阑 滚欺
				_recvQ.MoveRear(numByteTrans);
				while (true)
				{
					if (_recvQ.GetUseSize() < sizeof(LANPacketHeader)) // 单捞磐啊 庆歹辨捞焊促 累澜
						break;

					// 庆歹甫 佬澜
					_recvQ.Peek(bufferHeader, sizeof(LANPacketHeader));
					LANPacketHeader header = *(LANPacketHeader*)bufferHeader;          // ? 咯扁辑 header 函荐俊 滚欺郴侩阑 促矫笼绢持绰 捞蜡绰??
					if (header.code != _packetCode) // 菩哦 内靛啊 肋给灯阑 版快 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] header.code is not valid. code:%d\n", header.code);
						bRecvSucceed = false;
						_disconnByPacketCode++;
						break;
					}

					if (header.len > _maxPacketSize) // 单捞磐 农扁啊 弥措 菩哦农扁焊促 奴 版快 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] header.len is larger than max packet size. len:%d, max packet size:%d\n", header.len, _maxPacketSize);
						bRecvSucceed = false;
						_disconnByPacketLength++;
						break;
					}

					if (sizeof(LANPacketHeader) + header.len > LAN_SIZE_RECV_BUFFER) // 单捞磐狼 农扁啊 滚欺 农扁焊促 努 版快 error
					{
						LAN_OUTPUT_DEBUG(L"[LAN Client] packet data length is longer than recv buffer. len:%d, size of ringbuffer:%d\n"
							, header.len, _recvQ.GetSize());
						bRecvSucceed = false;
						_disconnByPacketLength++;
						break;
					}

					// 滚欺郴狼 单捞磐 农扁 犬牢
					if (_recvQ.GetUseSize() < sizeof(LANPacketHeader) + header.len) // 单捞磐啊 葛滴 档馒窍瘤 臼疽澜
					{
						break;
					}

					// 流纺拳滚欺 霖厚
					pRecvPacket = CPacket::AllocPacket();
					pRecvPacket->Init(sizeof(LANPacketHeader));

					// 流纺拳 滚欺肺 单捞磐甫 佬澜
					_recvQ.MoveFront(sizeof(LANPacketHeader));
					_recvQ.Dequeue(pRecvPacket->GetDataPtr(), header.len);
					pRecvPacket->MoveWritePos(header.len);

					// 汗龋拳
					if (_bUsePacketEncryption == true)
					{
						pRecvPacket->PutHeader((const char*)&header); // 汗龋拳甫 困秦 庆歹甫 持绢淋
						bool retDecipher = DecipherPacket(pRecvPacket);
						if (retDecipher == false) // 汗龋拳俊 角菩窃(CheckSum捞 肋给凳)
						{
							LAN_OUTPUT_DEBUG(L"[LAN Client] packet decoding failed.\n");
							pRecvPacket->SubUseCount();
							bRecvSucceed = false;
							_disconnByPacketDecode++;
							break;
						}
					}

					LAN_OUTPUT_DEBUG(L"[LAN Client] recved. data len:%d, packet type:%d\n", header.len, *(WORD*)pRecvPacket->GetDataPtr());

					// 荤侩磊 菩哦贸府 窃荐 龋免
					OnRecv(*pRecvPacket);

					// 荤侩墨款飘 皑家
					pRecvPacket->SubUseCount();

				}
			}

			// numByteTrans啊 0焊促 累澜. error?
			else
			{
				bRecvSucceed = false;  // 眠啊
				OnError(L"[LAN Client] recv error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u\n", numByteTrans, error, WSAError);
				Crash();
			}

			// recv甫 己傍利栏肺 场陈栏搁 促矫 recv 茄促. 角菩沁栏搁 closesocket
			if (bRecvSucceed)
				RecvPost();
			else
				Disconnect();
		}

		// send IO 肯丰烹瘤 贸府
		else if (pOverlapped->ioType == LAN_IO_SEND)
		{
			_sendCompletionCount++;

			// send俊 荤侩茄 流纺拳滚欺狼 荤侩墨款飘甫 皑家矫挪促.
			for (int i = 0; i < _numSendPacket; i++)
			{
				long useCount = _arrPtrPacket[i]->SubUseCount();
			}

			// sendIoFlag 甫 秦力茄促.
			// 流纺拳滚欺狼 荤侩墨款飘甫 皑家矫挪促澜 sendIoFlag甫 秦力秦具 _arrPtrPacket 俊 单捞磐啊 丹绢竞况瘤绰老捞 绝促.
			_sendIoFlag = false;

			// send IO啊 角菩茄 版快
			if (retGQCS == 0)
			{
				if (error == ERROR_NETNAME_DELETED        // ERROR_NETNAME_DELETED 64 绰 WSAECONNRESET 客 悼老窍促?
					|| error == ERROR_CONNECTION_ABORTED  // ERROR_CONNECTION_ABORTED 1236 篮 努扼捞攫飘俊辑 楷搬阑 谗菌阑 版快 惯积窃. WSAECONNABORTED 10053 客 悼老?
					|| error == ERROR_OPERATION_ABORTED)  // ERROR_OPERATION_ABORTED 995 绰 厚悼扁 IO啊 柳青吝俊 努扼捞攫飘俊辑 楷搬阑 谗菌阑 版快 惯积窃.
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
			// 肯丰烹瘤 贸府 
			else if (numByteTrans > 0)
			{
				// sendQ 郴狼 单捞磐 傈价阑 矫档窃
				SendPost();
			}
			// numByteTrans啊 0焊促 累澜. error?
			else
			{
				OnError(L"[LAN Client] send error. NumberOfBytesTransferred is %d, error:%u, WSAError:%u\n", numByteTrans, error, WSAError);
				Disconnect();
				Crash();
			}
		}
		else
		{
			// overlapped 备炼眉狼 IO Type捞 棵官福瘤 臼澜
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











