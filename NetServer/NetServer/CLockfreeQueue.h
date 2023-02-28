#pragma once

#include "CMemoryPool.h"

//#define QUEUE_ENABLE_LOGGING


using TYPE_ID = unsigned __int64;

#define QUEUE_GET_ADDRESS(Id) (Node*)((Id) & _addressMask)
#define QUEUE_GET_PROC_COUNT(Id) (int)((Id) >> _addressBitCount)
#define QUEUE_MAKE_ID(procCount, address) (((TYPE_ID)(procCount) << _addressBitCount) | (TYPE_ID)(address))


#ifndef QUEUE_ENABLE_LOGGING

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

//public:
//	__int64 _failEnqCAS1 = 0;
//	__int64 _failEnqCAS2 = 0;
//	__int64 _failEnqCAS3 = 0;
//	__int64 _waitDeq = 0;
//	__int64 _failDeqCAS1 = 0;
//	__int64 _failDeqCAS2 = 0;
//	__int64 _succEnqCAS1 = 0;
//	__int64 _succEnqCAS2 = 0;
//	__int64 _succEnqCAS3 = 0;
//	__int64 _succDeqCAS1 = 0;
//	__int64 _succDeqCAS2 = 0;
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





#endif






/*
변경사항
@ 20221208
Interlock에 사용되는 _headId, _tailId, _size 멤버의 위치를 맨 위로 옮기고 아래와 같이 alignas(64) 함.

그리고 락프리큐가 64byte 경계에 생성되는것을 보장하기위해 new, delete 함수를 오버라이딩함
	void* operator new(size_t size);
	void operator delete(void* p);

테스트CPU: Intel(R) Core(TM) i5-7500 CPU @ 3.40GHz
테스트과정: 1000 번씩 Enqueue, Dequeue 하는것을 30초동안 수행한뒤 총 수행횟수를 비교함
# alignas 적용전, 1스레드
[30 sec] total proc:857630000, avg:28587666/s
[30 sec] total proc:857716000, avg:28590533/s
[30 sec] total proc:858159658, avg:28605321/s
# alignas 적용후, 1스레드
[30 sec] total proc:782044000, avg:26068133/s
[30 sec] total proc:852732000, avg:28424400/s
[30 sec] total proc:856797246, avg:28559908/s

# alignas 적용전, 2스레드
[30 sec] total proc:197535287, avg:6584509/s
[30 sec] total proc:185392768, avg:6179758/s
[30 sec] total proc:190456666, avg:6348555/s
# alignas 적용후, 2스레드
[30 sec] total proc:217065788,  avg:7235526/s
[30 sec] total proc:257293159, avg:8576438/s
[30 sec] total proc:252393576, avg:8413119/s

# alignas 적용전, 4스레드
[30 sec] total proc:204339072, avg:6772473/s
[30 sec] total proc:192087946, avg:6369808/s
[30 sec] total proc:201179615, avg:6667758/s
# alignas 적용후, 4스레드
[30 sec] total proc:248866454, avg:8252634/s
[30 sec] total proc:251760561, avg:8348605/s
[30 sec] total proc:246821642, avg:8184826/s


@ 20221208
Enqueue 에서의 CAS3, Dequeue 에서의 CAS1을 수행하기 전 CAS하려는 대상 변수값이 변경되었을경우 CAS를 수행하지 않도록 하는 코드를 추가함
InterlockedCompareExchange64 함수의 호출횟수를 감소시켰기 때문에 속도가 향상될거라 생각했지만 실제로는 속도에 차이가 없는것으로 보임

# 수정전, 2스레드
[30 sec] total proc:190191149, avg:6339704/s, try fail/try Enq [CAS1:(98973459, 0.0392), CAS2:(95095453, 0.1063), CAS3:(32832775, 0.6922)], Deq [wait:0, CAS1:(104259731, 0.0881), CAS2:(0, 0.0000)]
[30 sec] total proc:183708525, avg:6123617/s, try fail/try Enq [CAS1:(94771573, 0.0308), CAS2:(91853546, 0.0892), CAS3:(28373118, 0.7111)], Deq [wait:0, CAS1:(98894753, 0.0717), CAS2:(0, 0.0000)]
# Enq CAS3 실패방지 코드삽입, 2스레드
[30 sec] total proc:192388565, avg:6412952/s, try fail/try Enq [CAS1:(98110603, 0.0196), CAS2:(96193460, 0.0446), CAS3:(8415796, 0.4907)], Deq [wait:0, CAS1:(110890427, 0.1328), CAS2:(0, 0.0000)]
[30 sec] total proc:186085782, avg:6202859/s, try fail/try Enq [CAS1:(95098195, 0.0216), CAS2:(93042404, 0.0515), CAS3:(11473483, 0.5827)], Deq [wait:0, CAS1:(100437509, 0.0743), CAS2:(0, 0.0000)]
# Enq CAS3, Deq CAS1 실패방지 코드삽입, 2스레드
[30 sec] total proc:185144813, avg:6171493/s, try fail/try Enq [CAS1:(94634246, 0.0218), CAS2:(92572351, 0.0633), CAS3:(11783934, 0.5031)], Deq [wait:0, CAS1:(96810495, 0.0446), CAS2:(0, 0.0000)]
[30 sec] total proc:192644616, avg:6421487/s, try fail/try Enq [CAS1:(99008120, 0.0272), CAS2:(96321038, 0.0777), CAS3:(14345427, 0.4786)], Deq [wait:0, CAS1:(100657560, 0.0439), CAS2:(0, 0.0000)]
[30 sec] total proc:190692308, avg:6356410/s, try fail/try Enq [CAS1:(97668897, 0.0238), CAS2:(95346182, 0.0631), CAS3:(13366106, 0.5502)], Deq [wait:0, CAS1:(100229404, 0.0493), CAS2:(0, 0.0000)]

# 수정전, 4스레드
[30 sec] total proc:198766097, avg:6591262/s, try fail/try Enq [CAS1:(135530405, 0.2668), CAS2:(99369977, 0.2752), CAS3:(84520909, 0.6764)], Deq [wait:270, CAS1:(141756363, 0.3004), CAS2:(21, 0.0476)]
[30 sec] total proc:194735180, avg:6457593/s, try fail/try Enq [CAS1:(130386596, 0.2536), CAS2:(97358411, 0.2613), CAS3:(76772622, 0.6686)], Deq [wait:291, CAS1:(131044198, 0.2584), CAS2:(22, 0.0455)]
# Enq CAS3 실패방지 코드삽입, 4스레드
[30 sec] total proc:196659605, avg:6521408/s, try fail/try Enq [CAS1:(133410069, 0.2631), CAS2:(98318800, 0.1809), CAS3:(39260982, 0.5469)], Deq [wait:257, CAS1:(128562612, 0.2364), CAS2:(25, 0.0800)]
[30 sec] total proc:193836644, avg:6427796/s, try fail/try Enq [CAS1:(131936699, 0.2656), CAS2:(96908652, 0.1810), CAS3:(38111087, 0.5398)], Deq [wait:225, CAS1:(127233468, 0.2393), CAS2:(20, 0.0000)]
# Enq CAS3, Deq CAS1 실패방지 코드삽입, 4스레드
[30 sec] total proc:192362968, avg:6378716/s, try fail/try Enq [CAS1:(129649800, 0.2583), CAS2:(96174018, 0.1570), CAS3:(35214765, 0.5713)], Deq [wait:221, CAS1:(108611533, 0.1154), CAS2:(10, 0.2000)]
[30 sec] total proc:191583241, avg:6352861/s, try fail/try Enq [CAS1:(129648631, 0.2612), CAS2:(95781728, 0.1794), CAS3:(37504953, 0.5418)], Deq [wait:248, CAS1:(108468147, 0.1187), CAS2:(12, 0.0000)]
[30 sec] total proc:194087987, avg:6432718/s, try fail/try Enq [CAS1:(131839486, 0.2640), CAS2:(97037889, 0.1863), CAS3:(38056591, 0.5250)], Deq [wait:234, CAS1:(110362459, 0.1228), CAS2:(16, 0.1875)]


# 카운팅코드 삭제, 수정전, 1스레드
[30 sec] total proc:722361906, avg:24078730/s
[30 sec] total proc:841131804, avg:28037726/s
[30 sec] total proc:841730000, avg:28057666/s
# 카운팅코드 삭제, Enq CAS3, Deq CAS1 실패방지 코드삽입, 1스레드
[30 sec] total proc:851920000, avg:28397333/s
[30 sec] total proc:852656000, avg:28421866/s
[30 sec] total proc:854117828, avg:28470594/s

# 카운팅코드 삭제, 수정전, 2스레드
[30 sec] total proc:216095426, avg:7203180/s
[30 sec] total proc:229330729, avg:7644357/s
[30 sec] total proc:232512672, avg:7750422/s
# 카운팅코드 삭제, Enq CAS3, Deq CAS1 실패방지 코드삽입, 2스레드
[30 sec] total proc:251136000, avg:8371200/s
[30 sec] total proc:225919388, avg:7530646/s
[30 sec] total proc:257385758, avg:8579525/s

# 카운팅코드 삭제, 수정전, 4스레드
[30 sec] total proc:248214265, avg:8226642/s
[30 sec] total proc:245319252, avg:8135006/s
[30 sec] total proc:243234121, avg:8065861/s
[30 sec] total proc:222348578, avg:7373278/s
[30 sec] total proc:242872797, avg:8049608/s
[30 sec] total proc:219820888, avg:7289216/s
# 카운팅코드 삭제, Enq CAS3, Deq CAS1 실패방지 코드삽입, 4스레드
[30 sec] total proc:221938271, avg:7359672/s
[30 sec] total proc:242066868, avg:8023163/s
[30 sec] total proc:214364407, avg:7108515/s
[30 sec] total proc:248326687, avg:8234735/s
[30 sec] total proc:243547632, avg:8075990/s
[30 sec] total proc:219237326, avg:7270106/s
[30 sec] total proc:244917608, avg:8121418/s

*/










#ifdef QUEUE_ENABLE_LOGGING
template <typename _Ty>
class CLockfreeQueue
{
public:
	struct Node
	{
		Node* pNext;
		_Ty obj;
	};

private:
	TYPE_ID _headId;
	TYPE_ID _tailId;
	long _size;

	CMemoryPool<Node>* _pPoolNode;
	TYPE_ID _addressMask;   // TYPE_ID 값에서 주소값을 얻기위한 mask
	int _addressBitCount;   // 유저영역 주소의 bit 수. TYPE_ID 값에서 pop count를 얻기위해 사용됨

public:
	CLockfreeQueue();
	~CLockfreeQueue();

public:
	int Size() { return _size; }
	int GetPoolSize() { return _pPoolNode->GetSize(); }

	void Enqueue(_Ty obj);
	bool Dequeue(_Ty& obj);


public:
	enum class eWorkType
	{
		ENQ1,
		ENQ2,
		ENQ3,
		DEQ1,
		DEQ2
	};
	struct ThreadLog
	{
		__int64 index;
		DWORD threadId;
		eWorkType workType;
		Node* curNode;
		Node* prevNode;
		UINT64 other;
		int procCount;
		int size;
	};

	alignas(64) int _sizeLogBuffer;
	ThreadLog* _logBuffer;
	alignas(64) __int64 _logBufferIndex;

	void Crash()
	{
		int* p = 0;
		*p = 0;
	}

	void OutputThreadLog();
	void OutputMemoryPoolLog() { _pPoolNode->OutputThreadLog(); }
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


	_sizeLogBuffer = 0x1ffff;  // 로그 버퍼 크기. 2진수 값이 모두 1로 이루어져야 함
	_logBuffer = (ThreadLog*)malloc(sizeof(ThreadLog) * _sizeLogBuffer);
	memset(_logBuffer, 0, sizeof(ThreadLog) * _sizeLogBuffer);
	_logBufferIndex = -1;

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
void CLockfreeQueue<_Ty>::Enqueue(_Ty obj)
{
	Node* pNewNode = _pPoolNode->Alloc();
	pNewNode->pNext = nullptr;
	pNewNode->obj = obj;

	TYPE_ID tailId;
	TYPE_ID tailNextId;
	TYPE_ID newTailId;
	Node* pTail;
	Node* pTailNext;
	int procCount;
	while (true)
	{
		tailId = _tailId;
		pTail = QUEUE_GET_ADDRESS(tailId);
		procCount = QUEUE_GET_PROC_COUNT(tailId);
		pTailNext = pTail->pNext;
		if (pTailNext == nullptr)
		{
			if (InterlockedCompareExchange64((__int64*)&pTail->pNext, (__int64)pNewNode, 0) == 0)
			{
				if (pTail == pTailNext)  // debugging
					Crash();
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
			tailNextId = QUEUE_MAKE_ID(procCount + 1, pTailNext);
			bool isTailChanged = InterlockedCompareExchange64((__int64*)&_tailId, (__int64)tailNextId, (__int64)tailId) == (__int64)tailId;
			if (isTailChanged == true)
			{
				__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
				int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
				_logBuffer[index].index = logBufferIndex;
				_logBuffer[index].threadId = GetCurrentThreadId();
				_logBuffer[index].workType = eWorkType::ENQ3;
				_logBuffer[index].curNode = pTail->pNext;
				_logBuffer[index].prevNode = pTail;
				_logBuffer[index].procCount = procCount + 1;
			}
		}
	}

	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = eWorkType::ENQ1;
	_logBuffer[index].curNode = pNewNode;
	_logBuffer[index].prevNode = pTail;
	_logBuffer[index].other = (UINT64)pNewNode->pNext;
	_logBuffer[index].procCount = procCount + 1;

	newTailId = QUEUE_MAKE_ID(procCount + 1, pNewNode);
	bool isTailChanged = InterlockedCompareExchange64((__int64*)&_tailId, (__int64)newTailId, (__int64)tailId) == (__int64)tailId;

	long size = InterlockedIncrement(&_size);

	logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = eWorkType::ENQ2;
	_logBuffer[index].curNode = pNewNode;
	_logBuffer[index].prevNode = pTail;
	_logBuffer[index].other = (UINT64)isTailChanged;
	_logBuffer[index].procCount = procCount + 1;
	_logBuffer[index].size = size;
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
	int procCount;
	int tailProcCount;
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
			continue;
		}

		tailId = _tailId;
		pTail = QUEUE_GET_ADDRESS(tailId);
		pTailNext = pTail->pNext;
		if (pHead == pTail && pTailNext != nullptr)
		{
			tailProcCount = QUEUE_GET_PROC_COUNT(tailId);
			tailNextId = QUEUE_MAKE_ID(tailProcCount + 1, pTailNext);
			bool isTailChanged = InterlockedCompareExchange64((__int64*)&_tailId, (__int64)tailNextId, (__int64)tailId) == (__int64)tailId;

			__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
			int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
			_logBuffer[index].index = logBufferIndex;
			_logBuffer[index].threadId = GetCurrentThreadId();
			_logBuffer[index].workType = eWorkType::DEQ2;
			_logBuffer[index].curNode = pTail->pNext;
			_logBuffer[index].prevNode = pTail;
			_logBuffer[index].other = (UINT64)isTailChanged;
			_logBuffer[index].procCount = tailProcCount + 1;
		}

		obj = pHeadNext->obj;
		procCount = QUEUE_GET_PROC_COUNT(headId);
		headNextId = QUEUE_MAKE_ID(procCount + 1, pHeadNext);
		if (InterlockedCompareExchange64((__int64*)&_headId, (__int64)headNextId, (__int64)headId) == (__int64)headId)
			break;
	}


	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = eWorkType::DEQ1;
	_logBuffer[index].curNode = pHeadNext;
	_logBuffer[index].prevNode = pHead;
	_logBuffer[index].other = (UINT64)pHeadNext->pNext;
	_logBuffer[index].procCount = procCount + 1;
	_logBuffer[index].size = size;

	_pPoolNode->Free(pHead);
	return true;
}







template <typename _Ty>
void CLockfreeQueue<_Ty>::OutputThreadLog()
{
	// 현재 날짜와 시간을 알아온다.
	WCHAR filename[MAX_PATH];
	SYSTEMTIME stNowTime;
	GetLocalTime(&stNowTime);
	wsprintf(filename, L"CLockfreeQueue_%d%02d%02d_%02d%02d%02d.txt",
		stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);

	// 시작 인덱스 계산
	int sizeLogBuffer = _sizeLogBuffer;
	__int64 logBufferIndex = _logBufferIndex;
	int startIndex;
	int endIndex = (int)(logBufferIndex + 1 & (__int64)sizeLogBuffer);
	if (logBufferIndex < sizeLogBuffer)
		startIndex = 0;
	else
		startIndex = (int)((logBufferIndex + 1) & (__int64)sizeLogBuffer);

	// 파일 열기
	HANDLE hLogFile = ::CreateFile(filename,
		GENERIC_WRITE,
		FILE_SHARE_WRITE,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	// 로그 출력
	int index = startIndex;
	wchar_t szLog[1000];
	int lenSzLog;
	bool isFirst = true;
	while (isFirst == true || index != endIndex)
	{
		isFirst = false;

		ThreadLog& log = _logBuffer[index];

		if (log.workType == eWorkType::ENQ1)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  ENQ1  id: %5u,      tail: %p          next: %p %d (alloc), next: %p\n"
				, log.index, log.threadId, log.prevNode, log.curNode, log.procCount, (Node*)log.other);
		else if (log.workType == eWorkType::ENQ2)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  ENQ2  id: %5u, prev tail: %p       -> tail: %p %d (alloc), size:%d %s\n"
				, log.index, log.threadId, log.prevNode, log.curNode, log.procCount, log.size, (int)log.other == 0? L" !!change tail failed!!" : L"");
		else if (log.workType == eWorkType::ENQ3)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  ENQ3  id: %5u, prev tail: %p        -> tail: %p %d (rectify)\n"
				, log.index, log.threadId, log.prevNode, log.curNode, log.procCount);
		else if (log.workType == eWorkType::DEQ1)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  DEQ1  id: %5u, prev head: %p (free) -> head: %p %d, next: %p, size:%d\n"
				, log.index, log.threadId, log.prevNode, log.curNode, log.procCount, (Node*)log.other, log.size);
		else if (log.workType == eWorkType::DEQ2)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  DEQ2  id: %5u, prev tail: %p        -> tail: %p %d (rectify) %s\n"
				, log.index, log.threadId, log.prevNode, log.curNode, log.procCount, (int)log.other == 0 ? L" !!change tail failed!!" : L"");
		else
			lenSzLog = swprintf_s(szLog, 1000, L"error, type:%d\n", (int)log.workType);

		WriteFile(hLogFile, szLog, lenSzLog * sizeof(wchar_t), NULL, NULL);

		index++;
		if (index >= sizeLogBuffer)
			index = 0;
	}

	CloseHandle(hLogFile);
}
#endif

