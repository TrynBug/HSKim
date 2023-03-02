#pragma once


#define SIZE_RECV_BUFFER 5000

#define IO_SEND 1
#define IO_RECV 2

namespace netserver
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




}
