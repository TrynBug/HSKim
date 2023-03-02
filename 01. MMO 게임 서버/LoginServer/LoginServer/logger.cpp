
#include <iostream>
#include <time.h>
#include <Windows.h>
#include <string>
#include <strsafe.h>
#include <unordered_map>
#include <vector>

#include "logger.h"

#define min(a, b)  (((a) < (b)) ? (a) : (b)) 
#define max(a, b)  (((a) > (b)) ? (a) : (b)) 


// extern 초기화
CLogger logger::ex_Logger;


CLogger::CLogger()
{

	_logNum = 0;
	_filePointer.QuadPart = 0;
	InitializeSRWLock(&_srwlVecLoggerTLS);

	// TLS index
	_tlsIndex = TlsAlloc();
	if (_tlsIndex == TLS_OUT_OF_INDEXES)
	{
		wprintf(L"CLogger::CLogger() !! TlsAlloc failed !! error:%u\n", GetLastError());
		return;
	}

	// 파일명 설정
	struct tm curTime;
	time_t longTime;
	time(&longTime);
	localtime_s(&curTime, &longTime);
	swprintf_s(_szLogFileName, LOGGING_FILE_LENGTH, L"log_%04d%02d%02d_%02d%02d%02d.txt"
		, curTime.tm_year, curTime.tm_mon + 1, curTime.tm_mday, curTime.tm_hour, curTime.tm_min, curTime.tm_sec);

	// logging level
	_consoleLoggingLevel = LOGGING_LEVEL_DEBUG;  // console out level
	_fileLoggingLevel = LOGGING_LEVEL_WARN;      // file out level

	// 콘솔 로깅 제한 레벨
	_bUseConsoleLoggingLimit = false;
	_consoleLoggingLimitLevel = LOGGING_LEVEL_ERROR;  // _bUseConsoleLoggingLimit == true && _limitedConsoleLoggingCount == 0 일 때 이 레벨 아래로는 콘솔에 출력되지 않는다. (파일에는 출력됨)
	_consoleLoggingLimitCount = 0;
	memset(_arrUnprintedLogCount, 0, sizeof(_arrUnprintedLogCount));
}


CLogger::~CLogger()
{
	// close file
	AcquireSRWLockExclusive(&_srwlVecLoggerTLS);
	for (auto& pLoggerTLS : _vecLoggerTLS)
	{
		delete pLoggerTLS;
	}
	ReleaseSRWLockExclusive(&_srwlVecLoggerTLS);

	TlsFree(_tlsIndex);
}


/* setter */
void CLogger::SetConsoleLoggingLevel(int loggingLevel)
{
	_consoleLoggingLevel = max(min(loggingLevel, LOGGING_LEVEL_NONE), LOGGING_LEVEL_DEBUG);
}

void CLogger::SetFileLoggingLevel(int loggingLevel)
{
	_fileLoggingLevel = max(min(loggingLevel, LOGGING_LEVEL_NONE), LOGGING_LEVEL_DEBUG);
}

void CLogger::SetConsoleLoggingLimitLevel(int loggingLevel)
{
	_consoleLoggingLimitLevel = max(min(loggingLevel, LOGGING_LEVEL_NONE), LOGGING_LEVEL_DEBUG);
}

void CLogger::SetConsoleLoggingLimitCount(unsigned int count)
{
	_consoleLoggingLimitCount = count;
	_bUseConsoleLoggingLimit = true;

	int numOfUnprintedLog = 0;
	for (int i = 0; i < sizeof(_arrUnprintedLogCount) / sizeof(_arrUnprintedLogCount[0]); i++)
		numOfUnprintedLog += _arrUnprintedLogCount[i];
	if (numOfUnprintedLog > 0)
	{
		std::wstring strOut;
		strOut.reserve(150);
		strOut += L"there are " + std::to_wstring(numOfUnprintedLog) + L" (";
		if (_arrUnprintedLogCount[0] > 0)
			strOut += L"DEBUG " + std::to_wstring(_arrUnprintedLogCount[0]) + L", ";
		if (_arrUnprintedLogCount[1] > 0)
			strOut += L"INFO " + std::to_wstring(_arrUnprintedLogCount[1]) + L", ";
		if (_arrUnprintedLogCount[2] > 0)
			strOut += L"WARN " + std::to_wstring(_arrUnprintedLogCount[2]) + L", ";
		if (_arrUnprintedLogCount[3] > 0)
			strOut += L"ERROR " + std::to_wstring(_arrUnprintedLogCount[3]) + L", ";
		if (_arrUnprintedLogCount[4] > 0)
			strOut += L"FATAL " + std::to_wstring(_arrUnprintedLogCount[4]);
		strOut += L") logs that were not printed due to the console logging limitation. check the log file.\n";

		wprintf(strOut.c_str());
		memset(_arrUnprintedLogCount, 0, sizeof(_arrUnprintedLogCount));
	}

}


/* logging :: 가변인자 로그 기록
szFormat 문자열 포맷으로 로그를 파일에 write한다. */
void CLogger::Log(int logLevel, const wchar_t* szFormat, ...)
{
	// 버퍼에 로그 write하기
	va_list vaList;
	va_start(vaList, szFormat);
	Log(logLevel, szFormat, vaList);
	va_end(vaList);

}

void CLogger::Log(int logLevel, const wchar_t* szFormat, va_list vaList)
{
	if (logLevel < _consoleLoggingLevel && logLevel < _fileLoggingLevel)
		return;

	// TLS get
	_CLoggerTLS* pLoggerTLS = (_CLoggerTLS*)TlsGetValue(_tlsIndex);
	if (pLoggerTLS == nullptr)
	{
		pLoggerTLS = new _CLoggerTLS(this);
		TlsSetValue(_tlsIndex, pLoggerTLS);

		AcquireSRWLockExclusive(&_srwlVecLoggerTLS);
		_vecLoggerTLS.push_back(pLoggerTLS);
		ReleaseSRWLockExclusive(&_srwlVecLoggerTLS);
	}

	// 버퍼에 로그번호, 시간, 로깅레벨 write하기
	int lenHead = WriteHeadLog(logLevel, pLoggerTLS->_szLog, LOGGING_NUM_OF_MAX_CHAR);

	// 버퍼에 로그 write하기
	HRESULT hr = StringCchVPrintf(pLoggerTLS->_szLog + lenHead, LOGGING_NUM_OF_MAX_CHAR - lenHead, szFormat, vaList);

	// 콘솔에 로그 출력
	if (logLevel >= _consoleLoggingLevel)
	{
		if (_bUseConsoleLoggingLimit == false)
		{
			wprintf(pLoggerTLS->_szLog);
		}
		else
		{
			// 콘솔 로그 출력 횟수 제한을 사용중일 경우, 현재 로그레벨이 제한레벨보다 낮고 제한카운트가 0이라면 콘솔에 출력하지 않는다.
			if (logLevel < _consoleLoggingLimitLevel)
			{
				if (_consoleLoggingLimitCount > 0)
				{
					InterlockedDecrement(&_consoleLoggingLimitCount);
					wprintf(pLoggerTLS->_szLog);
				}
				else
				{
					InterlockedIncrement(&_arrUnprintedLogCount[logLevel - LOGGING_LEVEL_DEBUG]);
				}
			}
		}
	}

	// 파일에 로그 출력
	int byteToWrite;
	LARGE_INTEGER filePointer;
	if (logLevel >= _fileLoggingLevel && pLoggerTLS->_hLogfile != INVALID_HANDLE_VALUE)
	{
		// 로그문자열 바이트수, write할 파일포인터 위치를 얻는다.
		byteToWrite = (int)wcslen(pLoggerTLS->_szLog) * 2;
		filePointer.QuadPart = InterlockedAdd64(&_filePointer.QuadPart, byteToWrite) - byteToWrite;

		// 현재 파일의 파일 포인터 위치를 write할 위치로 옮긴다.
		if (pLoggerTLS->_prevFilePointer.QuadPart != filePointer.QuadPart)
		{
			if (SetFilePointerEx(pLoggerTLS->_hLogfile, filePointer, NULL, FILE_BEGIN) == FALSE)
			{
				wprintf(L"CLogger::Log SetFilePointerEx failed !!, error:%u, byteToWrite:%d\n", GetLastError(), byteToWrite);
			}
		}
		pLoggerTLS->_prevFilePointer.QuadPart += byteToWrite;

		// 파일에 write
		if (WriteFile(pLoggerTLS->_hLogfile, pLoggerTLS->_szLog, byteToWrite, NULL, NULL) == FALSE)
		{
			wprintf(L"CLogger::Log WriteFile failed !!, error:%u, byteToWrite:%d\n", GetLastError(), byteToWrite);
		}
		
	}


	// strsafe 오류 발생 시 오류 로그 기록
	if (FAILED(hr))
	{
		int lenHead = WriteHeadLog(LOGGING_LEVEL_WARN, pLoggerTLS->_szLog, LOGGING_NUM_OF_MAX_CHAR);
		if (hr == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			swprintf_s(pLoggerTLS->_szLog + lenHead, LOGGING_NUM_OF_MAX_CHAR - lenHead, L"\nstrsafe function error... %d : the log is too long!!\n", hr);
		}
		else
		{
			swprintf_s(pLoggerTLS->_szLog + lenHead, LOGGING_NUM_OF_MAX_CHAR - lenHead, L"strsafe function error... %d\n", hr);
		}

		// 콘솔에 로그 출력
		if (LOGGING_LEVEL_WARN >= _consoleLoggingLevel)
		{
			wprintf(pLoggerTLS->_szLog);
		}
		// 파일에 로그 출력
		if (logLevel >= _fileLoggingLevel && pLoggerTLS->_hLogfile != INVALID_HANDLE_VALUE)
		{
			// 로그문자열 바이트수, 로그번호, write할 파일포인터 위치를 얻는다.
			byteToWrite = (int)wcslen(pLoggerTLS->_szLog) * 2;
			filePointer.QuadPart = InterlockedAdd64(&_filePointer.QuadPart, byteToWrite) - byteToWrite;

			// 현재 파일의 파일 포인터 위치를 write할 위치로 옮긴다.
			// 이 작업때문에 속도가 느려지는것같음.. 이유는 모름
			if (SetFilePointerEx(pLoggerTLS->_hLogfile, filePointer, NULL, FILE_BEGIN) == FALSE)
			{
				wprintf(L"CLogger::Log SetFilePointerEx failed !!, error:%u, byteToWrite:%d\n", GetLastError(), byteToWrite);
			}

			// 파일에 write
			if (WriteFile(pLoggerTLS->_hLogfile, pLoggerTLS->_szLog, byteToWrite, NULL, NULL) == FALSE)
			{
				wprintf(L"CLogger::Log WriteFile failed !!, error:%u, byteToWrite:%d\n", GetLastError(), byteToWrite);
			}
		}
	}

}





/* write head log
로그 번호, 현재 시간, 로깅 레벨을 szLog 에 write한다.
write한 문자 수를 return한다.
WriteHeadLog 함수를 호출한 뒤에 _szLog를 사용할 때는 _szLog + return_value 위치부터 write해야 한다. */
int CLogger::WriteHeadLog(int logLevel, wchar_t* szLog, int bufLen)
{
	SYSTEMTIME systime;
	GetLocalTime(&systime);

	__int64 logNum = InterlockedIncrement64(&_logNum) - 1;
	int numChar = swprintf_s(szLog, bufLen, L"%lld [%04d-%02d-%02d %02d:%02d:%02d.%03d]"
		, logNum, systime.wYear, systime.wMonth, systime.wDay, systime.wHour, systime.wMinute, systime.wSecond, systime.wMilliseconds);

	switch (logLevel)
	{
	case LOGGING_LEVEL_DEBUG:
		memcpy(szLog + numChar, L" DEBUG  ", 18);
		numChar += 8;
		break;
	case LOGGING_LEVEL_INFO:
		memcpy(szLog + numChar, L" INFO   ", 18);
		numChar += 8;
		break;
	case LOGGING_LEVEL_WARN:
		memcpy(szLog + numChar, L" WARN   ", 18);
		numChar += 8;
		break;
	case LOGGING_LEVEL_ERROR:
		memcpy(szLog + numChar, L" ERROR  ", 18);
		numChar += 8;
		break;
	case LOGGING_LEVEL_FATAL:
		memcpy(szLog + numChar, L" FATAL  ", 18);
		numChar += 8;
		break;
	}

	return numChar;

}












_CLoggerTLS::_CLoggerTLS(CLogger* pLogger)
{
	_pLogger = pLogger;
	_prevFilePointer.QuadPart = 0;

	// 로그 문자열 저장 버퍼 생성
	_szLog = new wchar_t[LOGGING_NUM_OF_MAX_CHAR];

	// 파일 open
	_hLogfile = CreateFile(_pLogger->_szLogFileName
		, GENERIC_READ | GENERIC_WRITE
		, FILE_SHARE_READ | FILE_SHARE_WRITE
		, NULL
		, OPEN_ALWAYS
		, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH  // FILE_FLAG_NO_BUFFERING 플래그는 적용시 SetFilePointerEx 함수로 파일 포인터를 옮길 때 sector-aligned 위치로만 옮길수 있다고함. 그래서안씀
		, NULL);
	if (_hLogfile == INVALID_HANDLE_VALUE)
	{
		wchar_t szHeadLog[100];
		pLogger->WriteHeadLog(LOGGING_LEVEL_FATAL, szHeadLog, 100);
		wprintf(L"%sCan not open the log file !!! filename: %s, error:%d\n", szHeadLog, _pLogger->_szLogFileName, GetLastError());
	}
}


_CLoggerTLS::~_CLoggerTLS()
{
	CloseHandle(_hLogfile);
	_hLogfile = INVALID_HANDLE_VALUE;
	delete _szLog;
}
