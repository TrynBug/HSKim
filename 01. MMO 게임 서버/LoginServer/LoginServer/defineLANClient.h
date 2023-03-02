#pragma once


#define LAN_SIZE_RECV_BUFFER 5000

#define LAN_IO_SEND 1
#define LAN_IO_RECV 2


struct LAN_OVERLAPPED_EX
{
	OVERLAPPED overlapped;
	int ioType;
};

#pragma pack(push, 1)
struct LANPacketHeader
{
	BYTE code;
	unsigned short len;
	BYTE randKey;
	BYTE checkSum;
};
#pragma pack(pop)


#define LAN_CLIENT_SIZE_ARR_PACKTE 10




