#pragma once

/* lockfree 메모리 풀 */

#include <stdlib.h>
#include <Windows.h>

//#define MEMPOOL_ENABLE_LOGGING

using TYPE_ID = unsigned __int64;

#define MEMPOOL_GET_ADDRESS(Id) (BlockNode*)((Id) & _addressMask)
#define MEMPOOL_GET_ALLOC_COUNT(Id) (int)((Id) >> _addressBitCount)
#define MEMPOOL_MAKE_ID(address) (((TYPE_ID)(address->allocCount) << _addressBitCount) | (TYPE_ID)(address))

namespace netserver
{

template <typename _Ty>
class alignas(64) CMemoryPool
{
private:
	struct BlockNode;

	TYPE_ID _topId;  // 현재 사용가능한 노드의 주소에 alloc 횟수를 합친 값 (64byte aligned)
	alignas(64) long _freeSize;  // pool 내의 미사용 객체 갯수 (64byte aligned)

	alignas(64) long _initPoolSize;     // 초기 pool size
	long _poolSize;         // 현재 pool size	
	bool _bPlacementNew;    // true 면 Alloc, Free 할 때 마다 객체의 생성자, 소멸자 호출. 
							// false면 객체가 처음 생성될 때 한 번만 생성자 호출, 메모리풀이 소멸될 때 모든 객체에서 소멸자 호출
	bool _isPoolVariable;   // false면 memory pool, true면 free list

	unsigned __int64 _leftVerifyCode;
	unsigned __int64 _rightVerifyCode;
	TYPE_ID _addressMask;   // TYPE_ID 값에서 주소값을 얻기위한 mask
	__int64 _addressBitCount;    // 유저영역 주소의 bit 수. TYPE_ID 값에서 alloc count를 얻기위해 사용됨



public:
	CMemoryPool(int initPoolSize, bool bFreeList, bool bPlacementNew);
	~CMemoryPool();

public:
	long GetInitSize() { return _initPoolSize; }
	long GetPoolSize() { return _poolSize; }
	long GetFreeCount() { return _freeSize; }
	long GetUseCount() { return _poolSize - _freeSize; }

	_Ty* Alloc();
	void Free(_Ty* ptr);

	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

private:
	void Crash();

private:
	//#pragma pack(push, 1)
	struct BlockNode
	{
		BlockNode* pNext;
		unsigned __int64 leftVerifyCode;
		_Ty object;
		int allocCount;
		unsigned __int64 rightVerifyCode;

		BlockNode(unsigned __int64 leftVerifyCode, unsigned __int64 rightVerifyCode)
			: pNext(nullptr), leftVerifyCode(leftVerifyCode), rightVerifyCode(rightVerifyCode), allocCount(0)
		{}
	};
	//#pragma pack(pop)

#ifdef MEMPOOL_ENABLE_LOGGING
public:
	enum class eWorkType
	{
		ALLOC,
		ALLOC_NEW,
		FREE
	};
	struct ThreadLog 
	{
		__int64 index;
		DWORD threadId;
		eWorkType workType;
		TYPE_ID topId;     // 현재 top
		TYPE_ID topNextId; // 현재 top의 next
		TYPE_ID otherId;   // ALLOC일 경우 반환된 노드, FREE일 경우 이전 top의 next
	}; // size = 40

	alignas(64) int _sizeLogBuffer;
	ThreadLog* _logBuffer;
	alignas(64) __int64 _logBufferIndex;

	void OutputThreadLog();
#endif
};



template <typename _Ty>
CMemoryPool<_Ty>::CMemoryPool(int initPoolSize, bool bFreeList, bool bPlacementNew)
	:_initPoolSize(initPoolSize), _poolSize(initPoolSize), _freeSize(initPoolSize)
	, _bPlacementNew(bPlacementNew), _isPoolVariable(bFreeList), _addressBitCount(0)
	, _topId(0)
{

	// 검증코드 생성
	_leftVerifyCode =
		(unsigned __int64)rand() << 60
		| (unsigned __int64)rand() << 45
		| (unsigned __int64)rand() << 30
		| (unsigned __int64)rand() << 15
		| (unsigned __int64)rand();

	_rightVerifyCode =
		(unsigned __int64)rand() << 60
		| (unsigned __int64)rand() << 45
		| (unsigned __int64)rand() << 30
		| (unsigned __int64)rand() << 15
		| (unsigned __int64)rand();

	// 주소 mask 계산
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	_addressMask = (TYPE_ID)sysInfo.lpMaximumApplicationAddress;
	for (int i = 63; i >= 0; i--)
	{
		if (_addressMask & (1i64 << i))
		{
			_addressMask = 0xFFFFFFFF'FFFFFFFF >> (63 - i);  // 0x00007fff'fffeffff
			_addressBitCount = i + 1;                        // 47
			break;
		}
	}

	BlockNode* pNewNode;
	BlockNode* pPrevNode;
	// 초기 노드 생성
	if (_initPoolSize > 0)
	{
		// 첫 노드 생성 및 초기화
		pNewNode = new BlockNode(_leftVerifyCode, _rightVerifyCode);
		if (_bPlacementNew == false)
		{
			// 생성자 호출
			new (&pNewNode->object) _Ty;
		}
		_topId = (TYPE_ID)pNewNode;
		pPrevNode = pNewNode;

		// 모든 노드 생성 및 초기화
		for (int iCnt = 1; iCnt < _initPoolSize; iCnt++)
		{
			pNewNode = new BlockNode(_leftVerifyCode, _rightVerifyCode);
			if (_bPlacementNew == false)
			{
				// 생성자 호출
				new (&pNewNode->object) _Ty;
			}

			pPrevNode->pNext = pNewNode;
			pPrevNode = pNewNode;
		}
		pNewNode->pNext = nullptr;
	}


#ifdef MEMPOOL_ENABLE_LOGGING
	_sizeLogBuffer = 0x1ffff;  // 2진수 값이 모두 1로 이루어져야 함
	_logBuffer = (ThreadLog*)malloc(sizeof(ThreadLog) * _sizeLogBuffer);
	memset(_logBuffer, 0, sizeof(ThreadLog) * _sizeLogBuffer);
	_logBufferIndex = -1;
#endif

}





template <typename _Ty>
CMemoryPool<_Ty>::~CMemoryPool()
{
	BlockNode* pNode = MEMPOOL_GET_ADDRESS(_topId);
	BlockNode* pNextNode = pNode;
	// pool 내의 모든 노드를 삭제
	for (int iCnt = 0; iCnt < _freeSize; iCnt++)
	{
		pNode = pNextNode;
		if (_bPlacementNew == false)
		{
			// 소멸자 호출
			(pNode->object).~_Ty();
		}
		pNextNode = pNode->pNext;
		free(pNode);
	}

}






template <typename _Ty>
_Ty* CMemoryPool<_Ty>::Alloc()
{
	TYPE_ID topId;
	TYPE_ID newTopId;
	BlockNode* pTop;
	BlockNode* pNewTop = nullptr;
#ifdef MEMPOOL_ENABLE_LOGGING
	BlockNode* pNewTopNext = nullptr; // debug
#endif
	bool isNewNode = false;
	do {
		topId = _topId;
		pTop = MEMPOOL_GET_ADDRESS(topId);

		// 현재 pool 내에 남은 노드가 없을 경우
		if (pTop == nullptr)
		{
			// free list일 경우
			if (_isPoolVariable)
			{
				// 현재 남은노드가 없는데 free list일 경우 노드 생성
				pTop = new BlockNode(_leftVerifyCode, _rightVerifyCode);
				isNewNode = true;
				InterlockedIncrement(&_poolSize);
				break; // 새로운 노드를 생성하였을 경우 현재의 top을 변경하지 않고 새로운 노드만을 리턴한다.
			}
			// memory pool일 경우
			else
			{
				// 현재 free node가 없는데 free list가 아닐 경우 crash
				Crash();
				break;
			}
		}
		else
		{
			pNewTop = pTop->pNext;
			if (pNewTop == nullptr)
			{
				newTopId = 0;
#ifdef MEMPOOL_ENABLE_LOGGING
				pNewTopNext = nullptr; // debug
#endif
			}
			else
			{
				newTopId = MEMPOOL_MAKE_ID(pNewTop);
#ifdef MEMPOOL_ENABLE_LOGGING
				pNewTopNext = pNewTop->pNext; // debug
#endif
			}
		}
	} while (InterlockedCompareExchange64((__int64*)&_topId, (__int64)newTopId, (__int64)topId) != (__int64)topId);

#ifdef MEMPOOL_ENABLE_LOGGING
	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = isNewNode == false ? eWorkType::ALLOC : eWorkType::ALLOC_NEW;
	_logBuffer[index].topId = isNewNode == false ? newTopId : 0;
	_logBuffer[index].topNextId = pNewTopNext == nullptr ? 0 : MEMPOOL_MAKE_ID(pNewTopNext);
	_logBuffer[index].otherId = MEMPOOL_MAKE_ID(pTop);
#endif

	if (isNewNode == false)
		InterlockedDecrement(&_freeSize);

	// 새로 생성된 노드이거나 placementNew 일 경우 생성자 호출
	if (_bPlacementNew == true
		|| (isNewNode == true && _bPlacementNew == false))
	{
		new (&pTop->object) _Ty;
	}

	// 노드의 alloc count +1
	pTop->allocCount++;

	return &pTop->object;
}




template <typename _Ty>
void CMemoryPool<_Ty>::Free(_Ty* ptr)
{
	// 코드 검증
	BlockNode* pFreeNode = (BlockNode*)((char*)ptr - (unsigned __int64)(&((BlockNode*)nullptr)->object));
	if (pFreeNode->leftVerifyCode != _leftVerifyCode
		|| pFreeNode->rightVerifyCode != _rightVerifyCode)
	{
		Crash();
	}

	if (_bPlacementNew)
	{
		// 소멸자 호출
		(pFreeNode->object).~_Ty();
	}

	// _topId를 Free된 노드로 교체 (lockfree)
	TYPE_ID topId;
	TYPE_ID freeNodeId = MEMPOOL_MAKE_ID(pFreeNode);
	BlockNode* pTop;
	do {
		topId = _topId;
		pTop = MEMPOOL_GET_ADDRESS(topId);
		pFreeNode->pNext = pTop;
	} while (InterlockedCompareExchange64((__int64*)&_topId, (__int64)freeNodeId, (__int64)topId) != (__int64)topId);

#ifdef MEMPOOL_ENABLE_LOGGING
	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = eWorkType::FREE;
	_logBuffer[index].topId = freeNodeId;
	_logBuffer[index].topNextId = topId;
	_logBuffer[index].otherId = pTop == nullptr ? 0 : (pTop->pNext == nullptr ? 0 : MEMPOOL_MAKE_ID(pTop->pNext));
#endif

	InterlockedIncrement(&_freeSize);
}


// 64byte aligned 객체 생성을 위한 new, delete overriding
template <typename _Ty>
void* CMemoryPool<_Ty>::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}
template <typename _Ty>
void CMemoryPool<_Ty>::operator delete(void* p)
{
	_aligned_free(p);
}




template <typename _Ty>
void CMemoryPool<_Ty>::Crash()
{
	int* p = nullptr;
	*p = 0;
}





#ifdef MEMPOOL_ENABLE_LOGGING
template <typename _Ty>
void CMemoryPool<_Ty>::OutputThreadLog()
{
	// 현재 날짜와 시간을 알아온다.
	WCHAR filename[MAX_PATH];
	SYSTEMTIME stNowTime;
	GetLocalTime(&stNowTime);
	wsprintf(filename, L"CMemoryPool_%d%02d%02d_%02d%02d%02d.txt",
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

		BlockNode* topNode = MEMPOOL_GET_ADDRESS(log.topId);
		int topAllocCount = MEMPOOL_GET_ALLOC_COUNT(log.topId);
		BlockNode* topNextNode = MEMPOOL_GET_ADDRESS(log.topNextId);
		int topNextAllocCount = MEMPOOL_GET_ALLOC_COUNT(log.topNextId);
		BlockNode* otherNode = MEMPOOL_GET_ADDRESS(log.otherId);
		int otherAllocCount = MEMPOOL_GET_ALLOC_COUNT(log.otherId);

		if (log.workType == eWorkType::ALLOC)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  ALLOC      id: %5u, old top: %p %d (pop) -> top: %p %d, next: %p %d\n"
				, log.index, log.threadId, otherNode, otherAllocCount, topNode, topAllocCount, topNextNode, topNextAllocCount);
		else if (log.workType == eWorkType::ALLOC_NEW)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  ALLOC_NEW  id: %5u, old top: %p %d (new) -> top: %p %d, next: %p %d\n"
				, log.index, log.threadId, otherNode, otherAllocCount, topNode, topAllocCount, topNextNode, topNextAllocCount);
		else if (log.workType == eWorkType::FREE)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  FREE       id: %5u, old top: %p %d       -> top: %p %d (push)\n"
				, log.index, log.threadId, topNextNode, topNextAllocCount, topNode, topAllocCount);
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



}


/*
변경사항
@ 20221207
InterlockedCompareExchange에 사용되는 _topId 멤버, InterlockedIncrement에 사용되는 _freeSize 멤버의 위치를 맨 위로 옮기고 아래와 같이 alignas(64) 함.
_poolSize 멤버는 alignas(64) 하지 않았는데, 노드를 새로 생성할때만 사용하는 값이기 때문에 메모리풀이 안정된 후에는 사용하지 않기 때문임.
	class alignas(64) CMemoryPool
	{
		TYPE_ID _topId;  // 현재 사용가능한 노드의 주소에 alloc 횟수를 합친 값 (64byte aligned)
		alignas(64) long _freeSize;  // pool 내의 미사용 객체 갯수 (64byte aligned)

그리고 메모리풀이 64byte 경계에 생성되는것을 보장하기위해 new, delete 함수를 오버라이딩함
	void* operator new(size_t size);
	void operator delete(void* p);

테스트CPU: Intel(R) Core(TM) i5-7500 CPU @ 3.40GHz
테스트과정: alloc count 만큼 alloc, free 하는것을 loop count만큼 반복하여 총 소요시간을 구함
num thread      object size     loop count      alloc count     total proc      time(ms)(변경전)		time(ms)(변경후)
1				8				10000			10000			100000000		3344  				3328  
1				64				10000			10000			100000000		3687  				3672  
1				1024			10000			10000			100000000		4609  				4609  
1				10240			10000			10000			100000000		6047  				5969  
2				8				10000			10000			200000000		32008 				24179 
2				64				10000			10000			200000000		29101 				26602 
2				1024			10000			10000			200000000		38664 				27718 
2				10240			10000			10000			200000000		36875 				33015 
4				8				10000			10000			400000000		115539				85613 
4				64				10000			10000			400000000		105140				87664 
4				1024			10000			10000			400000000		95148 				94156 
4				10240			10000			10000			400000000		121511				102632
*/
