#pragma comment(lib, "DbgHelp.Lib")

#include <iostream>
#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>
#include <Psapi.h>
#include <queue>

#include "CCrashDump.h"


SRWLOCK g_srwlExceptionFilter;
long CCrashDump::_DumpCount = 0;

std::queue<void (*)(void*)> g_qFinalJob;
std::queue<void*> g_qFinalJobParam;
SRWLOCK g_srwlJobQ;


void CCrashDump::Init()
{
	_DumpCount = 0;

	_invalid_parameter_handler oldHandler;
	_invalid_parameter_handler newHandler;
	newHandler = myInvalidParameterHandler;

	oldHandler = _set_invalid_parameter_handler(newHandler);  // CRT 함수에 null 포인터 등을 넣었을 때
	_CrtSetReportMode(_CRT_WARN, 0);    // CRT 오류 메시지 표시 중단. 바로 덤프로 남도록.
	_CrtSetReportMode(_CRT_ASSERT, 0);  // CRT 오류 메시지 표시 중단. 바로 덤프로 남도록.
	_CrtSetReportMode(_CRT_ERROR, 0);   // CRT 오류 메시지 표시 중단. 바로 덤프로 남도록.

	_CrtSetReportHook(_custom_Report_hook);

	// pure virtual function called 에러 핸들러를 사용자 정의 함수로 우회시킨다.
	_set_purecall_handler(myPurecallHandler);

	SetHandlerDump();

	InitializeSRWLock(&g_srwlExceptionFilter);
	InitializeSRWLock(&g_srwlJobQ);
}

void CCrashDump::Crash()
{
	int* p = nullptr;
	*p = 0;
}



void CCrashDump::AddFinalJob(void (*func)(void*), void* param)
{
	AcquireSRWLockExclusive(&g_srwlJobQ);
	g_qFinalJob.push(func);
	g_qFinalJobParam.push(param);
	ReleaseSRWLockExclusive(&g_srwlJobQ);
}


LONG WINAPI CCrashDump::MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
{
	// 최초 함수 진입 스레드만 덤프파일을 생성함
	AcquireSRWLockExclusive(&g_srwlExceptionFilter);

	// call final job
	while (true)
	{
		void (*funcPtr)(void*);
		void* funcParam;
		AcquireSRWLockExclusive(&g_srwlJobQ);
		if (g_qFinalJob.size() > 0)
		{
			funcPtr = g_qFinalJob.front();
			g_qFinalJob.pop();
			funcParam = g_qFinalJobParam.front();
			g_qFinalJobParam.pop();
		}
		else
		{
			ReleaseSRWLockExclusive(&g_srwlJobQ);
			break;
		}
		ReleaseSRWLockExclusive(&g_srwlJobQ);

		funcPtr(funcParam);
	}


	int iWorkingMemory = 0;
	SYSTEMTIME stNowTime;

	long DumpCount = InterlockedIncrement(&_DumpCount);

	// 현재 날짜와 시간을 알아온다.
	WCHAR filename[MAX_PATH];

	GetLocalTime(&stNowTime);
	wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d.dmp",
		stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, DumpCount);

	// 메모리 덤프 파일을 생성한다.
	wprintf(L"\n\n\n!!! Crash Error !!! %d.%d.%d / %d:%d:%d \n",
		stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);
	wprintf(L"Now Save dump file...\n");

	HANDLE hDumpFile = ::CreateFile(filename,
		GENERIC_WRITE,
		FILE_SHARE_WRITE,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (hDumpFile != INVALID_HANDLE_VALUE)
	{
		_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;
		MinidumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
		MinidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
		MinidumpExceptionInformation.ClientPointers = TRUE;

		// Writes user-mode minidump information to the specified file.
		MiniDumpWriteDump(GetCurrentProcess(),
			GetCurrentProcessId(),
			hDumpFile,
			MiniDumpWithFullMemory,
			&MinidumpExceptionInformation,   // If the value of this parameter is NULL, no exception information is included in the minidump file.
			NULL,  // If the value of this parameter is NULL, no user-defined information is included in the minidump file.
			NULL); // If the value of this parameter is NULL, no callbacks are performed.

		CloseHandle(hDumpFile);

		wprintf(L"CrashDump Save Finished!\n");
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void CCrashDump::SetHandlerDump()
{
	SetUnhandledExceptionFilter(MyExceptionFilter);
}

// Invalid Parameter handler
void CCrashDump::myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
{
	Crash();
}

int CCrashDump::_custom_Report_hook(int ireposttype, char* message, int* returnvalue)
{
	Crash();
	return true;
}

void CCrashDump::myPurecallHandler(void)
{
	Crash();
}