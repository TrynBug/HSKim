#pragma once


#define SESSION_SIZE_ARR_PACKET 50

namespace netserver
{

/* session */
class alignas(64) CSession
{
	friend class CNetServer;

private:
	union {  // 64byte aligned
		struct {
			volatile bool releaseFlag; // _releaseFlag 매크로로 접근 가능
			volatile long ioCount;     // _ioCount 매크로로 접근 가능
		} __s;
		volatile long long _ioCountAndReleaseFlag;
	};
	#define _ioCount     __s.ioCount
	#define _releaseFlag __s.releaseFlag

	alignas(64) CLockfreeQueue<CPacket*>* _sendQ;

	unsigned __int64 _sessionId;
	unsigned short _index;
	unsigned long _IP;
	wchar_t _szIP[16];
	unsigned short _port;

	SOCKET _sock;
	CRingbuffer _recvQ;
	OVERLAPPED_EX _sendOverlapped;
	OVERLAPPED_EX _recvOverlapped;
	CPacket** _arrPtrPacket;  // WSASend 함수에 사용한 패킷 주소를 저장해두는 배열. SESSION_SIZE_ARR_PACKET 크기이다.
	int _numSendPacket;
	SOCKET _socketToBeFree;

	volatile bool _sendIoFlag;
	volatile bool _isClosed;
	volatile bool _bError;       // 오류 발생 여부
	volatile bool _bCloseWait;   // 종료 대기. 보내고끊기 함수 호출시 true가 됨. true일 경우 _sendQ 내의 모든 데이터를 send한 다음 종료된다.
	CPacket* _lastPacket;        // _bCloseWait == true일 경우, 이 패킷이 보내진것이 확인되면 소켓을 닫는다.

	static __int64 _sNextSessionId;  // 다음 세션 ID

public:
	CSession(unsigned short index);
	~CSession();

	void Init(SOCKET clntSock, SOCKADDR_IN& clntAddr);  // 새로운 세션 사용 시 호출

	/* dynamic alloc */
	void* operator new(size_t size);
	void operator delete(void* p);
	void* operator new(size_t size, void* p); // for placement new
	void operator delete(void* p, void* p2);  // for placement delete

private:
	/* (static) 다음 세션 ID 얻기 */
	static __int64 GetNextSessionId();

};


}