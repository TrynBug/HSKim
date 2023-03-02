#pragma once

#include "CMemoryPool.h"

//#define QUEUE_ENABLE_LOGGING

namespace netserver
{


using TYPE_ID = unsigned __int64;

#define QUEUE_GET_ADDRESS(Id) (Node*)((Id) & _addressMask)
#define QUEUE_GET_PROC_COUNT(Id) (int)((Id) >> _addressBitCount)
#define QUEUE_MAKE_ID(procCount, address) (((TYPE_ID)(procCount) << _addressBitCount) | (TYPE_ID)(address))

template <typename _Ty>
class alignas(64) CLockfreeQueue
{
public:
	struct Node
	{
		Node* pNext;
		_Ty obj;
	};

private:
	TYPE_ID _headId;  // 64byte aligned
	alignas(64) TYPE_ID _tailId;  // 64byte aligned
	alignas(64) long _size;  // 64byte aligned

	alignas(64) CMemoryPool<Node>* _pPoolNode;
	TYPE_ID _addressMask;   // TYPE_ID 값에서 주소값을 얻기위한 mask
	__int64 _addressBitCount;   // 유저영역 주소의 bit 수. TYPE_ID 값에서 pop count를 얻기위해 사용됨

public:
	CLockfreeQueue();
	CLockfreeQueue(int size);
	~CLockfreeQueue();

public:
	int Size() { return _size; }
	int GetPoolSize() { return _pPoolNode->GetPoolSize(); }

	void Enqueue(_Ty& obj);
	bool Dequeue(_Ty& obj);

	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

};


template <typename _Ty>
CLockfreeQueue<_Ty>::CLockfreeQueue()
	:_size(0), _addressBitCount(0)
{
	// 노드 풀 생성
	_pPoolNode = new CMemoryPool<Node>(0, true, false);
	
	// 주소 mask 계산
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	_addressMask = (TYPE_ID)sysInfo.lpMaximumApplicationAddress;
	for (int i = 63; i >= 0; i--)
	{
		if (_addressMask & (1i64 << i))
		{
			_addressMask = 0xFFFFFFFF'FFFFFFFF >> (63 - i);  // 0x00007FFF'FFFFFFFF
			_addressBitCount = i + 1;                        // 47
			break;
		}
	}

	// 더미노드 생성
	Node* pDummy = _pPoolNode->Alloc();
	pDummy->pNext = nullptr;
	TYPE_ID dummyId = QUEUE_MAKE_ID(0, pDummy);
	_headId = dummyId;
	_tailId = dummyId;
}


template <typename _Ty>
CLockfreeQueue<_Ty>::CLockfreeQueue(int size)
	:_size(0), _addressBitCount(0)
{
	// 노드 풀 생성
	_pPoolNode = new CMemoryPool<Node>(size, true, false);

	// 주소 mask 계산
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	_addressMask = (TYPE_ID)sysInfo.lpMaximumApplicationAddress;
	for (int i = 63; i >= 0; i--)
	{
		if (_addressMask & (1i64 << i))
		{
			_addressMask = 0xFFFFFFFF'FFFFFFFF >> (63 - i);  // 0x00007FFF'FFFFFFFF
			_addressBitCount = i + 1;                        // 47
			break;
		}
	}

	// 더미노드 생성
	Node* pDummy = _pPoolNode->Alloc();
	pDummy->pNext = nullptr;
	TYPE_ID dummyId = QUEUE_MAKE_ID(0, pDummy);
	_headId = dummyId;
	_tailId = dummyId;
}


template <typename _Ty>
CLockfreeQueue<_Ty>::~CLockfreeQueue()
{
	// 모든 노드를 free 한다.
	Node* pNode = QUEUE_GET_ADDRESS(_headId);
	Node* pNext;
	while (pNode != nullptr)
	{
		_size--;
		pNext = pNode->pNext;
		_pPoolNode->Free(pNode);
		pNode = pNext;
	}

	// 노드 풀 삭제
	delete _pPoolNode;
}



template <typename _Ty>
void CLockfreeQueue<_Ty>::Enqueue(_Ty& obj)
{
	Node* pNewNode = _pPoolNode->Alloc();
	pNewNode->pNext = nullptr;
	pNewNode->obj = obj;

	TYPE_ID tailId;
	TYPE_ID tailNextId;
	TYPE_ID newTailId;
	Node* pTail;
	Node* pTailNext;
	while (true)
	{
		tailId = _tailId;
		pTail = QUEUE_GET_ADDRESS(tailId);
		pTailNext = pTail->pNext;
		if (pTailNext == nullptr)
		{
			if (InterlockedCompareExchange64((__int64*)&pTail->pNext, (__int64)pNewNode, 0) == 0)
			{
				// tail->next를 new node로 교체했다면 성공.
				break;
			}
			else
			{
				// pTailNext == nullptr 인데 CAS1을 실패했다는 것은 그사이에 pTail->pNext 가 교체되었다는 의미이다.
				// 이 경우 pTail->pNext를 교체한 스레드가 CAS2로 _tailId를 교체할 것이기 때문에 내가 _tailId 교체를 시도할 필요는 없음.
				continue;
			}
		}
		else
		{
			// tail->next가 null이 아니라면 tail을 뒤로 밀기를 시도한다.
			tailNextId = QUEUE_MAKE_ID(QUEUE_GET_PROC_COUNT(tailId) + 1, pTailNext);
			if (_tailId != tailId)  // CAS전 미리 확인
				continue;
			InterlockedCompareExchange64((__int64*)&_tailId, (__int64)tailNextId, (__int64)tailId);
		}
	}

	// tail->next를 new node로 교체한 뒤, tail을 뒤로 민다.
	newTailId = QUEUE_MAKE_ID(QUEUE_GET_PROC_COUNT(tailId) + 1, pNewNode);
	InterlockedCompareExchange64((__int64*)&_tailId, (__int64)newTailId, (__int64)tailId);

	InterlockedIncrement(&_size);
}





template <typename _Ty>
bool CLockfreeQueue<_Ty>::Dequeue(_Ty& obj)
{
	TYPE_ID headId;
	TYPE_ID headNextId;
	TYPE_ID tailId;
	TYPE_ID tailNextId;
	Node* pHead;
	Node* pHeadNext;
	Node* pTail;
	Node* pTailNext;

	if (_size <= 0)
		return false;

	long size = InterlockedDecrement(&_size);
	if (size < 0)
	{
		InterlockedIncrement(&_size);
		return false;
	}

	while (true) 
	{
		headId = _headId;
		pHead = QUEUE_GET_ADDRESS(headId);
		pHeadNext = pHead->pNext;
		if (pHeadNext == nullptr)
		{
			// size가 1 이상이었는데 head->next가 null인 경우는 다른곳에서 끊어진 노드가 아직 연결되지 않은 것임. 연결될때까지 기다림.
			continue;
		}
		
		tailId = _tailId;
		pTail = QUEUE_GET_ADDRESS(tailId);
		pTailNext = pTail->pNext;
		if (pHead == pTail && pTailNext != nullptr)
		{
			// head와 tail이 같은데 tail->next가 null이 아니면 tail이 뒤로 밀리지 않은 것임. tail을 뒤로 미는것을 시도함.
			// 여기서 CAS가 실패했다면 다른곳에서 tail을 뒤로 민 것이기 때문에 그냥 넘어감.
			tailNextId = QUEUE_MAKE_ID(QUEUE_GET_PROC_COUNT(tailId) + 1, pTailNext);
			InterlockedCompareExchange64((__int64*)&_tailId, (__int64)tailNextId, (__int64)tailId);
		}

		obj = pHeadNext->obj;
		headNextId = QUEUE_MAKE_ID(QUEUE_GET_PROC_COUNT(headId) + 1, pHeadNext);
		if (_headId != headId)  // CAS전 미리 확인
			continue;
		if (InterlockedCompareExchange64((__int64*)&_headId, (__int64)headNextId, (__int64)headId) == (__int64)headId)
		{
			// head를 head->next로 교체하는것이 성공하였다면 완료.
			break;
		}
	} 

	_pPoolNode->Free(pHead);
	return true;
}



// 64byte aligned 객체 생성을 위한 new, delete overriding
template <typename _Ty>
void* CLockfreeQueue<_Ty>::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}
template <typename _Ty>
void CLockfreeQueue<_Ty>::operator delete(void* p)
{
	_aligned_free(p);
}




}