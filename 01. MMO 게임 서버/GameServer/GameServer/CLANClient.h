#pragma once

#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <process.h>
#include <vector>
#include <unordered_map>

#include "CPacket.h"
#include "CRingbuffer.h"
#include "CLockfreeQueue.h"

#include "defineLANClient.h"

// OnOutputDebug 함수 호출 매크로
#define LAN_OUTPUT_DEBUG(szOutputFormat, ...) do { \
	if (_bOutputDebug) \
		OnOutputDebug(szOutputFormat, ##__VA_ARGS__); \
} while(0) 

// OnOutputSystem 함수 호출 매크로
#define LAN_OUTPUT_SYSTEM(szOutputFormat, ...) do { \
	if (_bOutputSystem) \
		OnOutputSystem(szOutputFormat, ##__VA_ARGS__); \
} while(0) 

/* Network */
class alignas(64) CLANClient
{
private:
	/* network */
	HANDLE _hIOCP;
	SOCKET _socket;

	/* config */
	wchar_t _szServerIP[16];
	unsigned short _serverPort;
	bool _bUseNagle;
	bool _bUsePacketEncryption;  // 패킷 암호화 사용 여부.

	BYTE _packetCode;
	BYTE _packetKey;
	int  _maxPacketSize;

	/* thread */
	HANDLE _hThreadWorker;
	unsigned int _idThreadWorker;

	/* client */
	CLockfreeQueue<CPacket*> _sendQ;
	CRingbuffer _recvQ;
	LAN_OVERLAPPED_EX _sendOverlapped;
	LAN_OVERLAPPED_EX _recvOverlapped;
	CPacket* _arrPtrPacket[LAN_CLIENT_SIZE_ARR_PACKTE];
	int _numSendPacket;

	volatile bool _sendIoFlag;
	volatile bool _isClosed;

	/* output */
	bool _bOutputDebug;    // OnOutputDebug 함수 활성화여부(default:false)
	bool _bOutputSystem;   // OnOutputSystem 함수 활성화여부(default:true)

	/* 모니터링 */
	volatile __int64 _sendCompletionCount = 0;    // send 완료통지 처리횟수
	volatile __int64 _recvCompletionCount = 0;    // recv 완료통지 처리횟수
	volatile __int64 _disconnByKnownRecvIoError = 0;    // recv 완료통지에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
	volatile __int64 _disconnByUnknownRecvIoError = 0;  // recv 완료통지에서 오류 발생(원인불명)
	volatile __int64 _disconnByKnownSendIoError = 0;    // send 완료통지에서 오류 발생
	volatile __int64 _disconnByUnknownSendIoError = 0;  // send 완료통지에서 오류 발생(원인불명)
	volatile __int64 _disconnByNormalProcess = 0;        // 서버가 정상적으로 연결을 끊음
	volatile __int64 _disconnByPacketCode = 0;    // 패킷코드가 잘못됨
	volatile __int64 _disconnByPacketLength = 0;  // 패킷길이가 잘못됨
	volatile __int64 _disconnByPacketDecode = 0;  // 패킷 디코딩에 실패함

	volatile __int64 _sendCount = 0;              // SendPost 함수에서 WSASend 함수로 전송한 패킷의 수
	volatile __int64 _sendAsyncCount = 0;         // SendPostAsync 함수에서 WSASend 함수로 전송한 패킷의 수
	volatile __int64 _recvCount = 0;              // WSARecv 함수 호출 횟수
	volatile __int64 _disconnectCount = 0;        // disconnect 횟수 (세션 release 횟수)
	volatile __int64 _WSARecvKnownError = 0;      // WSARecv 함수에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
	volatile __int64 _WSARecvUnknownError = 0;    // WSARecv 함수에서 오류 발생(원인불명)
	volatile __int64 _WSASendKnownError = 0;      // WSASend 함수에서 오류 발생
	volatile __int64 _WSASendUnknownError = 0;    // WSASend 함수에서 오류 발생(원인불명)


public:
	CLANClient();
	virtual ~CLANClient();

public:
	/* Set */
	void SetOutputDebug(bool enable) { _bOutputDebug = enable; }    // OnOutputDebug 함수 활성화, 비활성화 (기본값:비활성)
	void SetOutputSystem(bool enable) { _bOutputSystem = enable; }  // OnOutputSystem 함수 활성화, 비활성화 (기본값:활성)

	/* Get 서버 상태 */
	int GetPacketPoolSize() { return CPacket::GetPoolSize(); };
	int GetPacketAllocCount() { return CPacket::GetAllocCount(); }
	int GetPacketActualAllocCount() { return CPacket::GetActualAllocCount(); }
	int GetPacketFreeCount() { return CPacket::GetFreeCount(); }

	/* Get 모니터링 */
	__int64 GetSendCompletionCount() { return _sendCompletionCount; }         // send 완료통지 처리횟수
	__int64 GetRecvCompletionCount() { return _recvCompletionCount; }         // recv 완료통지 처리횟수
	__int64 GetSendCount() { return _sendCount + _sendAsyncCount; }           // WSASend 함수로 전송한 패킷의 수
	__int64 GetRecvCount() { return _recvCount; }                             // WSARecv 함수 호출 횟수
	__int64 GetDisconnectCount() { return _disconnectCount; }                 // disconnect 횟수 (세션 release 횟수)
	__int64 GetDisconnByKnownIoError() { return _disconnByKnownRecvIoError + _disconnByKnownSendIoError + _WSARecvKnownError + _WSASendKnownError; }             // WSARecv, WSASend, recv 완료통지, send 완료통지에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
	__int64 GetDisconnByUnknownIoError() { return _disconnByUnknownRecvIoError + _disconnByUnknownSendIoError + _WSARecvUnknownError + _WSASendUnknownError; }   // WSARecv, WSASend, recv 완료통지, send 완료통지에서 오류 발생(원인불명)
	__int64 GetDisconnByNormalProcess() { return _disconnByNormalProcess; }      // 클라이언트가 정상적으로 연결을 끊음
	__int64 GetDisconnByPacketCode() { return _disconnByPacketCode; }            // 패킷코드가 잘못됨
	__int64 GetDisconnByPacketLength() { return _disconnByPacketLength; }        // 패킷길이가 잘못됨
	__int64 GetDisconnByPacketDecode() { return _disconnByPacketDecode; }        // 패킷 디코딩에 실패함

	/* client */
	bool StartUp(const wchar_t* serverIP, unsigned short serverPort, bool bUseNagle, BYTE packetCode, BYTE packetKey, int maxPacketSize, bool bUsePacketEncryption);
	void Shutdown();    // 네트워크 종료
	bool Disconnect();

	/* packet */
	CPacket* AllocPacket();
	bool SendPacket(CPacket* pPacket);  // send가 동기로 수행될 수 있을 때는 동기로 수행되고, 그렇지 않을 경우 비동기로 수행된다.
	bool SendPacketAsync(CPacket* pPacket);  // send가 반드시 비동기로 수행된다.
	bool PostInnerRequest(CPacket* pPacket);   // IOCP 완료통지 큐에 작업을 삽입한다. 작업이 획득되면 OnInnerRequest 함수가 호출된다.

	/* dynamic alloc */
	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

private:
	/* Send, Recv */
	void RecvPost();   // WSARecv를 한다.	
	void SendPost();   // WSASend를 시도한다. 
	void SendPostAsync();  // SendPacketAsync 함수를 통해 비동기 send 요청을 받았을 때 worker 스레드 내에서 호출된다. WSASend를 호출한다.

	/* 패킷 암호화 */
	void EncryptPacket(CPacket* pPacket);
	bool DecipherPacket(CPacket* pPacket);

	/* thread */
	static unsigned WINAPI ThreadWorker(PVOID pParam);
	void UpdateWorkerThread();

	/* Crash */
	void Crash();

private:
	/* 라이브러리 callback 함수 */
	virtual bool OnRecv(CPacket& packet) = 0;
	virtual bool OnInnerRequest(CPacket& packet) { return false; }
	virtual void OnError(const wchar_t* szError, ...) = 0;            // 오류 발생
	virtual void OnOutputDebug(const wchar_t* szError, ...) = 0;      // debug 문자열을 함수로 전달. 이 함수를 직접 호출하지말고 NET_OUTPUT_DEBUG 매크로 사용을 권장함.
	virtual void OnOutputSystem(const wchar_t* szError, ...) = 0;     // system 문자열을 함수로 전달. 이 함수를 직접 호출하지말고 NET_OUTPUT_SYSTEM 매크로 사용을 권장함.
};



