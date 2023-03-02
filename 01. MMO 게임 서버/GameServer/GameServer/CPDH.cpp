#pragma comment(lib, "pdh.lib")

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <strsafe.h>

#include "CPDH.h"




CPDH::CPDH()
    : _dwFlagsQueryEntry(0xFFFFFFFF)
    , _szProcessName { 0, }, _pdh_Status(0), _pdh_Query(0)
    , _pdhQueryResult{ 0, }, _arrPdhNetworkQueryResult(nullptr), _numNetworkInterface(0), _pdhCount{ 0, }
{
}

CPDH::CPDH(DWORD dwFlagsQueryEntry)
    : _dwFlagsQueryEntry(dwFlagsQueryEntry)
    , _szProcessName{ 0, }, _pdh_Status(0), _pdh_Query(0)
    , _pdhQueryResult{ 0, }, _arrPdhNetworkQueryResult(nullptr), _numNetworkInterface(0), _pdhCount{ 0, }
{
}

CPDH::~CPDH()
{

}



bool CPDH::Init()
{
    // Create a query.
    _pdh_Status = PdhOpenQuery(NULL, NULL, &_pdh_Query);
    if (_pdh_Status != ERROR_SUCCESS)
    {
        wprintf(L"[PDH] PdhOpenQuery failed with status 0x%x.\n", _pdh_Status);
        if (_pdh_Query)
        {
            PdhCloseQuery(_pdh_Query);
        }
        return false;
    }


    /* 네트워크 카운트값 얻는 쿼리 등록하기 */
    WCHAR* szCounters = NULL;
    WCHAR* szInterfaces = NULL;
    DWORD dwCounterSize = 0;
    DWORD dwInterfaceSize = 0;

    // PdhEnumObjectItems 를 통해서 "Network Interface" 항목에서 얻을 수 있는 측정항목(Counters), 인터페이스 항목(Interfaces)을 얻음.
    // 그런데 갯수나 길이를 모르기 때문에 먼저 버퍼의 길이를 알기 위해서 Out Buffer 인자들을 NULL 포인터로 넣어서 사이즈만 확인함.
    // 그리고 모든 이더넷의 이름이 나오지만 실제 사용중인 이더넷인지 가상이더넷인지 등등을 구분할수없음.
    PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0);

    // 버퍼의 동적할당 후 다시 호출!
    // szCounters 와 szInterfaces 버퍼에는 여러개의 문자열이 쭉쭉쭉 들어온다.
    // 2차원 배열도 아니고, 그냥 NULL 포인터로 끝나는 문자열들이 dwCounterSize, dwInterfaceSize 길이만큼 줄줄이 들어있음.
    // 이를 문자열 단위로 끊어서 개수를 확인해야함. aaa\0bbb\0ccc\0ddd 이딴 식
    szCounters = new WCHAR[dwCounterSize];      // 512
    szInterfaces = new WCHAR[dwInterfaceSize];  // 36
    if (PdhEnumObjectItems(NULL, NULL, L"Network Interface", szCounters, &dwCounterSize, szInterfaces, &dwInterfaceSize, PERF_DETAIL_WIZARD, 0) != ERROR_SUCCESS)
    {
        PdhCloseQuery(_pdh_Query);
        delete[] szCounters;
        delete[] szInterfaces;
        szInterfaces = nullptr;
        szCounters = nullptr;
        return false;
    }


    // 네트워크 인터페이스 수를 찾고, 수만큼 네트워크 count 구조체를 만든다.
    _numNetworkInterface = 0;
    for (unsigned int i = 0; i < dwInterfaceSize - 1; i++)
    {
        if (*(szInterfaces + i) == L'\0')
            _numNetworkInterface++;
    }
    _arrPdhNetworkQueryResult = new _PDHNetworkQueryResult[_numNetworkInterface];

    // 네트워크 카운트값 얻는 쿼리 등록
    // Bytes Received/sec는 프레이밍 문자를 포함하여 각 네트워크 어댑터에서 받는 바이트의 비율입니다. Network Interface\\Bytes Received/sec은 Network Interface\\Bytes Total/sec의 하위 집합입니다.
    // Bytes Sent/sec 는 프레이밍 문자를 포함하여 네트워크 어댑터에서 보낸 바이트의 비율입니다. Network Interface\\Bytes Sent/sec은 Network Interface\\Bytes Total/sec의 하위 집합입니다.
    WCHAR* szCur = szInterfaces;
    WCHAR szQuery[1024] = { 0, };
    int idx = 0;
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_NETWORK)
    {
        for (unsigned int i = 0; i < dwInterfaceSize - 1; i++)
        {
            if (*(szInterfaces + i) == L'\0')
            {
                // szInterfaces 를 문자열 단위로 끊어서 네트워크 count 구조체에 복사한다.
                _arrPdhNetworkQueryResult[idx].szName[0] = L'\0';
                wcscpy_s(_arrPdhNetworkQueryResult[idx].szName, szCur);

                // 쿼리 등록
                szQuery[0] = L'\0';
                StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Received/sec", szCur);  // "\Network Interface(Realtek PCIe GbE Family Controller)\Bytes Received/sec"
                PdhAddCounter(_pdh_Query, szQuery, NULL, &_arrPdhNetworkQueryResult[idx].networkRecvBytes);  // 받는 데이터 바이트수 쿼리 등록. 마지막 인자는 Handle to the counter that was added to the query 이다.

                szQuery[0] = L'\0';
                StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Network Interface(%s)\\Bytes Sent/sec", szCur);  // "\Network Interface(Realtek PCIe GbE Family Controller)\Bytes Sent/sec"
                PdhAddCounter(_pdh_Query, szQuery, NULL, &_arrPdhNetworkQueryResult[idx].networkSendBytes);  // 보내는 데이터 바이트수 쿼리 등록

                szCur = szInterfaces + i + 1;
                idx++;
            }
        }
    }


    /* TCP segment, 재전송 쿼리 등록하기 */
    // Segments Sent/sec는 다시 전송되는 바이트만 들어있는 세그먼트는 제외하고, 현재 연결에 있는 세그먼트를 포함하여 세그먼트를 보내는 비율입니다.
    // Segments Retransmitted/sec는 세그먼트를 다시 전송하는 비율입니다. 즉 이전에 전송된 바이트가 하나 이상 포함된 세그먼트를 전송하는 것입니다.
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_NETWORK)
    {
        PdhAddCounter(_pdh_Query, L"\\TCPv4\\Segments Sent/sec", NULL, &_pdhQueryResult.TCPSegmentsSent);
        PdhAddCounter(_pdh_Query, L"\\TCPv4\\Segments Retransmitted/sec", NULL, &_pdhQueryResult.TCPSegmentsRetransmitted);
    }

    /* 시스템 메모리 카운트값 얻는 쿼리 등록하기 */
    // Available Bytes는 컴퓨터에서 실행되는 프로세스에 할당하거나 시스템에서 사용할 수 있는 실제 메모리의 양(바이트)입니다. 이것은 대기 중이거나 비어 있거나 0으로 채워진 페이지 목록에 할당된 메모리의 총계입니다.
    // Committed Bytes는 커밋된 가상 메모리의 양(바이트)입니다. 커밋된 메모리는 디스크 페이징 파일을 디스크에 다시 써야 할 필요가 있을 경우를 위해 예약된 실제 메모리입니다. 각 실제 드라이브에 하나 이상의 페이징 파일이 있을 수 있습니다. 이 카운터는 최근에 관찰된 값만 표시하며 평균값은 아닙니다.
    // Pool Nonpaged Bytes는 비페이징 풀의 크기(바이트)이고, 비페이징 풀은 디스크에 쓸 수 없지만 할당되어 있는 동안 실제 메모리에 남아 있어야 하는 개체에 사용되는 시스템 가상 메모리 영역입니다. Memory\\Pool Nonpaged Bytes는 Process\\Pool Nonpaged Bytes와 다르게 계산되므로 Process(_Total)\\Pool Nonpaged Bytes과 같지 않을 수 있습니다. 이 카운터는 최근에 관찰된 값만 표시하며 평균값은 아닙니다.
    // Pool Paged Bytes는 페이징 풀의 크기(바이트)이고, 페이징 풀은 사용되지 않고 있을 때 디스크에 쓸 수 있는 개체에 사용되는 시스템 가상 메모리 영역입니다. Memory\\Pool Paged Bytes는 Process\\Pool Paged Bytes와 다르게 계산되므로 Process(_Total)\\Pool Paged Bytes와 같지 않을 수 있습니다. 이 카운터는 최근에 관찰된 값만 표시하며 평균값은 아닙니다.
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_SYSTEM_MEMORY)
    {
        PdhAddCounter(_pdh_Query, L"\\Memory\\Available Bytes", NULL, &_pdhQueryResult.systemAvailableBytes);
        PdhAddCounter(_pdh_Query, L"\\Memory\\Committed Bytes", NULL, &_pdhQueryResult.systemComittedBytes);
        PdhAddCounter(_pdh_Query, L"\\Memory\\Pool Nonpaged Bytes", NULL, &_pdhQueryResult.systemNonpagedPoolBytes);
        PdhAddCounter(_pdh_Query, L"\\Memory\\Pool Paged Bytes", NULL, &_pdhQueryResult.systemPagedPoolBytes);
    }

    /* 프로세스 메모리 카운트값 얻는 쿼리 등록하기 */
    // 프로세스 이름 얻기(.exe제외)
    WCHAR szExePath[MAX_PATH];
    int lenPath = GetModuleFileNameW(nullptr, szExePath, MAX_PATH);  // 프로세스 전체 경로 얻기(lenPath는 null 제외한 문자열길이)
    int locDot = lenPath;  // 마지막 dot 위치
    for (int i = lenPath; i >= 0; i--)
    {
        if (szExePath[i] == L'.' && locDot == lenPath)  // 마지막 dot 위치 얻기
            locDot = i;
        if (szExePath[i] == L'\\')  // 마지막 \ 위치에 도달했으면 \위치+1 부터 마지막dot위치까지 memcpy 한다.
        {
            memcpy(_szProcessName, szExePath + i + 1, (locDot - i - 1) * 2);
            szExePath[locDot - i - 1] = L'\0';
            break;
        }
    }

    // 프로세스 메모리 카운트값 얻는 쿼리 등록하기
    // Page Faults/sec는 이 프로세스에서 실행 중인 스레드에서 발생하는 페이지 폴트 오류 비율입니다. 페이지 폴트는 프로세스가 주 메모리의 작업 집합에 없는 가상 메모리 페이지를 참조할 때 발생합니다. 이것은 페이지가 실행 대기 목록에 있지 않고 이미 주 메모리에 있거나 페이지를 공유하는 다른 프로세스에서 사용되고 있으면 디스크에서 페이지를 가져오지 않습니다.
    // Pool Nonpaged Bytes는 비페이징 풀의 크기(바이트)이고, 비페이징 풀은 디스크에 쓸 수 없지만 할당되어 있는 동안 실제 메모리에 남아 있어야 하는 개체에 사용되는 시스템 가상 메모리 영역입니다. Memory\\Pool Nonpaged Bytes는 Process\\Pool Nonpaged Bytes와 다르게 계산되므로 Process(_Total)\\Pool Nonpaged Bytes과 같지 않을 수 있습니다. 이 카운터는 최근에 관찰된 값만 표시하며 평균값은 아닙니다.
    // Pool Paged Bytes는 페이징 풀의 크기(바이트)이고, 페이징 풀은 사용되지 않고 있을 때 디스크에 쓸 수 있는 개체에 사용되는 시스템 가상 메모리 영역입니다. Memory\\Pool Paged Bytes는 Process\\Pool Paged Bytes와 다르게 계산되므로 Process(_Total)\\Pool Paged Bytes와 같지 않을 수 있습니다. 이 카운터는 최근에 관찰된 값만 표시하며 평균값은 아닙니다.
    // Private Bytes는 이 프로세스가 할당하여 다른 프로세스와는 공유할 수 없는 메모리의 현재 크기(바이트)입니다.
    // Working Set는 이 프로세스에 대한 작업 집합의 현재 크기(바이트)입니다. 작업 집합은 프로세스의 스레드가 최근에 작업한 메모리 페이지의 집합입니다. 컴퓨터에 있는 빈 메모리가 한계를 초과하면 페이지는 사용 중이 아니라도 프로세스의 작업 집합에 남아 있습니다. 빈 메모리가 한계 미만이면 페이지는 작업 집합에서 지워집니다. 이 페이지가 필요하면 주 메모리에서 없어지기 전에 소프트 오류 처리되어 다시 작업 집합에 있게 됩니다.
    // Working Set - Private는 다른 프로세서가 공유하거나 공유할 수 있는 작업 집합이 아니라 이 프로세서만 사용하고 있는 작업 집합의 크기(바이트)입니다.
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_PROCESS_MEMORY)
    {
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Page Faults/sec", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processPageFaultRate);
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Pool Nonpaged Bytes", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processNonpagedPoolBytes);
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Pool Paged Bytes", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processPagedPoolBytes);
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Private Bytes", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processPrivateBytes);
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Working Set", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processWorkingSet);
        StringCbPrintf(szQuery, sizeof(WCHAR) * 1024, L"\\Process(%s)\\Working Set - Private", _szProcessName);
        PdhAddCounter(_pdh_Query, szQuery, NULL, &_pdhQueryResult.processPrivateWorkingSet);
    }


    /* 쿼리 최초 실행 */
    // Most counters require two sample values to display a formatted value. PDH stores the current sample value and the previously collected sample value. 
    // This call retrieves the first value that will be used by PdhGetFormattedCounterValue in the first iteration of the loop.
    // Note that this value is lost if the counter does not require two values to compute a displayable value.
    _pdh_Status = PdhCollectQueryData(_pdh_Query);
    if (_pdh_Status != ERROR_SUCCESS)
    {
        wprintf(L"[PDH] PdhCollectQueryData failed with 0x%x.\n", _pdh_Status);
        PdhCloseQuery(_pdh_Query);
        delete[] szCounters;
        delete[] szInterfaces;
        delete[] _arrPdhNetworkQueryResult;
        szInterfaces = nullptr;
        szCounters = nullptr;
        _arrPdhNetworkQueryResult = nullptr;
        return false;
    }

    return true;
}


bool CPDH::Update()
{
    _pdh_Status = PdhCollectQueryData(_pdh_Query);
    if (_pdh_Status != ERROR_SUCCESS)
    {
        wprintf(L"[PDH] PdhCollectQueryData failed with status 0x%x.\n", _pdh_Status);
        return false;
    }

    /* 네트워크 카운트 합계를 계산한 다음 저장*/
    PDH_FMT_COUNTERVALUE counterValue;
    _pdhCount.networkRecvBytes = 0.0;
    _pdhCount.networkSendBytes = 0.0;
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_NETWORK)
    {
        for (int i = 0; i < _numNetworkInterface; i++)
        {
            _pdh_Status = PdhGetFormattedCounterValue(_arrPdhNetworkQueryResult[i].networkRecvBytes, PDH_FMT_DOUBLE, NULL, &counterValue);
            if (_pdh_Status == 0)
                _pdhCount.networkRecvBytes += counterValue.doubleValue;
            else
                wprintf(L"[PDH] PdhGetFormattedCounterValue 1 failed with status 0x%x.\n", _pdh_Status);

            _pdh_Status = PdhGetFormattedCounterValue(_arrPdhNetworkQueryResult[i].networkSendBytes, PDH_FMT_DOUBLE, NULL, &counterValue);
            if (_pdh_Status == 0)
                _pdhCount.networkSendBytes += counterValue.doubleValue;
            else
                wprintf(L"[PDH] PdhGetFormattedCounterValue 2 failed with status 0x%x.\n", _pdh_Status);
        }
    }

    /* 시스템, 프로세스 카운트 저장 */
#define _PDH_STORE_QUERY_RESULT_DOUBLE(counter) \
    _pdh_Status = PdhGetFormattedCounterValue(_pdhQueryResult.counter, PDH_FMT_DOUBLE, NULL, &counterValue); \
    if (_pdh_Status == 0) \
        _pdhCount.counter = counterValue.doubleValue; \
    else \
        wprintf(L"[PDH] PdhGetFormattedCounterValue %s failed with status 0x%x.\n", L#counter, _pdh_Status);

#define _PDH_STORE_QUERY_RESULT_INT64(counter) \
    _pdh_Status = PdhGetFormattedCounterValue(_pdhQueryResult.counter, PDH_FMT_LARGE, NULL, &counterValue); \
    if (_pdh_Status == 0) \
        _pdhCount.counter = counterValue.largeValue; \
    else \
        wprintf(L"[PDH] PdhGetFormattedCounterValue %s failed with status 0x%x.\n", L#counter, _pdh_Status);

    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_NETWORK) 
    {
        _PDH_STORE_QUERY_RESULT_INT64(TCPSegmentsSent);
        _PDH_STORE_QUERY_RESULT_INT64(TCPSegmentsRetransmitted);
    }
    
    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_SYSTEM_MEMORY)
    {
        _PDH_STORE_QUERY_RESULT_INT64(systemAvailableBytes);
        _PDH_STORE_QUERY_RESULT_INT64(systemComittedBytes);
        _PDH_STORE_QUERY_RESULT_INT64(systemNonpagedPoolBytes);
        _PDH_STORE_QUERY_RESULT_INT64(systemPagedPoolBytes);
    }

    if (_dwFlagsQueryEntry & PDH_FLAG_QUERY_ENTRY_PROCESS_MEMORY)
    {
        _PDH_STORE_QUERY_RESULT_DOUBLE(processPageFaultRate);
        _PDH_STORE_QUERY_RESULT_INT64(processNonpagedPoolBytes);
        _PDH_STORE_QUERY_RESULT_INT64(processPagedPoolBytes);
        _PDH_STORE_QUERY_RESULT_INT64(processPrivateBytes);
        _PDH_STORE_QUERY_RESULT_INT64(processWorkingSet);
        _PDH_STORE_QUERY_RESULT_INT64(processPrivateWorkingSet);
    }


    return true;
}


