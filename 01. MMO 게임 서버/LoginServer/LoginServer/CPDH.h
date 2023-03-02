#pragma once

#include <pdh.h>
#include <pdhmsg.h>

#define PDH_FLAG_QUERY_ENTRY_ALL               0xFFFFFFFF
#define PDH_FLAG_QUERY_ENTRY_NETWORK           0x00000001
#define PDH_FLAG_QUERY_ENTRY_SYSTEM_MEMORY     0x00000002
#define PDH_FLAG_QUERY_ENTRY_PROCESS_MEMORY    0x00000004


struct _PDHQueryResult
{
	PDH_HCOUNTER   TCPSegmentsSent;
	PDH_HCOUNTER   TCPSegmentsRetransmitted;
	PDH_HCOUNTER   systemAvailableBytes;
	PDH_HCOUNTER   systemComittedBytes;
	PDH_HCOUNTER   systemNonpagedPoolBytes;
	PDH_HCOUNTER   systemPagedPoolBytes;
	PDH_HCOUNTER   processPageFaultRate;
	PDH_HCOUNTER   processNonpagedPoolBytes;
	PDH_HCOUNTER   processPagedPoolBytes;
	PDH_HCOUNTER   processPrivateBytes;
	PDH_HCOUNTER   processWorkingSet;
	PDH_HCOUNTER   processPrivateWorkingSet;
};


struct _PDHNetworkQueryResult
{
	WCHAR          szName[128];
	PDH_HCOUNTER   networkRecvBytes;
	PDH_HCOUNTER   networkSendBytes;
};

struct PDHCount
{
	__int64 TCPSegmentsSent;
	__int64 TCPSegmentsRetransmitted;
	__int64 systemAvailableBytes;
	__int64 systemComittedBytes;
	__int64 systemNonpagedPoolBytes;
	__int64 systemPagedPoolBytes;
	double processPageFaultRate;
	__int64 processNonpagedPoolBytes;
	__int64 processPagedPoolBytes;
	__int64 processPrivateBytes;
	__int64 processWorkingSet;
	__int64 processPrivateWorkingSet;
	double networkRecvBytes;
	double networkSendBytes;
};


class CPDH
{
private:
	DWORD _dwFlagsQueryEntry;         // 쿼리 항목 활성/비활성 flag

	WCHAR _szProcessName[MAX_PATH];  // 프로세스명(.exe제외)

	PDH_STATUS _pdh_Status;
	HQUERY _pdh_Query;

	_PDHQueryResult _pdhQueryResult;
	_PDHNetworkQueryResult* _arrPdhNetworkQueryResult;
	int _numNetworkInterface;

	PDHCount _pdhCount;

public:
	CPDH();
	CPDH(DWORD dwFlagsQueryEntry);
	~CPDH();

	const PDHCount& GetPDHCount() { return _pdhCount; }

public:
	bool Init();

	bool Update();
};

