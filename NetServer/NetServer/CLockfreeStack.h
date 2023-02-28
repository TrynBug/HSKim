#pragma once
#include <Windows.h>
#include "CMemoryPool.h"

//#define STACK_ENABLE_LOGGING


using TYPE_ID = unsigned __int64;

#define STACK_GET_ADDRESS(Id) (Node*)((Id) & _addressMask)
#define STACK_GET_PROC_COUNT(Id) (int)((Id) >> _addressBitCount)
#define STACK_MAKE_ID(procCount, address) (((TYPE_ID)(procCount) << _addressBitCount) | (TYPE_ID)(address))


template <typename _Ty>
class alignas(64) CLockfreeStack
{
private:
	struct Node
	{
		Node* pNext;
		_Ty obj;
	};

	TYPE_ID _topId;             // 64byte aligned
	alignas(64) long _size;     // 64byte aligned

	alignas(64) CMemoryPool<Node> _poolNode;
	TYPE_ID _addressMask;   // TYPE_ID 값에서 주소값을 얻기위한 mask
	int _addressBitCount;   // 유저영역 주소의 bit 수. TYPE_ID 값에서 pop count를 얻기위해 사용됨

public:
	CLockfreeStack();
	~CLockfreeStack();

public:
	int Size() { return _size; }
	int GetPoolSize() { return _poolNode.GetPoolSize(); }

	void Push(_Ty obj);
	bool Pop(_Ty& obj);

	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

#ifdef STACK_ENABLE_LOGGING
public:
	enum class eWorkType
	{
		PUSH,
		POP
	};
	struct ThreadLog
	{
		__int64 index;
		DWORD threadId;
		eWorkType workType;
		Node* topNode;      // 현재 top
		Node* topNextNode;  // 현재 top의 next
		Node* otherNode;    // PUSH일 경우 이전 top의 next, POP일 경우 pop된 노드
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
	void OutputMemoryPoolLog() { _poolNode.OutputThreadLog(); }
#endif
};



template <typename _Ty>
CLockfreeStack<_Ty>::CLockfreeStack()
	:_topId(0), _size(0), _poolNode(0, true, false), _addressBitCount(0)
{
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

#ifdef STACK_ENABLE_LOGGING
	_sizeLogBuffer = 0x1ffff;  // 2진수 값이 모두 1로 이루어져야 함
	_logBuffer = (ThreadLog*)malloc(sizeof(ThreadLog) * _sizeLogBuffer);
	memset(_logBuffer, 0, sizeof(ThreadLog) * _sizeLogBuffer);
	_logBufferIndex = -1;
#endif
}


template <typename _Ty>
CLockfreeStack<_Ty>::~CLockfreeStack()
{
	// 모든 노드를 free 한다.
	Node* pNode = STACK_GET_ADDRESS(_topId);
	Node* pNext;
	while (pNode != nullptr)
	{
		_size--;
		pNext = pNode->pNext;
		_poolNode.Free(pNode);
		pNode = pNext;
	}

	// 메모리풀의 소멸자에서 모든 노드를 delete 한다.
}



template <typename _Ty>
void CLockfreeStack<_Ty>::Push(_Ty obj)
{
	Node* pNewNode = _poolNode.Alloc();
	pNewNode->obj = obj;

	TYPE_ID topId;
	TYPE_ID newNodeId;
	Node* pTop;
	do {
		topId = _topId;
		pTop = STACK_GET_ADDRESS(topId);
		pNewNode->pNext = pTop;
		newNodeId = STACK_MAKE_ID(STACK_GET_PROC_COUNT(topId) + 1, pNewNode);
	} while (InterlockedCompareExchange64((__int64*)&_topId, (__int64)newNodeId, (__int64)topId) != (__int64)topId);  // Push

#ifdef STACK_ENABLE_LOGGING
	// Interlock 함수로 Push를 성공한 직후 메모리에 로그를 기록한다.
	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);     // 로그 번호 얻기
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);           // 로그를 기록할 배열 인덱스 얻기
	_logBuffer[index].index = logBufferIndex;                              // 로그 배열에 로그 번호 기록
	_logBuffer[index].threadId = GetCurrentThreadId();                     //            스레드ID 기록
	_logBuffer[index].workType = eWorkType::PUSH;                          //            작업 유형(Push) 기록
	_logBuffer[index].topNode = pNewNode;                                  //            현재 top 노드 주소 기록
	_logBuffer[index].topNextNode = pNewNode->pNext;                       //            현재 top->next 노드 주소 기록
	_logBuffer[index].otherNode = pTop == nullptr ? nullptr : pTop->pNext; //            이전 top 노드 주소 기록
#endif

	InterlockedIncrement(&_size);
}


template <typename _Ty>
bool CLockfreeStack<_Ty>::Pop(_Ty& obj)
{
	TYPE_ID topId;
	TYPE_ID newTopId;
	Node* pTop;
	Node* pNewTop;

	if (_size <= 0)
		return false;
	long size = InterlockedDecrement(&_size);
	if (size < 0)
	{
		InterlockedIncrement(&_size);
		return false;
	}

	do {
		topId = _topId;
		pTop = STACK_GET_ADDRESS(topId);
		pNewTop = pTop->pNext;
		newTopId = STACK_MAKE_ID(STACK_GET_PROC_COUNT(topId) + 1, pNewTop);
	} while (InterlockedCompareExchange64((__int64*)&_topId, (__int64)newTopId, (__int64)topId) != (__int64)topId);

#ifdef STACK_ENABLE_LOGGING
	__int64 logBufferIndex = InterlockedIncrement64(&_logBufferIndex);
	int index = (int)(logBufferIndex & (__int64)_sizeLogBuffer);
	_logBuffer[index].index = logBufferIndex;
	_logBuffer[index].threadId = GetCurrentThreadId();
	_logBuffer[index].workType = eWorkType::POP;
	_logBuffer[index].topNode = pNewTop;
	_logBuffer[index].topNextNode = pNewTop == nullptr ? nullptr : pNewTop->pNext;
	_logBuffer[index].otherNode = pTop;
#endif

	obj = pTop->obj;
	_poolNode.Free(pTop);
	return true;
}



// 64byte aligned 객체 생성을 위한 new, delete overriding
template <typename _Ty>
void* CLockfreeStack<_Ty>::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}
template <typename _Ty>
void CLockfreeStack<_Ty>::operator delete(void* p)
{
	_aligned_free(p);
}





/*
변경사항
@ 20221208
Interlock에 사용되는 _topId, _size 멤버의 위치를 맨 위로 옮기고 alignas(64) 함.

그리고 메모리풀이 64byte 경계에 생성되는것을 보장하기위해 new, delete 함수를 오버라이딩함
	void* operator new(size_t size);
	void operator delete(void* p);

테스트CPU: Intel(R) Core(TM) i5-7500 CPU @ 3.40GHz
테스트과정: 5번 push, pop 하는것을 30초동안 반복하여 총 시행 횟수를 비교함
# alignas 미적용, 4스레드
[30 sec] stack size:13, thread proc count: (151540, 74652890, 77443153, 75436301, )
[30 sec] stack size:9, thread proc count: (151070, 75477021, 75276392, 77536356, )
[30 sec] stack size:10, thread proc count: (151660, 74034551, 75360611, 76260951, )
[30 sec] stack size:9, thread proc count: (151240, 76835561, 75547718, 75325760, )
# alignas 적용, 4스레드
[30 sec] stack size:9, thread proc count: (151800, 81863963, 82414467, 82377860, )
[30 sec] stack size:5, thread proc count: (151030, 82027166, 82167928, 82490583, )
[30 sec] stack size:11, thread proc count: (151710, 82719828, 81855998, 83242741, )
[30 sec] stack size:9, thread proc count: (151090, 83410171, 83857833, 82899067, )
*/




#ifdef STACK_ENABLE_LOGGING
template <typename _Ty>
void CLockfreeStack<_Ty>::OutputThreadLog()
{
	// 현재 날짜와 시간을 알아온다.
	WCHAR filename[MAX_PATH];
	SYSTEMTIME stNowTime;
	GetLocalTime(&stNowTime);
	wsprintf(filename, L"CLockfreeStack_%d%02d%02d_%02d%02d%02d.txt",
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

		if (log.workType == eWorkType::PUSH)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  PUSH  id: %5u, old top: %p        -> top: %p (alloc)\n"
				, log.index, log.threadId, log.topNextNode, log.topNode);
		else if (log.workType == eWorkType::POP)
			lenSzLog = swprintf_s(szLog, 1000, L"#%llu  POP   id: %5u, old top: %p (free) -> top: %p, next: %p\n"
				, log.index, log.threadId, log.otherNode, log.topNode, log.topNextNode);
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