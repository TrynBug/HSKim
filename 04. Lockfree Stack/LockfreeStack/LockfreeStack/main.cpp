/* main 함수에서는 Lockfree Stack을 테스트 합니다. */



#pragma comment(lib, "Winmm.lib")
#include <iostream>
#include <Windows.h>
#include <process.h>
#include <vector>
#include <sstream>
#include "CCrashDump.h"

//#define MEMPOOL_ENABLE_LOGGING
//#define STACK_ENABLE_LOGGING
#include "CLockfreeStack.h"

struct Element
{
	int _id;
	int _val;

	void SetValue(int id, int val) {
		_id = id;
		_val = val;
	}
	bool IsChanged(int id, int val)
	{
		if (_id == id && _val == val)
			return false;
		else
			return true;
	}
};

void Crash()
{
	int* p = 0;
	*p = 0;
}

std::vector<__int64> g_vecThreadProcCount;
CLockfreeStack<Element*> g_stack;
volatile bool g_shutdown = false;
unsigned WINAPI ThreadProc(PVOID pParam)
{
	static long sThreadNumber = -1;
	long threadNumber = InterlockedIncrement(&sThreadNumber);

	DWORD threadId = GetCurrentThreadId();
	printf("[Thread] begin %u\n", threadId);

	__int64 pushCount = (__int64)pParam;
	Element** arrPtrElement = new Element*[pushCount];
	for (int i = 0; i < pushCount; i++)
		arrPtrElement[i] = new Element;

	while (g_shutdown == false)
	{
		for (int i = 0; i < pushCount; i++)
		{
			g_stack.Push(arrPtrElement[i]);
			g_vecThreadProcCount[threadNumber]++;
		}
		for (int i = 0; i < pushCount; i++)
		{
			g_stack.Pop(arrPtrElement[i]);
			arrPtrElement[i]->SetValue(threadId, i);
			g_vecThreadProcCount[threadNumber]++;
		}


		//if (threadNumber == 0)
		//	Sleep(1);
		//for (int i = 0; i < pushCount; i++)
		//{
		//	if (arrPtrElement[i]->IsChanged(threadId, i) == true)
		//		Crash();
		//}
	}

	printf("[Thread] end %u\n", threadId);
	return 0;
}



void OutputLog(void* param);

int main()
{
	timeBeginPeriod(1);
	CCrashDump::Init();



	const int numThread = 4;
	__int64 pushCount = 5;

	HANDLE hThreads[numThread];
	unsigned int* threadIds = new unsigned int[numThread];
	for (int i = 0; i < numThread; i++)
	{
		g_vecThreadProcCount.push_back(0);
		hThreads[i] = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, (PVOID)pushCount, 0, &threadIds[i]);
	}
	
	// CCrashDump에 로그 출력 작업 등록
	void** paramFinalJob = new void*[3];
	paramFinalJob[0] = (void*)&g_stack;
	paramFinalJob[1] = (void*)threadIds;
	paramFinalJob[2] = (void*)numThread;
	CCrashDump::AddFinalJob(OutputLog, paramFinalJob);
	

	std::wstringstream ss;
	ULONGLONG startTick = GetTickCount64();
	ULONGLONG currentTick;
	long long totalThreadProcCount = 0;
	long long prevTotalThreadProcCount = 0;
	std::vector<__int64> vecThreadProcCount;
	while (true)
	{
		Sleep(1000);
		currentTick = GetTickCount64();

		// 스레드 작업횟수 가져오기
		vecThreadProcCount.clear();
		totalThreadProcCount = 0;
		for (int i = 0; i < numThread; i++)
		{
			vecThreadProcCount.push_back(g_vecThreadProcCount[i]);
			totalThreadProcCount += vecThreadProcCount[i];
		}

		// 로그문자열 생성
		ss.str(L"");
		ss << L"[" << (currentTick - startTick) / 1000 << L" sec] Stack size:" << g_stack.Size() << L",  Node pool size:" << g_stack.GetPoolSize()
			<< L",  Total run count: " << totalThreadProcCount;
		ss << L" (avg:" << (int)((double)totalThreadProcCount / ((double)(currentTick - startTick) / 1000.0)) << "/s),  Per thread run count: (";
		for (int i = 0; i < numThread; i++)
		{
			ss << vecThreadProcCount[i] << L", ";
			totalThreadProcCount += vecThreadProcCount[i];
		}
		ss.seekp(-2, std::ios_base::end); // 마지막 ", " 문자열 제거
		ss << L")\n";

		// print
		std::wcout << ss.str();

		prevTotalThreadProcCount = totalThreadProcCount;



		if (GetAsyncKeyState('Q') || GetAsyncKeyState('q'))
		{
			if (GetConsoleWindow() == GetForegroundWindow())
			{
				printf("shutdown\n");
				g_shutdown = true;
				Sleep(1000);
				break;
			}
		}
		
	}

	return 0;
}












void OutputLog(void* param)
{
	// 파라미터 얻기
	void** params = (void**)param;
	CLockfreeStack<Element*>& stack = *(CLockfreeStack<Element*>*)params[0];
	DWORD* threadIds = (DWORD*)params[1];
	int numThread = (int)params[2];


	// 내 스레드가 아닌 다른 스레드를 정지시킨다.
	DWORD myThreadId = GetCurrentThreadId();
	HANDLE hThread;
	for (int i = 0; i < numThread; i++)
	{
		if (threadIds[i] != myThreadId)
		{
			hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadIds[i]);
			SuspendThread(hThread);
		}
	}

	
#ifdef STACK_ENABLE_LOGGING
	// stack 로그 출력
	stack.OutputThreadLog();
	wprintf(L"finisned output stack log\n");
#endif

#ifdef MEMPOOL_ENABLE_LOGGING
	// memory pool 로그 출력
	stack.OutputMemoryPoolLog();
	wprintf(L"finisned output memory pool log\n");
#endif

}