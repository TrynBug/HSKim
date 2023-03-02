#include "CRingbuffer.h"
#include <iostream>

using namespace netserver;

#define min(a, b)  (((a) < (b)) ? (a) : (b)) 
#define max(a, b)  (((a) > (b)) ? (a) : (b)) 

#define RINGBUFFER_DEFAULT_SIZE 10000
#define RINGBUFFER_MAX_SIZE     30000 

CRingbuffer::CRingbuffer(void)
{
	_size = RINGBUFFER_DEFAULT_SIZE;
	_buf = new char[_size + 1];
	_front = _buf;
	_rear = _buf;
}

CRingbuffer::CRingbuffer(int size)
{
	_size = size;
	_buf = new char[_size + 1];
	_front = _buf;
	_rear = _buf;
}

CRingbuffer::~CRingbuffer()
{
	delete[] _buf;
}




/* ringbuffer에 데이터를 쓴다.
현재 rear 위치부터 데이터를 쓰고, 쓰고 난 뒤 rear의 위치는 데이터의 마지막 위치 +1 이다.
@parameters
src : 데이터 포인터, len : 데이터 byte 길이
@return
성공 시 입력한 데이터 byte 크기를 리턴함.
ringbuffer에 충분한 공간이 없으면 실패하며, 0을 리턴함. */
int CRingbuffer::Enqueue(const char* src, int len)
{
	if (GetFreeSize() < len || len < 1)
		return 0;

	int directFreesize = GetDirectFreeSize();
	if (directFreesize < len)
	{
		memcpy(_rear, src, directFreesize);
		memcpy(_buf, src + directFreesize, len - directFreesize);
		_rear = _buf + len - directFreesize;
	}
	else
	{
		memcpy(_rear, src, len);
		_rear += len;
		if (_rear == _buf + _size + 1)
			_rear = _buf;
	}
	return len;
}




/* 현재 ringbuffer의 남은 공간 byte 크기를 얻는다.
@return
ringbuffer의 남은 공간 byte 크기 */
int CRingbuffer::GetFreeSize(void)
{
	volatile char* front = _front; // enqueue 스레드 1개, dequeue 스레드 1개인 멀티스레드 환경에서 FreeSize를 안전하게 계산하기 위해 front를 volatile로 선언함
	if (front > _rear)
		return (int)(front - _rear - 1);
	else
		return (int)(_size + front - _rear);
}


/* 현재 ringbuffer에서 사용중인 공간 byte 크기를 얻는다.
@return
ringbuffer의 사용중인 공간 byte 크기 */
int CRingbuffer::GetUseSize(void)
{
	volatile char* rear = _rear;
	if (_front > rear)
		return (int)(_size + rear - _front + 1);
	else
		return (int)(rear - _front);
}

/* 현재 ringbuffer에서 rear와 버퍼의 끝 사이의 남은 공간 byte 크기를 얻는다.
@return
ringbuffer에서 rear와 버퍼의 끝 사이의 남은 공간 byte 크기 */
int CRingbuffer::GetDirectFreeSize(void)
{
	volatile char* front = _front;
	if (front > _rear)
		return (int)(front - _rear - 1);
	else
	{
		if (front != _buf)
			return (int)(_buf + _size - _rear + 1);
		else
			return (int)(_buf + _size - _rear);
	}

}


/* 현재 ringbuffer에서 front와 버퍼의 끝 사이의 사용중인 공간의 byte 크기를 얻는다.
@return
ringbuffer에서 front와 버퍼의 끝 사이의 사용중인 공간 byte 크기 */
int CRingbuffer::GetDirectUseSize(void)
{
	volatile char* rear = _rear;
	if (_front > rear)
		return (int)(_buf + _size - _front + 1);
	else
		return (int)(rear - _front);
}





/* ringbuffer의 데이터를 dest로 읽는다.
현재 front 위치부터 데이터를 읽고, 읽은만큼 front를 전진시킨다.
@parameters
dest : 데이터 읽기 버퍼, len : 버퍼 byte 길이
@return
성공 시 읽은 데이터 byte 크기를 리턴함.
ringbuffer 내에 len 크기만큼 데이터가 있을 경우 len 만큼 읽음.
ringbuffer 내에 len 크기만큼 데이터가 없을 경우 남아있는 모든 데이터를 읽음. */
int CRingbuffer::Dequeue(char* dest, int len)
{
	if (len < 1)
		return 0;

	int useSize = GetUseSize();
	if (useSize == 0)
		return 0;

	int readBytes = min(len, useSize);
	int directUseSize = GetDirectUseSize();
	if (directUseSize < readBytes)
	{
		memcpy(dest, _front, directUseSize);
		memcpy(dest + directUseSize, _buf, readBytes - directUseSize);
		_front = _buf + readBytes - directUseSize;
	}
	else
	{
		memcpy(dest, _front, readBytes);
		_front += readBytes;
		if (_front == _buf + _size + 1)
			_front = _buf;
	}
	return readBytes;
}




/* ringbuffer의 데이터를 dest로 읽는다.
현재 front 위치부터 데이터를 읽고, front는 전진시키지 않는다.
@parameters
dest : 데이터 읽기 버퍼, len : 버퍼 byte 길이
@return
성공 시 읽은 데이터 byte 크기를 리턴함.
ringbuffer 내에 len 크기만큼 데이터가 있을 경우 len 만큼 읽음.
ringbuffer 내에 len 크기만큼 데이터가 없을 경우 남아있는 모든 데이터를 읽음. */
int CRingbuffer::Peek(char* dest, int len)
{
	if (len < 1)
		return 0;

	int useSize = GetUseSize();
	if (useSize == 0)
		return 0;

	int readBytes = min(len, useSize);
	int directUseSize = GetDirectUseSize();
	if (directUseSize < readBytes)
	{
		memcpy(dest, _front, directUseSize);
		memcpy(dest + directUseSize, _buf, readBytes - directUseSize);
	}
	else
	{
		memcpy(dest, _front, readBytes);
	}
	return readBytes;
}



/* ringbuffer의 데이터를 dest로 읽는다. 이 때 현재 사용중인 크기를 계산하지 않고 미리 전달받는다.
현재 front 위치부터 데이터를 읽고, front는 전진시키지 않는다.
@parameters
dest : 데이터 읽기 버퍼, len : 버퍼 byte 길이
@return
성공 시 읽은 데이터 byte 크기를 리턴함.
ringbuffer 내에 len 크기만큼 데이터가 있을 경우 len 만큼 읽음.
ringbuffer 내에 len 크기만큼 데이터가 없을 경우 남아있는 모든 데이터를 읽음. */
int CRingbuffer::Peek(char* dest, int len, int useSize, int directUseSize)
{
	if (len < 1)
		return 0;

	if (useSize == 0)
		return 0;

	int readBytes = min(len, useSize);
	if (directUseSize < readBytes)
	{
		memcpy(dest, _front, directUseSize);
		memcpy(dest + directUseSize, _buf, readBytes - directUseSize);
	}
	else
	{
		memcpy(dest, _front, readBytes);
	}
	return readBytes;
}



/* front의 위치를 len 만큼 뒤로 옮긴다.
@parameters
len : front의 위치를 뒤로 옮길 byte 크기 */
void CRingbuffer::MoveFront(int dist)
{
	_front = _buf + (_front - _buf + dist) % (_size + 1);
}

/* rear의 위치를 len 만큼 뒤로 옮긴다.
@parameters
len : rear의 위치를 뒤로 옮길 byte 크기 */
void CRingbuffer::MoveRear(int dist)
{
	_rear = _buf + (_rear - _buf + dist) % (_size + 1);
}


/* 버퍼를 비운다. */
void CRingbuffer::Clear(void)
{
	_front = _buf;
	_rear = _buf;
}

/* 버퍼 크기를 늘리거나 줄인다.
@parameters
size : 늘리거나 줄일 버퍼의 크기
@return
늘린 뒤의 버퍼 크기. 늘릴 수 있는 최대 크기는 RINGBUFFER_MAX_SIZE 이다.
*/
int CRingbuffer::Resize(int size)
{
	int newSize = min(max(1, size), RINGBUFFER_MAX_SIZE);
	char* newBuf = new char[newSize + 1];
	int readBytes = Dequeue(newBuf, newSize);

	delete _buf;
	_size = newSize;
	_buf = newBuf;
	_front = newBuf;
	_rear = newBuf + readBytes;

	return newSize;
}