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
#include "CLockfreeStack.h"
#include "CLockfreeQueue.h"
#include "CMemoryPoolTLS.h"

#include "defineNetwork.h"
#include "CContents.h"
#include "CSession.h"


namespace game_netserver
{


// OnOutputDebug 함수 호출 매크로
#define NET_OUTPUT_DEBUG(szOutputFormat, ...) do { \
	if (_bOutputDebug) \
		OnOutputDebug(szOutputFormat, ##__VA_ARGS__); \
} while(0) 

// OnOutputSystem 함수 호출 매크로
#define NET_OUTPUT_SYSTEM(szOutputFormat, ...) do { \
	if (_bOutputSystem) \
		OnOutputSystem(szOutputFormat, ##__VA_ARGS__); \
} while(0) 



class CSession;

/* Network */
class alignas(64) CNetServer
{
	friend class CContents;

private:
	/* server */
	HANDLE _hIOCP;
	SOCKET _listenSock;
	bool _bShutdown;
	volatile bool _bTrafficCongestion;  // 트래픽 혼잡여부
	bool _bOutputDebug;    // OnOutputDebug 함수 활성화여부(default:false)
	bool _bOutputSystem;   // OnOutputSystem 함수 활성화여부(default:true)

	/* config */
	wchar_t _szBindIP[16];
	ULONG _bindIP;
	unsigned short _portNumber;
	int _numConcurrentThread;
	int _numWorkerThread;
	int _numMaxSession;
	bool _bUseNagle;
	bool _bUsePacketEncryption;  // 패킷 암호화 사용 여부
	bool _bUseTrafficCongestionControl;  // 트래픽 혼잡제어 사용여부

	BYTE _packetCode;
	BYTE _packetKey;
	int  _maxPacketSize;

	/* thread */
	HANDLE _hThreadAccept;                           // accept 스레드
	unsigned int _idThreadAccept;
	std::vector<HANDLE> _vecHThreadWorker;           // worker 스레드
	std::vector<unsigned int> _vecIdThreadWorker;
	HANDLE _hThreadTrafficCongestionControl;         // 트래픽 혼잡제어 스레드
	unsigned int _idThreadTrafficCongestionControl;
	HANDLE _hThreadDeferredCloseSocket;              // 지연된 소켓닫기 스레드
	unsigned int _idThreadDeferredCloseSocket;

	/* deferred close socket thread */
	CLockfreeQueue<SOCKET> _msgQDeferredCloseSocket;  // 지연된 소켓닫기 스레드 메시지큐
	HANDLE _hEventDeferredCloseSocket;
	bool _bEventSetDeferredCloseSocket;

	/* session */
	CSession* _arrSession;
	CLockfreeStack<unsigned short> _stackSessionIdx;

	/* 컨텐츠 */
	CContents* _initialContents;                         // 세션 최초 접속시 할당될 컨텐츠
	std::list<CContents*> _listContents;                 // 컨텐츠 객체 보관용 리스트
	SRWLOCK _srwlListContents;                           // list lock
	CMemoryPoolTLS<MsgContents> _poolMsgContents;        // 컨텐츠 스레드의 메시지큐에서 사용할 메시지의 pool

	/* 모니터링 */
	LARGE_INTEGER _liPerformanceFrequency;
	// worker 스레드 내부 TLS 모니터링(worker 스레드 수 길이의 배열. worker 스레드 내부의 _stlsTlsIndex 변수를 배열 index로 사용함)
	struct _StWorkerTlsMonitor
	{
		volatile __int64 GQCSWaitTime = 0;           // GQCS가 리턴되기를 기다린 시간(performance frequency 단위)
		volatile __int64 sendCompletionCount = 0;    // send 완료통지 처리횟수
		volatile __int64 recvCompletionCount = 0;    // recv 완료통지 처리횟수
		volatile __int64 recvCount = 0;              // recv 완료통지에서 처리한 패킷 수
		volatile __int64 disconnByKnownRecvIoError = 0;    // recv 완료통지에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
		volatile __int64 disconnByUnknownRecvIoError = 0;  // recv 완료통지에서 오류 발생(원인불명)
		volatile __int64 disconnByKnownSendIoError = 0;    // send 완료통지에서 오류 발생
		volatile __int64 disconnByUnknownSendIoError = 0;  // send 완료통지에서 오류 발생(원인불명)
		volatile __int64 disconnBy121RecvIoError = 0;      // recv 완료통지에서 오류 발생(ERROR_SEM_TIMEOUT 121 오류)
		volatile __int64 disconnByNormalProcess = 0;        // 클라이언트가 정상적으로 연결을 끊음
		volatile __int64 disconnByPacketCode = 0;    // 패킷코드가 잘못됨
		volatile __int64 disconnByPacketLength = 0;  // 패킷길이가 잘못됨
		volatile __int64 disconnByPacketDecode = 0;  // 패킷 복호화에 실패함
	};
	_StWorkerTlsMonitor* _arrWorkerTlsMonitor;

	// accept 스레드 모니터링
	volatile __int64 _acceptCount = 0;            // accept 횟수
	volatile __int64 _connectCount = 0;           // connect 횟수 (accept 후 connect 승인된 횟수)
	volatile __int64 _disconnBySessionLimit = 0;  // 세션수 제한으로 인해 끊김
	volatile __int64 _disconnByIOCPAssociation = 0;  // IOCP와 소켓 연계에 실패하여 끊김
	volatile __int64 _disconnByOnConnReq = 0;     // OnConnectionRequest 함수로 인해 끊김

	// 라이브러리 함수 내부 모니터링
	alignas(64) volatile __int64 _sendCount = 0;        // SendPost 함수에서 WSASend 함수로 전송한 패킷의 수
	alignas(64) volatile __int64 _sendAsyncCount = 0;   // SendPostAsync 함수에서 WSASend 함수로 전송한 패킷의 수
	volatile __int64 _disconnectCount = 0;        // disconnect 횟수 (세션 release 횟수)
	volatile __int64 _deferredDisconnectCount = 0;  // 지연된 disconnect 횟수
	volatile __int64 _WSARecvKnownError = 0;      // WSARecv 함수에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
	volatile __int64 _WSARecvUnknownError = 0;    // WSARecv 함수에서 오류 발생(원인불명)
	volatile __int64 _WSASendKnownError = 0;      // WSASend 함수에서 오류 발생
	volatile __int64 _WSASendUnknownError = 0;    // WSASend 함수에서 오류 발생(원인불명)
	volatile __int64 _otherErrors = 0;            // 기타 오류

	// 트래픽 혼잡제어 스레드 모니터링
	__int64 _trafficCongestionControlCount = 0;


public:
	CNetServer();
	virtual ~CNetServer();

public:
	/* Set */
	void SetOutputDebug(bool enable) { _bOutputDebug = enable; }    // OnOutputDebug 함수 활성화, 비활성화 (기본값:비활성)
	void SetOutputSystem(bool enable) { _bOutputSystem = enable; }  // OnOutputSystem 함수 활성화, 비활성화 (기본값:활성)

	/* Get 서버 상태 */
	int GetNumWorkerThread() { return _numWorkerThread; }
	int GetNumSession() { return _numMaxSession - _stackSessionIdx.Size(); }
	int GetPacketPoolSize() { return CPacket::GetPoolSize(); };
	int GetPacketAllocCount() { return CPacket::GetAllocCount(); }
	int GetPacketActualAllocCount() { return CPacket::GetActualAllocCount(); }
	int GetPacketFreeCount() { return CPacket::GetFreeCount(); }
	int GetMsgPoolSize() { return _poolMsgContents.GetPoolSize(); }
	int GetMsgAllocCount() { return _poolMsgContents.GetAllocCount(); }
	int GetMsgActualAllocCount() { return _poolMsgContents.GetActualAllocCount(); }
	int GetMsgFreeCount() { return _poolMsgContents.GetFreeCount(); }
	int GetSizeMsgQDeferredCloseSocket() { return _msgQDeferredCloseSocket.Size(); }

	/* Get 모니터링 */
	__int64 GetAcceptCount() { return _acceptCount; }     // accept 횟수
	__int64 GetConnectCount() { return _connectCount; }   // connect 횟수 (accept 후 connect 승인된 횟수)
	__int64 GetDisconnBySessionLimit() { return _disconnBySessionLimit; }     // 세션수 제한으로 인해 끊김
	__int64 GetDisconnByIOCPAssociation() { return _disconnByIOCPAssociation; }  // IOCP와 소켓 연계에 실패하여 끊김
	__int64 GetDisconnByOnConnectionRequest() { return _disconnByOnConnReq; } // OnConnectionRequest 함수로 인해 끊김

	void GetArrGQCSWaitTime(int* arrTime);     // worker 스레드별 GQCS 기다린시간
	__int64 GetSendCompletionCount();          // send 완료통지 처리횟수
	__int64 GetRecvCompletionCount();          // recv 완료통지 처리횟수
	__int64 GetRecvCount();                    // recv 완료통지에서 처리한 패킷 수
	__int64 GetDisconnByKnownIoError();        // WSARecv, WSASend, recv 완료통지, send 완료통지에서 오류 발생(클라이언트 또는 서버에서 강제로 끊었을때)
	__int64 GetDisconnByUnknownIoError();      // WSARecv, WSASend, recv 완료통지, send 완료통지에서 오류 발생(원인불명)
	__int64 GetDisconnBy121RecvIoError();      // recv 완료통지에서 121(ERROR_SEM_TIMEOUT) 오류 발생
	__int64 GetDisconnByNormalProcess();       // 클라이언트가 정상적으로 연결을 끊음
	__int64 GetDisconnByPacketCode();          // 패킷코드가 잘못됨
	__int64 GetDisconnByPacketLength();        // 패킷길이가 잘못됨
	__int64 GetDisconnByPacketDecode();        // 패킷 복호화에 실패함

	__int64 GetSendCount() { return _sendCount + _sendAsyncCount; } // WSASend 함수로 전송한 패킷의 수
	__int64 GetDisconnectCount() { return _disconnectCount; }       // disconnect 횟수 (세션 release 횟수)
	__int64 GetDeferredDisconnectCount() { return _deferredDisconnectCount; }   // 지연된 disconnect 횟수
	__int64 GetOtherErrorCount() { return _otherErrors; }                     // 기타 오류 횟수
	__int64 GetTrafficCongestionControlCount() { return _trafficCongestionControlCount; }  // 트래픽 혼잡제어 횟수

	__int64 GetContentsInternalMsgCount();     // 컨텐츠 내부 메시지 처리횟수
	__int64 GetContentsSessionMsgCount();      // 컨텐츠 세션 메시지 처리횟수
	__int64 GetContentsDisconnByHeartBeat();   // 컨텐츠 내부에서 하트비트 타임아웃으로 끊긴 세션수


	/* server */
	bool StartUp(const wchar_t* bindIP, unsigned short port, int numConcurrentThread, int numWorkerThread, bool bUseNagle, int numMaxSession, BYTE packetCode, BYTE packetKey, int maxPacketSize, bool bUsePacketEncryption, bool bUseTrafficCongestionControl);
	void StopAccept();  // accept 종료
	void Shutdown();    // 서버 종료(accept 종료 포함)


	/* packet */
	CPacket* AllocPacket();
	bool SendPacket(__int64 sessionId, CPacket* pPacket);  // send가 동기로 수행될 수 있을 때는 동기로 수행되고, 그렇지 않을 경우 비동기로 수행된다.
	bool SendPacket(__int64 sessionId, CPacket** arrPtrPacket, size_t numPacket);
	bool SendPacketAsync(__int64 sessionId, CPacket* pPacket);  // send가 반드시 비동기로 수행된다.
	bool SendPacketAsync(__int64 sessionId, CPacket** arrPtrPacket, size_t numPacket);
	bool SendPacketAndDisconnect(__int64 sessionId, CPacket* pPacket);       // send가 완료된 후 연결을 끊는다.
	bool SendPacketAndDisconnect(__int64 sessionId, CPacket** arrPtrPacket, size_t numPacket);
	bool SendPacketAsyncAndDisconnect(__int64 sessionId, CPacket* pPacket);  // 비동기 send를 요청하고, send가 완료되면 연결을 끊는다.
	bool SendPacketAsyncAndDisconnect(__int64 sessionId, CPacket** arrPtrPacket, size_t numPacket);
	bool PostInnerRequest(CPacket* pPacket);   // IOCP 완료통지 큐에 작업을 삽입한다. 작업이 획득되면 OnInnerRequest 함수가 호출된다.

	/* session */
	bool Disconnect(__int64 sessionId);
	
	/* 컨텐츠 */
	void SetInitialContents(CContents* pContents);   // 세션이 최초 접속시 할당될 컨텐츠를 지정한다.
	bool AttachContents(CContents* pContents);       // 컨텐츠를 라이브러리에 추가하고 스레드를 시작한다.
	void DetachContents(CContents* pContents);       // 컨텐츠를 라이브러리에서 제거하고 스레드를 종료한다.
	MsgContents* AllocMsg() { return _poolMsgContents.Alloc(); }

	/* dynamic alloc */
	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

private:
	/* Send, Recv */
	void RecvPost(CSession* pSession);   // WSARecv를 한다.	
	void SendPost(CSession* pSession);   // WSASend를 시도한다. 
	void SendPostAsync(CSession* pSession);  // SendPacketAsync 함수를 통해 비동기 send 요청을 받았을 때 worker 스레드 내에서 호출된다. WSASend를 호출한다.


	/* 세션 */
	CSession* FindSession(__int64 sessionId);  // 주의: FindSession 함수로 세션을 얻었을 경우 세션 사용 후에 반드시 DecreaseIoCount 함수를 호출해야함
	void IncreaseIoCount(CSession* pSession);
	void DecreaseIoCount(CSession* pSession);
	CSession* AllocSession(SOCKET clntSock, SOCKADDR_IN& clntAddr);
	void CloseSocket(CSession* pSession);  // 소켓을 닫는다(세션을 free하지는 않음).
	void ReleaseSession(CSession* pSession);
	bool Disconnect(CSession* pSession);

	/* 패킷 암호화 */
	void EncryptPacket(CPacket* pPacket);
	bool DecipherPacket(CPacket* pPacket);

	/* thread */
	static unsigned WINAPI ThreadAccept(PVOID pParam);
	static unsigned WINAPI ThreadWorker(PVOID pParam);
	static unsigned WINAPI ThreadTrafficCongestionControl(PVOID pParam);
	static unsigned WINAPI ThreadDeferredCloseSocket(PVOID pParam);
	void UpdateAcceptThread();
	void UpdateWorkerThread();
	void UpdateTrafficCongestionControlThread();
	void UpdateThreadDeferredCloseSocketThread();

	/* Crash */
	void Crash();

private:
	/* 라이브러리 callback 함수 */
	virtual bool OnInnerRequest(CPacket& packet) { return false; }
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port) = 0;
	virtual void OnError(const wchar_t* szError, ...) = 0;            // 오류 발생
	virtual void OnOutputDebug(const wchar_t* szError, ...) = 0;      // debug 문자열을 함수로 전달. 이 함수를 직접 호출하지말고 NET_OUTPUT_DEBUG 매크로 사용을 권장함.
	virtual void OnOutputSystem(const wchar_t* szError, ...) = 0;     // system 문자열을 함수로 전달. 이 함수를 직접 호출하지말고 NET_OUTPUT_SYSTEM 매크로 사용을 권장함.


};



}