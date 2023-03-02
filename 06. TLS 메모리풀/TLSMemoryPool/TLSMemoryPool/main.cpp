/* main 함수에서는 TLS 메모리풀을 테스트 합니다. */


#pragma comment(lib, "winmm.lib")
#include <conio.h>
#include <iostream>
#include <Windows.h>
#include <process.h>
#include <psapi.h>
#include <vector>
#include "CMemoryPoolTLS.h"


void Crash()
{
	int* p = 0;
	*p = 0;
}

__int64 g_firstTimeSpent = 0;
__int64 g_timeSpent = 0;

class CCC
{
public:
	char c[100] = { 0, };
};

CMemoryPoolTLS<CCC>* g_pool;
volatile bool g_shutdown = false;
const int g_numThread = 4;
const int g_chunkSize = 100;
const int g_sizeObjectArray = 32768;
CCC* g_arrObject[g_sizeObjectArray] = { 0, };
SRWLOCK g_arrSRWL[g_sizeObjectArray];


__int64 g_arrAllocCount[g_numThread] = { 0, };
__int64 g_arrFreeCount[g_numThread] = { 0, };
unsigned WINAPI ThreadProc(PVOID pParam)
{
	printf("begin %u\n", GetCurrentThreadId());
	__int64 threadNumber = (__int64)pParam;
	Sleep(100);

	int r;
	while (g_shutdown == false)
	{
		r = rand();
		AcquireSRWLockExclusive(&g_arrSRWL[r]);
		if (g_arrObject[r] == nullptr)
		{
			g_arrObject[r] = g_pool->Alloc();
			if (InterlockedIncrement16((short*)g_arrObject[r]->c) != 1)  // 할당받았을 때 값이 0이 아니었으면 오류
				Crash();
			g_arrAllocCount[threadNumber]++;
		}
		ReleaseSRWLockExclusive(&g_arrSRWL[r]);


		r = rand();
		AcquireSRWLockExclusive(&g_arrSRWL[r]);
		if (g_arrObject[r] != nullptr)
		{
			if (InterlockedDecrement16((short*)g_arrObject[r]->c) != 0)  // free하기 전 값이 1이 아니었으면 오류
				Crash();
			g_pool->Free(g_arrObject[r]);
			g_arrObject[r] = nullptr;
			g_arrFreeCount[threadNumber]++;
		}
		ReleaseSRWLockExclusive(&g_arrSRWL[r]);
	}

	for (int i = 0; i < g_sizeObjectArray; i++)
	{
		AcquireSRWLockExclusive(&g_arrSRWL[i]);
		if (g_arrObject[i] != nullptr)
		{
			g_pool->Free(g_arrObject[i]);
			g_arrObject[i] = nullptr;
		}
		ReleaseSRWLockExclusive(&g_arrSRWL[i]);
	}

	//if ((__int64)pParam == 1)
	//{
	//	Sleep(1000);
	//	arrCCC[0] = g_pool->Alloc();
	//}


	printf("done %u\n", GetCurrentThreadId());
	return 0;
}



void StartSanityTest()
{

	CMemoryPoolTLS<CCC> pool(0, false, g_chunkSize);
	g_pool = &pool;
	for (int i = 0; i < g_sizeObjectArray; i++)
	{
		InitializeSRWLock(&g_arrSRWL[i]);
	}

	for (__int64 i = 0; i < g_numThread; i++)
	{
		_beginthreadex(NULL, 0, ThreadProc, (PVOID)i, 0, NULL);
	}

	__int64 allocCount[g_numThread] = { 0, };
	__int64 freeCount[g_numThread] = { 0, };
	__int64 prevAllocCount[g_numThread] = { 0, };
	__int64 prevFreeCount[g_numThread] = { 0, };
	__int64 sumAllocCount = 0;
	__int64 sumFreeCount = 0;
	__int64 whileCount = 0;
	while (true)
	{
		PROCESS_MEMORY_COUNTERS_EX pmc = { 0, };
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

		for (int i = 0; i < g_numThread; i++)
		{
			allocCount[i] = g_arrAllocCount[i];
			freeCount[i] = g_arrFreeCount[i];
		}
		for (int i = 0; i < g_numThread; i++)
		{
			sumAllocCount += allocCount[i] - prevAllocCount[i];
			sumFreeCount += freeCount[i] - prevFreeCount[i];
		}

		printf("[run %lld sec]  pool state: (size:%d,  allocated chunk:%d,  free chunk:%d,  allocated object:%d),  threads: (alloc object:%lld/s,  free object:%lld/s),  memory usage:%.2fMB\n"
			, whileCount, pool.GetPoolSize(), pool.GetAllocCount() / g_chunkSize, pool.GetFreeCount() / g_chunkSize
			, pool.GetActualAllocCount(), sumAllocCount, sumFreeCount, (double)pmc.PrivateUsage / (double)(1024 * 1024));

		for (int i = 0; i < g_numThread; i++)
		{
			prevAllocCount[i] = allocCount[i];
			prevFreeCount[i] = freeCount[i];
		}
		sumAllocCount = 0;
		sumFreeCount = 0;

		Sleep(1000);
		whileCount++;
		//if (GetAsyncKeyState('q') || GetAsyncKeyState('Q'))
		//	g_shutdown = true;


	}
	return;
}







int main()
{
	StartSanityTest();
}

