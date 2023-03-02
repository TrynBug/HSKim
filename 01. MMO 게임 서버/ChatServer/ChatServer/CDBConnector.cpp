#pragma comment(lib, "libmysql.lib")
#include <iostream>
#include <Windows.h>
#include <strsafe.h>

#include "CDBConnector.h"
#include "logger.h"


CDBConnector::CDBConnector()
	: _szHost{ 0, }, _szUser{ 0, }, _szPassword{ 0, }, _szDatabase{ 0, }, _port(0), _bEnableMultiStatements(false)
	, _mysqlConn(), _pMysqlConn(nullptr), _mysqlErrorNo(0), _mysqlError(nullptr)
	, _maxQueryRunTime{ 0, }, _minQueryRunTime{ 0, }, _avgQueryRunTime(0.f), _accQueryRunTime(0.0)
{
	_query_utf16 = new wchar_t[MAX_QUERY_LENGTH];
	_query_utf8 = new char[MAX_QUERY_LENGTH];
	_mysqlErrorBuffer = new wchar_t[MAX_QUERY_LENGTH];

	_queryRunTimeLimit = 20000;

	_minQueryRunTime[0] = FLT_MAX;
	_minQueryRunTime[1] = FLT_MAX;
	QueryPerformanceFrequency(&_liCounterFrequency);
}


CDBConnector::~CDBConnector()
{
	if(_pMysqlConn != nullptr)
		mysql_close(_pMysqlConn);
	_pMysqlConn = nullptr;
	delete[] _query_utf16;
	delete[] _query_utf8;
	delete[] _mysqlErrorBuffer;
}


bool CDBConnector::Connect(const char* host, const char* user, const char* password, const char* database, unsigned int port)
{
	return Connect(host, user, password, database, port, false);
}


bool CDBConnector::Connect(const char* host, const char* user, const char* password, const char* database, unsigned int port, bool bEnableMultiStatements)
{
	strcpy_s(_szHost, 100, host);
	strcpy_s(_szUser, 100, user);
	strcpy_s(_szPassword, 100, password);
	strcpy_s(_szDatabase, 100, database);
	_port = port;
	_bEnableMultiStatements = bEnableMultiStatements;

	// DB 초기화
	mysql_init(&_mysqlConn);

	// DB 연결
	if (bEnableMultiStatements)
	{
		_pMysqlConn = mysql_real_connect(&_mysqlConn, host, user, password, database, port, (char*)NULL, CLIENT_MULTI_STATEMENTS);
	}
	else
	{
		_pMysqlConn = mysql_real_connect(&_mysqlConn, host, user, password, database, port, (char*)NULL, 0);
	}

	if (_pMysqlConn == NULL)
	{
		StoreMysqlError();
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] Mysql connection error:%s\n", _mysqlErrorBuffer);
		return false;
	}

	// auto reconnect 옵션 enable
	bool optReconnect = 0;
	mysql_options(_pMysqlConn, MYSQL_OPT_RECONNECT, &optReconnect);

	return true;
}


void CDBConnector::Disconnect()
{
	if (_pMysqlConn != nullptr)
		mysql_close(_pMysqlConn);
	_pMysqlConn = nullptr;
}


/* query */
// 반환값이 없는 쿼리를 실행한다. 성공 시 쿼리가 적용된 row 수를 리턴한다(0~max). 실패 시 -1을 리턴한다.
int CDBConnector::ExecuteQuery(const wchar_t* query, ...)
{
	va_list vaList;
	va_start(vaList, query);

	// 버퍼에 쿼리 write
	HRESULT hr = StringCchVPrintf(_query_utf16, MAX_QUERY_LENGTH, query, vaList);
	if (FAILED(hr))
	{
		if (hr == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] query length exceeds buffer. \n\tquery: %s\n", _query_utf16);
		}
		else
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred within the StringCchVPrinf function. \n\tquery format:%s\n", query);
		}

		return -1;
	}
	va_end(vaList);

	// utf16 쿼리를 utf8로 변환
	int retConversion = WideCharToMultiByte(CP_UTF8, 0, _query_utf16, -1, _query_utf8, MAX_QUERY_LENGTH, NULL, NULL);
	if (retConversion == 0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] failed to convert query utf-16 to utf-8. error:%u \n\tquery format:%s \n\tquery utf16:%s\n"
			, GetLastError(), query, _query_utf16);
		return -1;
	}

	// 쿼리 실행
	LARGE_INTEGER liStart;
	LARGE_INTEGER liEnd;
	QueryPerformanceCounter(&liStart);
	int queryState = mysql_query(_pMysqlConn, _query_utf8);
	QueryPerformanceCounter(&liEnd);
	if (queryState != 0)
	{
		// 쿼리 error
		StoreMysqlError();
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred while executing the query. error:%s \n\tquery format:%s \n\tquery utf16:%s\n"
			, _mysqlErrorBuffer, query, _query_utf16);

		return -1;
	}

	// 쿼리 실행시간 계산 및 모니터링 업데이트
	float queryRunTime = (float)((liEnd.QuadPart - liStart.QuadPart) * 1000) / (float)_liCounterFrequency.QuadPart;
	if ((int)queryRunTime > _queryRunTimeLimit)
	{
		// 쿼리 실행이 너무 오래 걸렸음. 오류로그 기록
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] query execution took too long time. time:%f \n\tquery utf16:%s\n"
			, queryRunTime, _query_utf16);
	}
	UpdateMonitor(queryRunTime);


	// 쿼리가 적용된 row 수를 얻는다.
	int numAffectedRows = 0;
	if (_bEnableMultiStatements)
	{
		// 쿼리가 1개 이상일 경우
		numAffectedRows = (int)mysql_affected_rows(_pMysqlConn);  // 첫 mysql_query 호출에 대한 결과 얻기
		while (true)
		{
			queryState = mysql_next_result(_pMysqlConn);
			if (queryState == 0)
			{
				numAffectedRows += (int)mysql_affected_rows(_pMysqlConn);  // 첫번째 이후의 쿼리에 대한 결과 얻기
			}
			else if (queryState == -1)   // mysql_next_result 반환값이 -1 이면 더이상 결과가 없는것
			{
				break;
			}
			else
			{
				StoreMysqlError();
				LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred while executing the query. error:%s \n\tquery format:%s \n\tquery utf16:%s\n"
					, _mysqlErrorBuffer, query, _query_utf16);
				return -1;
			}
		}
	}
	else
	{
		numAffectedRows = (int)mysql_affected_rows(_pMysqlConn);
	}

	return numAffectedRows;
}

// 반환값이 있는 쿼리를 실행한다. 성공 시 쿼리 결과를 담고있는 포인터를 리턴한다. 실패시 nullptr을 리턴한다.
MYSQL_RES* CDBConnector::ExecuteQueryAndGetResult(const wchar_t* query, ...)
{
	va_list vaList;
	va_start(vaList, query);

	// 버퍼에 쿼리 write
	HRESULT hr = StringCchVPrintf(_query_utf16, MAX_QUERY_LENGTH, query, vaList);
	if (FAILED(hr))
	{
		if (hr == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] query length exceeds buffer. \n\tquery:%s\n", _query_utf16);
		}
		else
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred within the StringCchVPrinf function. \n\tquery format:%s\n", query);
		}

		return nullptr;
	}
	va_end(vaList);

	// utf16 쿼리를 utf8로 변환
	int retConversion = WideCharToMultiByte(CP_UTF8, 0, _query_utf16, -1, _query_utf8, MAX_QUERY_LENGTH, NULL, NULL);
	if (retConversion == 0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] failed to convert query utf-16 to utf-8. error:%u \n\tquery format:%s \n\tquery utf16:%s\n"
			, GetLastError(), query, _query_utf16);
		return nullptr;
	}

	// 쿼리 실행
	LARGE_INTEGER liStart;
	LARGE_INTEGER liEnd;
	QueryPerformanceCounter(&liStart);
	int queryState = mysql_query(_pMysqlConn, _query_utf8);
	QueryPerformanceCounter(&liEnd);
	if (queryState != 0)
	{
		// 쿼리 error
		StoreMysqlError();
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred while executing the query. error:%s \n\tquery format:%s \n\tquery utf16:%s\n"
			, _mysqlErrorBuffer, query, _query_utf16);
		return nullptr;
	}

	// 쿼리 실행시간 계산 및 모니터링 업데이트
	float queryRunTime = (float)((liEnd.QuadPart - liStart.QuadPart) * 1000) / (float)_liCounterFrequency.QuadPart;
	if ((int)queryRunTime > _queryRunTimeLimit)
	{
		// 쿼리 실행이 너무 오래 걸렸음. 오류로그 기록
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] query execution took too long time. time:%.2fms \n\tquery utf16:%s \n\tquery utf8:%s\n"
			, queryRunTime, _query_utf16, _query_utf8);
	}
	UpdateMonitor(queryRunTime);

	// 쿼리 결과를 얻는다.
	MYSQL_RES* myRes = mysql_store_result(_pMysqlConn);
	if (myRes == nullptr)
	{
		// 쿼리 결과를 얻지 못했음. 원래 결과가 없는 쿼리인지 아니면 결과를 얻지 못한것인지 확인함.
		if (mysql_field_count(_pMysqlConn) == 0)
		{
			// 원래 결과가 없는 쿼리임.
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] ExecuteQueryAndGetResult function expected to get a result, but the query wasn't with a result. \n\tquery:%s\n"
				, _query_utf16);
		}
		else
		{
			// mysql_store_result 함수가 데이터를 리턴해야 했으나 그러지 못했음. error
			StoreMysqlError();
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] Mysql should have returned a result, but it didn't. error:%s \n\tquery format:%s \n\tquery utf16:%s\n"
				, _mysqlErrorBuffer, query, _query_utf16);

		}
		return nullptr;
	}

	

	return myRes;
}


void CDBConnector::FreeResult(MYSQL_RES* mysqlRes)
{
	if (mysqlRes != nullptr)
		mysql_free_result(mysqlRes);

}










/* get query result */
uint64_t CDBConnector::GetNumQueryRows(MYSQL_RES* res)
{
	if (res == nullptr)
		return 0;

	return mysql_num_rows(res);
}

MYSQL_ROW CDBConnector::FetchRow(MYSQL_RES* res)
{
	return mysql_fetch_row(res);
}





/* error */
// _mysqlError에 mysql_error 함수 반환값을 저장하고, _mysqlErrorBuffer 에 utf16으로 변환된 오류문자열을 저장한다.
void CDBConnector::StoreMysqlError()
{
	_mysqlErrorNo = mysql_errno(_pMysqlConn);
	_mysqlError = mysql_error(_pMysqlConn);
	MultiByteToWideChar(CP_UTF8, 0, _mysqlError, -1, _mysqlErrorBuffer, MAX_QUERY_LENGTH);
}


/* monitor */
void CDBConnector::UpdateMonitor(float queryRunTime)
{
	if (queryRunTime > _maxQueryRunTime[0])
	{
		_maxQueryRunTime[1] = _maxQueryRunTime[0];
		_maxQueryRunTime[0] = queryRunTime;
	}
	else if (queryRunTime > _maxQueryRunTime[1])
	{
		_maxQueryRunTime[1] = queryRunTime;
	}

	if (queryRunTime < _minQueryRunTime[0])
	{
		_minQueryRunTime[1] = _minQueryRunTime[0];
		_minQueryRunTime[0] = queryRunTime;
	}
	else if (queryRunTime < _minQueryRunTime[1])
	{
		_minQueryRunTime[1] = queryRunTime;
	}

	_queryRunCount++;
	_accQueryRunTime += (double)queryRunTime;
	_avgQueryRunTime = (float)(_accQueryRunTime / (double)_queryRunCount);
}






