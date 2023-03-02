#pragma once

#include "mysql/mysql.h"
#include "mysql/errmsg.h"

#define MAX_QUERY_LENGTH 10000

class CDBConnector
{
private:
	/* config */
	char _szHost[100];
	char _szUser[100];
	char _szPassword[100];
	char _szDatabase[100];
	unsigned int _port;
	bool _bEnableMultiStatements;

	/* MySQL */
	MYSQL  _mysqlConn;
	MYSQL* _pMysqlConn;

	/* Query */
	wchar_t* _query_utf16;
	char* _query_utf8;

	/* error */
	unsigned int _mysqlErrorNo;
	const char* _mysqlError;
	int _queryRunTimeLimit;  // ms 단위. 쿼리 실행이 이 시간 이상 걸린다면 오류 로그를 남긴다.
	wchar_t* _mysqlErrorBuffer;  // _mysqlError 문자열을 utf16으로 변환하여 저장할 버퍼

	/* monitor */
	__int64 _queryRunCount;      // 쿼리 실행 횟수
	float _maxQueryRunTime[2];   // 최대 쿼리 실행시간. ms 단위. [0] 제일큰값, [1] 두번째로큰값
	float _minQueryRunTime[2];   // 최소 쿼리 실행시간. ms 단위. [0] 제일작은값, [1] 두번째로작은값
	float _avgQueryRunTime;      // 평균 쿼리 실행시간. ms 단위.
	double _accQueryRunTime;     // 누적 쿼리 실행시간

	LARGE_INTEGER _liCounterFrequency;  


public:
	CDBConnector();
	~CDBConnector();

public:
	/* set */
	void SetQueryRunTimeLimit(int queryRunTimeLimit) { _queryRunTimeLimit = queryRunTimeLimit; }

	/* connect */
	bool Connect(const char* host, const char* user, const char* password, const char* database, unsigned int port);
	bool Connect(const char* host, const char* user, const char* password, const char* database, unsigned int port, bool bEnableMultiStatements);
	void Disconnect();

	/* query */
	int ExecuteQuery(const wchar_t* query, ...);     // 반환값이 없는 쿼리를 실행한다. 성공 시 쿼리가 적용된 row 수를 리턴한다(0~max). 실패 시 -1을 리턴한다.
	MYSQL_RES* ExecuteQueryAndGetResult(const wchar_t* query, ...);  // 반환값이 있는 쿼리를 실행한다. 성공 시 쿼리 결과를 담고있는 포인터를 리턴한다. 실패시 nullptr을 리턴한다.
	
	/* get query result */
	uint64_t GetNumQueryRows(MYSQL_RES* res);
	MYSQL_ROW FetchRow(MYSQL_RES* res);
	void FreeResult(MYSQL_RES* mysqlRes);
	unsigned int GetMysqlErrorNo() { return _mysqlErrorNo; }
	const wchar_t* GetMysqlError() { return _mysqlErrorBuffer; }

	/* get monitor */
	__int64 GetQueryRunCount() { return _queryRunCount; }
	float GetMaxQueryRunTime() { return _maxQueryRunTime[0]; }
	float Get2ndMaxQueryRunTime() { return _maxQueryRunTime[1]; }
	float GetMinQueryRunTime() { return _minQueryRunTime[0]; }
	float Get2ndMinQueryRunTime() { return _minQueryRunTime[1]; }
	float GetAvgQueryRunTime() { return _avgQueryRunTime; }

private:
	/* error */
	void StoreMysqlError();  // _mysqlError에 mysql_error 함수 반환값을 저장하고, _mysqlErrorBuffer 에 utf16으로 변환된 오류문자열을 저장한다.

	/* monitor */
	void UpdateMonitor(float queryRunTime);

};

