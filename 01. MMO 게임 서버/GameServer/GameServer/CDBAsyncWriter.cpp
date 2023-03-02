
#include <Windows.h>
#include <process.h>
#include <strsafe.h>
#include "logger.h"

#include "CDBAsyncWriter.h"





CDBAsyncWriter::CDBAsyncWriter()
	: _pDBConn(nullptr), _hThreadDBWriter(0), _threadDBWriterId(0), _hEventMsg(0), _isClosed(false)
	, _poolQueryRequest(0, true, false)
{

}

CDBAsyncWriter::~CDBAsyncWriter()
{
	if (_isClosed == false)
		Close();
}


/* connect and run thread */
bool CDBAsyncWriter::ConnectAndRunThread(const char* host, const char* user, const char* password, const char* database, unsigned int port)
{
	if (_pDBConn != nullptr)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Async DB Writer Thread already has a DB connection\n");
		return false;
	}

	// event 객체 생성
	_hEventMsg = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (_hEventMsg == NULL)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Failed to create event object. error:%u\n", GetLastError());
		return false;
	}

	// DB Connector 생성
	_pDBConn = new CDBConnector();
	if (_pDBConn->Connect(host, user, password, database, port, true) == false)
	{
		delete _pDBConn;
		_pDBConn = nullptr;
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Failed to connect to DB\n");
		return false;
	}

	// 스레드 시작
	_hThreadDBWriter = (HANDLE)_beginthreadex(NULL, 0, ThreadAsyncDBWriter, (PVOID)this, 0, &_threadDBWriterId);
	if (_hThreadDBWriter == 0)
	{
		delete _pDBConn;
		_pDBConn = nullptr;
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Failed to start Async DB Writer Thread. error:%u\n", GetLastError());
		return false;
	}

	return true;
}



/* query */
// 반환값이 없는 쿼리를 요청한다. 스레드의 메시지큐에 쿼리를 전달한다.
bool CDBAsyncWriter::PostQueryRequest(const wchar_t* query, ...)
{
	_StQueryRequest* pQueryRequest = _poolQueryRequest.Alloc();
	pQueryRequest->msgType = MSG_QUERY_REQUEST;

	va_list vaList;
	va_start(vaList, query);
	// QueryRequest 버퍼에 쿼리 write
	HRESULT hr = StringCchVPrintf(pQueryRequest->szQuery, MAX_QUERY_LENGTH, query, vaList);
	if (FAILED(hr))
	{
		if (hr == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] query length exceeds buffer. \n\tquery: %s\n", query);
		}
		else
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB] an error occurred within the StringCchVPrinf function. \n\tquery format:%s\n", query);
		}

		_poolQueryRequest.Free(pQueryRequest);
		return false;
	}
	va_end(vaList);

	// 메시지큐에 쿼리 전달
	_msgQ.Enqueue(pQueryRequest);
	SetEvent(_hEventMsg);
	return true;
}


/* Disconnect */
void CDBAsyncWriter::Close()
{
	if (_isClosed == true)
		return;

	// 스레드 종료 메시지를 보낸다.
	_StQueryRequest* pMsg = _poolQueryRequest.Alloc();
	pMsg->msgType = MSG_SHUTDOWN;
	_msgQ.Enqueue(pMsg);
	SetEvent(_hEventMsg);

	// 스레드가 종료되기를 기다린다.
	BOOL retWait = WaitForSingleObject(_hThreadDBWriter, INFINITE);
	if (retWait != WAIT_OBJECT_0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] An error occurred when terminate DB Writer Thread. error:%u\n", GetLastError());
	}

	// 메시지큐를 비운다.
	_StQueryRequest* pQueryRequest;
	int unprocessedMsgCount = 0;
	while (_msgQ.Dequeue(pQueryRequest))
	{
		_poolQueryRequest.Free(pQueryRequest);
		unprocessedMsgCount++;
	}
	if (unprocessedMsgCount > 0)
	{
		LOGGING(LOGGING_LEVEL_ERROR, L"[DB] %d query requests were unprocessed\n", unprocessedMsgCount);
	}

	// DB 연결을 끊는다.
	delete _pDBConn;
	_pDBConn = nullptr;

	_isClosed = true;
	return;
}


/* dynamic alloc */
// 64byte aligned 객체 생성을 위한 new, delete overriding
void* CDBAsyncWriter::operator new(size_t size)
{
	return _aligned_malloc(size, 64);
}

void CDBAsyncWriter::operator delete(void* p)
{
	_aligned_free(p);
}

void CDBAsyncWriter::Crash()
{
	int* p = 0;
	*p = 0;
}

/* (static) 비동기 DB Writer 스레드 */
unsigned WINAPI CDBAsyncWriter::ThreadAsyncDBWriter(PVOID pParam)
{
	wprintf(L"[DB Thread] begin async DB writer thread\n");
	CDBAsyncWriter& db = *(CDBAsyncWriter*)pParam;

	_StQueryRequest* pQueryRequest;
	while (true)
	{
		// 메시지 삽입 이벤트를 기다린다.
		DWORD retWait = WaitForSingleObject(db._hEventMsg, INFINITE);
		if (retWait != WAIT_OBJECT_0 && retWait != WAIT_IO_COMPLETION)
		{
			LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Wait for event failed!!, error:%d\n", GetLastError());
			return 0;
		}

		// 메시지큐에 있는 모든 쿼리를 처리한다.
		while (db._msgQ.Dequeue(pQueryRequest))
		{
			switch (pQueryRequest->msgType)
			{
			case MSG_QUERY_REQUEST:
			{
				if (db._pDBConn->ExecuteQuery(pQueryRequest->szQuery) == -1)
					db.Crash();
				break;
			}
			case MSG_SHUTDOWN:
			{
				db._poolQueryRequest.Free(pQueryRequest);
				return 0;
			}
			default:
			{
				LOGGING(LOGGING_LEVEL_ERROR, L"[DB Thread] Invalid message type. type:%d\n", pQueryRequest->msgType);
				break;
			}
			}
			db._poolQueryRequest.Free(pQueryRequest);
		}
	}
	return 0;

}