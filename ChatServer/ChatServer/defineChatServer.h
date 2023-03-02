#pragma once

/* 서버 */
//#define CHAT_SERVER_PORT    12001
//#define dfPACKET_CODE		0x77
//#define dfPACKET_KEY		0x32

/* 섹터 */
#define dfSECTOR_MAX_Y      50
#define dfSECTOR_MAX_X      50

/* 메시지 */
#define MSG_FROM_CLIENT     1
#define MSG_FROM_SERVER     2

/* 패킷 */
//#define MAX_PACKET_SIZE     522 


enum class EServerMsgType
{
	MSG_JOIN_PLAYER,
	MSG_LEAVE_PLAYER,
	MSG_CHECK_HEART_BEAT_TIMEOUT,
	MSG_CHECK_LOGIN_TIMEOUT,
	MSG_SHUTDOWN,
	MSG_LOGIN_REDIS_GET,
};

namespace netserver
{
	class CPacket;
}
struct MsgChatServer
{
	unsigned short msgFrom;    // 클라이언트에게 받은 메시지인지, 서버 내부 컨트롤 메시지인지 구분
	__int64        sessionId;
	netserver::CPacket* pPacket;
	EServerMsgType eServerMsgType;  // 서버 내부 컨트롤 메시지일경우 메시지 타입
};


// 비동기 redis get 호출 때 completion routine 함수에 전달할 데이터
class CChatServer;
struct StAPCData
{
	CChatServer* pChatServer;
	netserver::CPacket* pPacket;
	__int64 sessionId;
	__int64 accountNo;
	bool isNull;
	char sessionKey[64];
};

/* 플레이어 */
#define SECTOR_NOT_SET      0xffff   // 섹터가 아직 설정되지 않았을 경우의 섹터 좌표값




