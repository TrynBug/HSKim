#include <iostream>
#include <Windows.h>

#include "CMemoryPoolTLS.h"
#include "CPacket.h"

using namespace game_netserver;

int CPacket::_sDefaultPacketSize = 10000;
CMemoryPoolTLS<CPacket>* g_poolPacket = new CMemoryPoolTLS<CPacket>(0, false, 100);


CPacket::CPacket()
	:_size(_sDefaultPacketSize), _sizeHeader(0), _offsetFront(0), _offsetRear(0), _useCount(1), _isHeaderSet(false), _isEncoded(false)
{
	_buf = new char[_size];
}


CPacket::~CPacket()
{
	delete[] _buf;
}

/* 초기화 */
void CPacket::Init(int sizeHeader)
{
	_sizeHeader = sizeHeader;
	_useCount = 1;
	Clear();
}


/* buffer control */
void CPacket::Clear()
{
	_offsetFront = _sizeHeader;
	_offsetRear = _sizeHeader;	
	_isHeaderSet = false;
	_isEncoded = false;
}

int CPacket::MoveWritePos(int iSize)
{
	int offsetRear_old = _offsetRear;
	_offsetRear += iSize;
	if (_offsetRear >= _size)
		_offsetRear = _size - 1;
	if (_offsetRear < _sizeHeader)
		_offsetRear = _sizeHeader;

	return _offsetRear - offsetRear_old;
}

int CPacket::MoveReadPos(int iSize)
{
	int offsetFront_old = _offsetFront;
	_offsetFront += iSize;
	if (_offsetFront >= _size)
		_offsetFront = _size - 1;
	if (_offsetFront < 0)
		_offsetFront = 0;

	return _offsetFront - offsetFront_old;
}



/* use count */
long CPacket::AddUseCount()
{
	return InterlockedIncrement(&_useCount);
}

long CPacket::AddUseCount(int value)
{
	return InterlockedAdd(&_useCount, value);
}

long CPacket::SubUseCount()
{
	long useCount = InterlockedDecrement(&_useCount);
	if (useCount == 0)
	{
		g_poolPacket->Free(this);
	}
	return useCount;
}




/* 메모리풀 (static) */
CPacket* CPacket::AllocPacket()
{
	return g_poolPacket->Alloc();
}

void CPacket::FreePacket(CPacket* pPacket)
{
	g_poolPacket->Free(pPacket);
}

int CPacket::GetPoolSize()
{
	return g_poolPacket->GetPoolSize();
}

void CPacket::RegeneratePacketPool(int packetSize, int initialPoolSize, int chunkSize)
{
	delete g_poolPacket;
	_sDefaultPacketSize = packetSize;
	g_poolPacket = new CMemoryPoolTLS<CPacket>(initialPoolSize, false, chunkSize);
}

 /* 패킷 사용현황 (static) */
int CPacket::GetAllocCount()
{
	return g_poolPacket->GetAllocCount();
}

int CPacket::GetActualAllocCount()
{
	return g_poolPacket->GetActualAllocCount();
}

int CPacket::GetFreeCount()
{
	return g_poolPacket->GetFreeCount();

}