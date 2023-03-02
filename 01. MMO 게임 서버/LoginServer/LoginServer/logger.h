#pragma once

#include <Windows.h>
#include <vector>

#define LOGGING_LEVEL_DEBUG 10
#define LOGGING_LEVEL_INFO  11
#define LOGGING_LEVEL_WARN  12
#define LOGGING_LEVEL_ERROR 13
#define LOGGING_LEVEL_FATAL 14
#define LOGGING_LEVEL_NONE  15

#define LOGGING_FILE_LENGTH 25
#define LOGGING_NUM_OF_MAX_CHAR 10000

// LOGGING 매크로. Log 함수를 호출하기 전 logLevel을 확인하여 실제로 출력되는 로그만 Log 함수가 호출되도록 한다.
// 실제 출력되지 않는 로그에 대해 함수 호출 비용, 인자 전달 비용을 줄일 수 있다.
#define LOGGING(logLevel, szFormat, ...) do { \
	if (logLevel >= logger::ex_Logger.GetConsoleLoggingLevel() || logLevel >= logger::ex_Logger.GetFileLoggingLevel()) \
		logger::ex_Logger.Log(logLevel, szFormat, ##__VA_ARGS__); \
} while(0) 

#define LOGGING_VALIST(logLevel, szFormat, vaList) do { \
	if (logLevel >= logger::ex_Logger.GetConsoleLoggingLevel() || logLevel >= logger::ex_Logger.GetFileLoggingLevel()) \
		logger::ex_Logger.Log(logLevel, szFormat, vaList); \
} while(0) 


class CLogger;
namespace logger
{
	extern CLogger ex_Logger;
}



class CLogger
{
	friend class _CLoggerTLS;

public:
	CLogger();
	~CLogger();

private:
	LARGE_INTEGER _filePointer;                   // 현재 파일 포인터 위치
	wchar_t _szLogFileName[LOGGING_FILE_LENGTH];  // 파일명
	int _consoleLoggingLevel;                     // 콘솔 로깅 레벨
	int _fileLoggingLevel;                        // 파일 로깅 레벨

	DWORD _tlsIndex;                              // TLS index
	std::vector<_CLoggerTLS*> _vecLoggerTLS;      // 생성된 스레드 로깅 객체 저장을 위한 벡터
	SRWLOCK _srwlVecLoggerTLS;                    // 벡터 전용 lock

	// 콘솔 로깅 제한
	bool _bUseConsoleLoggingLimit;                // 콘솔 로깅 제한 사용여부. SetConsoleLoggingLimitLevel 함수를 호출하면 true로 설정됨.
	long _consoleLoggingLimitCount;               // 콘솔 로깅 제한 카운트. 콘솔에 출력할 횟수를 제한하고 싶다면 이 값을 설정한다. 파일에는 정상적으로 출력된다. 
												  // 이 값은 _limitedConsoleLoggingLevel 레벨 아래의 로그를 콘솔에 출력할 때 마다 -1 된다.
												  // 프레임당 출력 횟수 제한 용도로 사용하고 싶다면 매 프레임마다 이 값을 리셋해주어야 한다.
	int _consoleLoggingLimitLevel;                // _bUseConsoleLoggingLimit == true && _limitedConsoleLoggingCount == 0 일 때 이 레벨 아래로는 콘솔에 출력하지 않는다. (파일에는 출력됨)
	long _arrUnprintedLogCount[6];                 // 콘솔 로깅 제한으로 인해 출력되지 않은 로그 횟수 카운트

	__int64 _logNum;                              // 로그 번호

public:
	/* getter */
	int GetConsoleLoggingLevel() { return _consoleLoggingLevel; }
	int GetFileLoggingLevel() { return _fileLoggingLevel; }

	/* setter */
	void SetConsoleLoggingLevel(int loggingLevel);
	void SetFileLoggingLevel(int loggingLevel);
	void SetConsoleLoggingLimitLevel(int loggingLevel);
	void SetConsoleLoggingLimitCount(unsigned int count);

public:
	/* logging */
	// Log 함수를 직접 사용하지 말고 LOGGING 매크로를 사용하도록 하자.
	void Log(int logLevel, const wchar_t* szFormat, ...);
	void Log(int logLevel, const wchar_t* szFormat, va_list vaList);

private:
	/* write head log */
	inline int WriteHeadLog(int logLevel, wchar_t* buffer, int bufLen);

};


class _CLoggerTLS
{
	friend class CLogger;

	HANDLE _hLogfile;    // 파일 핸들
	wchar_t* _szLog;     // 로그 문자열
	LARGE_INTEGER _prevFilePointer;     // 이전 파일 포인터 위치

	CLogger* _pLogger;   // 전역 CLogger 클래스

	_CLoggerTLS(CLogger* pLogger);
	~_CLoggerTLS();
};
