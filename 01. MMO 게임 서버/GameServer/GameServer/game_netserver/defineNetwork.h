#pragma once

#define SIZE_RECV_BUFFER 10000

#define IO_SEND 1
#define IO_RECV 2


namespace game_netserver
{

struct OVERLAPPED_EX
{
	OVERLAPPED overlapped;
	int ioType;
};

#pragma pack(push, 1)
struct PacketHeader
{
	BYTE code;
	unsigned short len;
	BYTE randKey;
	BYTE checkSum;
};
#pragma pack(pop)

enum class EMsgContentsType
{
	INITIAL_JOIN_SESSION,
	JOIN_SESSION,
	SHUTDOWN,
};

class CSession;
struct MsgContents
{
	EMsgContentsType type;
	CSession* pSession;
	PVOID data;
};

}

