#pragma once

#include "CDBConnector.h"
#include "CMemoryPool.h"
#include "CLockfreeQueue.h"

class alignas(64) CDBAsyncWriter
{
private:
	struct _StQueryRequest
	{
		int msgType;
		wchar_t* szQuery;
		_StQueryRequest() 
			: msgType(0) 
		{
			szQuery = new wchar_t[MAX_QUERY_LENGTH]; 
		}
		~_StQueryRequest() { delete[] szQuery; }
	};

private:
	/* DB */
	CDBConnector* _pDBConn;

	/* thread */
	HANDLE _hThreadDBWriter;
	unsigned int _threadDBWriterId;
	HANDLE _hEventMsg;
	bool _isClosed;

	/* message */
	CMemoryPool<_StQueryRequest> _poolQueryRequest;
	CLockfreeQueue<_StQueryRequest*> _msgQ;

public:
	CDBAsyncWriter();
	~CDBAsyncWriter();

public:
	/* connect and run thread */
	bool ConnectAndRunThread(const char* host, const char* user, const char* password, const char* database, unsigned int port);

	/* query */
	bool PostQueryRequest(const wchar_t* query, ...);  // 반환값이 없는 쿼리를 요청한다.

	/* close */
	void Close();

	/* 모니터링 */
	int GetUnprocessedQueryCount() { return _msgQ.Size(); }
	__int64 GetQueryRunCount() { return _pDBConn->GetQueryRunCount(); }
	float GetMaxQueryRunTime() { return _pDBConn->GetMaxQueryRunTime(); }
	float Get2ndMaxQueryRunTime() { return _pDBConn->Get2ndMaxQueryRunTime(); }
	float GetMinQueryRunTime() { return _pDBConn->GetMinQueryRunTime(); }
	float Get2ndMinQueryRunTime() { return _pDBConn->Get2ndMinQueryRunTime(); }
	float GetAvgQueryRunTime() { return _pDBConn->GetAvgQueryRunTime(); }
	int GetQueryRequestPoolSize() { return _poolQueryRequest.GetPoolSize(); }
	int GetQueryRequestAllocCount() { return _poolQueryRequest.GetUseCount(); }
	int GetQueryRequestFreeCount() { return _poolQueryRequest.GetFreeCount(); }

	/* dynamic alloc */
	// 64byte aligned 객체 생성을 위한 new, delete overriding
	void* operator new(size_t size);
	void operator delete(void* p);

	void Crash();

private:
	/* 비동기 DB Writer 스레드 */
	static unsigned WINAPI ThreadAsyncDBWriter(PVOID pParam);

	/* msg type */
	enum {
		MSG_QUERY_REQUEST,
		MSG_SHUTDOWN
	};
};

