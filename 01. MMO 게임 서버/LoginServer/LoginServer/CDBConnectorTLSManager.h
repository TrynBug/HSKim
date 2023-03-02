#pragma once

#include "CDBConnector.h"

class CDBConnectorTLSManager
{
	/* config */
	char _szHost[100];
	char _szUser[100];
	char _szPassword[100];
	char _szDatabase[100];
	unsigned int _port;

	/* DB Connector */
	DWORD _tlsIndexDBConnector;
	std::vector<CDBConnector*> _vecDBConnector;
	SRWLOCK _srwlConnect;   // CDBConnector 의 Connect 함수의 lock
	SRWLOCK _srwlVecDBConnector;

	/* option */
	int _queryRunTimeLimit;  // ms 단위. 쿼리 실행이 이 시간 이상 걸린다면 오류 로그를 남긴다.

public:
	CDBConnectorTLSManager(const char* host, const char* user, const char* password, const char* database, unsigned int port);
	~CDBConnectorTLSManager();

public:
	/* Get DBConnector*/
	CDBConnector* GetTlsDBConnector();

	/* set */
	void SetQueryRunTimeLimit(int queryRunTimeLimit);

	/* Get */
	int GetNumConnection() { return (int)_vecDBConnector.size(); }
	__int64 GetQueryRunCount();
	float GetMaxQueryRunTime();
	float Get2thMaxQueryRunTime();
	float GetMinQueryRunTime();
	float Get2thMinQueryRunTime();
	float GetAvgQueryRunTime();

};



