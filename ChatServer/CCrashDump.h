#pragma once

class CCrashDump
{
private:
	static long _DumpCount;

private:
	CCrashDump() {}
	~CCrashDump() {}

public:

	static void Init();
	static void Crash();

	// 예외 발생 시 dump 파일을 생성하기 전 수행할 작업을 등록한다.
	static void AddFinalJob(void (*func)(void*), void* param);

	// 예외 발생 시 dump 파일 생성
	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer);
	
	static void SetHandlerDump();
	// Invalid Parameter handler
	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved);
	static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue);
	static void myPurecallHandler(void);

};

