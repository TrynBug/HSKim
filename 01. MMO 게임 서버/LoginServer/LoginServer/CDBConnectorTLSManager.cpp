#include <Windows.h>
#include <vector>

#include "CDBConnectorTLSManager.h"

CDBConnectorTLSManager::CDBConnectorTLSManager(const char* host, const char* user, const char* password, const char* database, unsigned int port)
	:_queryRunTimeLimit(20000)
{
	/* config */
	strcpy_s(_szHost, 100, host);
	strcpy_s(_szUser, 100, user);
	strcpy_s(_szPassword, 100, password);
	strcpy_s(_szDatabase, 100, database);
	_port = port;

	// TLS index √ ±‚»≠
	_tlsIndexDBConnector = TlsAlloc();
	if (_tlsIndexDBConnector == TLS_OUT_OF_INDEXES)
	{
		wprintf(L"CDBConnectorTLSManager TlsAlloc failed!!! error:%u\n", GetLastError());
		int* p = 0;
		*p = 0;
	}

	InitializeSRWLock(&_srwlConnect);
	InitializeSRWLock(&_srwlVecDBConnector);
}


CDBConnectorTLSManager::~CDBConnectorTLSManager()
{
	AcquireSRWLockExclusive(&_srwlVecDBConnector);
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		delete _vecDBConnector[i];
	}
	ReleaseSRWLockExclusive(&_srwlVecDBConnector);

	// TLS free
	TlsFree(_tlsIndexDBConnector);
}





CDBConnector* CDBConnectorTLSManager::GetTlsDBConnector()
{
	CDBConnector* pDBConn = (CDBConnector*)TlsGetValue(_tlsIndexDBConnector);
	if (pDBConn == nullptr)
	{
		pDBConn = new CDBConnector();
		AcquireSRWLockExclusive(&_srwlConnect);
		pDBConn->Connect(_szHost, _szUser, _szPassword, _szDatabase, _port);
		ReleaseSRWLockExclusive(&_srwlConnect);
		TlsSetValue(_tlsIndexDBConnector, pDBConn);
		AcquireSRWLockExclusive(&_srwlVecDBConnector);
		_vecDBConnector.push_back(pDBConn);
		ReleaseSRWLockExclusive(&_srwlVecDBConnector);
	}

	return pDBConn;
}



/* set */
void CDBConnectorTLSManager::SetQueryRunTimeLimit(int queryRunTimeLimit)
{
	_queryRunTimeLimit = queryRunTimeLimit;
	AcquireSRWLockShared(&_srwlVecDBConnector);
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		_vecDBConnector[i]->SetQueryRunTimeLimit(_queryRunTimeLimit);
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);

}



__int64 CDBConnectorTLSManager::GetQueryRunCount()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	__int64 queryRunCount = 0;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		queryRunCount += _vecDBConnector[i]->GetQueryRunCount();
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);
	return queryRunCount;
}

float CDBConnectorTLSManager::GetMaxQueryRunTime()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	float maxQueryRunTime = 0.f;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		float first = _vecDBConnector[i]->GetMaxQueryRunTime();
		float second = _vecDBConnector[i]->Get2ndMaxQueryRunTime();
		if (first > maxQueryRunTime)
		{
			maxQueryRunTime = first;
		}
		else if (second > maxQueryRunTime)
		{
			maxQueryRunTime = second;
		}
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);
	return maxQueryRunTime;
}

float CDBConnectorTLSManager::Get2thMaxQueryRunTime()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	float maxQueryRunTime = 0.f;
	float max2ndQueryRunTime = 0.f;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		float first = _vecDBConnector[i]->GetMaxQueryRunTime();
		float second = _vecDBConnector[i]->Get2ndMaxQueryRunTime();
		if (first > maxQueryRunTime)
		{
			max2ndQueryRunTime = maxQueryRunTime;
			maxQueryRunTime = first;
			if (second > max2ndQueryRunTime)
				max2ndQueryRunTime = second;
		}
		else if (second > maxQueryRunTime)
		{
			max2ndQueryRunTime = maxQueryRunTime;
			maxQueryRunTime = second;
		}
		else if (second > max2ndQueryRunTime)
		{
			max2ndQueryRunTime = second;
		}
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);
	return max2ndQueryRunTime;
}

float CDBConnectorTLSManager::GetMinQueryRunTime()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	float minQueryRunTime = FLT_MAX;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		float first = _vecDBConnector[i]->GetMinQueryRunTime();
		float second = _vecDBConnector[i]->Get2ndMinQueryRunTime();
		if (first < minQueryRunTime)
		{
			minQueryRunTime = first;
		}
		else if (second < minQueryRunTime)
		{
			minQueryRunTime = second;
		}
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);
	return minQueryRunTime;
}

float CDBConnectorTLSManager::Get2thMinQueryRunTime()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	float minQueryRunTime = 0.f;
	float min2ndQueryRunTime = 0.f;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		float first = _vecDBConnector[i]->GetMinQueryRunTime();
		float second = _vecDBConnector[i]->Get2ndMinQueryRunTime();
		if (first < minQueryRunTime)
		{
			min2ndQueryRunTime = minQueryRunTime;
			minQueryRunTime = first;
			if (second < min2ndQueryRunTime)
				min2ndQueryRunTime = second;
		}
		else if (second < minQueryRunTime)
		{
			min2ndQueryRunTime = minQueryRunTime;
			minQueryRunTime = second;
		}
		else if (second > min2ndQueryRunTime)
		{
			min2ndQueryRunTime = second;
		}
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);
	return min2ndQueryRunTime;
}

float CDBConnectorTLSManager::GetAvgQueryRunTime()
{
	AcquireSRWLockShared(&_srwlVecDBConnector);
	float totalQueryRunTime = 0.f;
	__int64 totalQueryRunCount = 0;
	__int64 queryRunCount;
	for (int i = 0; i < _vecDBConnector.size(); i++)
	{
		queryRunCount = _vecDBConnector[i]->GetQueryRunCount();
		totalQueryRunTime += (float)queryRunCount * _vecDBConnector[i]->GetAvgQueryRunTime();
		totalQueryRunCount += queryRunCount;
	}
	ReleaseSRWLockShared(&_srwlVecDBConnector);

	if (totalQueryRunCount == 0)
		return 0.f;
	else
		return totalQueryRunTime / (float)totalQueryRunCount;
}
