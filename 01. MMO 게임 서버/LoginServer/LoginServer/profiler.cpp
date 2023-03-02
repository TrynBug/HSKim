#include <iostream>
#include <string>
#include <sstream>
#include <windows.h>
#include <unordered_map>
#include <list>


using std::make_pair;
using std::wostringstream;
using std::wstring;


#include "profiler.h"


// 전역 프로파일 매니저 객체 생성
CProfileManager g_profileManager;


/* 콘솔 어플리케이션인 경우에는 console control handler 를 등록하여
사용자가 콘솔창 종료 시 프로파일링 결과가 파일로 출력될 수 있도록 해야함 */
BOOL CtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		g_profileManager.ProfileDataOutText(L"");
		break;
	}
	return true;
}


// 프로파일링 데이터 초기화
void profiler::ResetProfilingData()
{
	g_profileManager.ProfileReset();
}

// 프로파일링 데이터 파일로 출력
void profiler::OutputProfilingData()
{
	g_profileManager.ProfileDataOutText(L"");
}



CProfiler::CProfiler()
	: _tag(nullptr), _isEnd(false)
{
}

CProfiler::CProfiler(const wchar_t* tag)
	: _tag(tag), _isEnd(false)
{
	Begin();
}


CProfiler::~CProfiler()
{
	if (_isEnd == false)  // PROFILE_END 를 호출한다음 소멸자가 한번더 호출되는것을 방지함
		End(_tag);
}

void CProfiler::Begin(const wchar_t* tag)
{
	_tag = tag;
	Begin();
}

// profiling 시작
void CProfiler::Begin()
{
	LARGE_INTEGER liProfileTime;
	QueryPerformanceCounter(&liProfileTime);

	// TLS에서 내 스레드의 프로파일 매니저를 가져온다.
	PVOID pTlsValue = TlsGetValue(g_profileManager._tlsIndex);
	
	if (pTlsValue == nullptr)
	{
		// 스레드 프로파일 매니저가 없다면 생성한다.
		pTlsValue = new CThreadProfileManager;
		TlsSetValue(g_profileManager._tlsIndex, pTlsValue);

		// 프로파일 매니저에 스레드 프로파일 매니저를 등록한다.
		AcquireSRWLockExclusive(&g_profileManager._srwlThreadProfileManager);
		g_profileManager._vecThreadProfileManager.push_back((CThreadProfileManager*)pTlsValue);
		ReleaseSRWLockExclusive(&g_profileManager._srwlThreadProfileManager);
	}
	CThreadProfileManager& mgr = *(CThreadProfileManager*)pTlsValue;

	
	// reset 플래그가 세팅되었을 경우 reset한다.
	if (mgr._bNeedReset == true)
		mgr.Reset();


	// tag가 mapProfileSample에 있는지 검색한다.
	auto iter = mgr._mapProfileSample.find(_tag);

	// 같은 tag가 없다면 프로파일 데이터를 생성한다.
	if (iter == mgr._mapProfileSample.end())
	{
		ProfileSample* pProfileSample = new ProfileSample(_tag);
		mgr._mapProfileSample.insert(make_pair(_tag, pProfileSample));
		iter = mgr._mapProfileSample.find(_tag);
	}
	ProfileSample* pSample = iter->second;


	// 현재 프로파일 샘플을 프로파일 계층 벡터에 넣는다. 
	mgr._vecProfileHierarchy.push_back(&(iter->second->iTotalTime));


	// 프로파일링 시작 시간 기록
	QueryPerformanceCounter(&(pSample->liStartTime));
	// 프로파일링 자체에 소요된 시간 계산
	mgr._iTotalProfileCall++;
	__int64 profileTime = pSample->liStartTime.QuadPart - liProfileTime.QuadPart;
	mgr._iTotalProfileTime += profileTime;
	// 계층벡터 내에서 자신을 제외한 모든 샘플의 소요시간에서 프로파일링 자체 소요시간을 뺀다.
	for (int i = 0; i < (int)mgr._vecProfileHierarchy.size() - 1; i++)
		*(mgr._vecProfileHierarchy[i]) -= profileTime;

}


// profiling 종료
void CProfiler::End(const wchar_t* tag)
{
	// 종료 시간을 구한다.
	LARGE_INTEGER liEndTime;
	QueryPerformanceCounter(&liEndTime);

	// 만약 tag != _tag 일 경우, 문자열을 비교해보고 같다면 ok, 다르다면 error.
	if (tag != _tag)
	{
		if (wcscmp(tag, _tag) != 0)
		{
			int* p = nullptr;
			*p = 0;
		}
	}

	// 같은 프로파일러에서 End가 2번 연속 호출된 경우 error
	if (_isEnd == true)
	{
		int* p = nullptr;
		*p = 0;
	}


	// TLS에서 스레드 프로파일 매니저를 가져온다.
	CThreadProfileManager& mgr = *(CThreadProfileManager*)TlsGetValue(g_profileManager._tlsIndex);

	// 소요시간을 계산한다.
	auto iter = mgr._mapProfileSample.find(_tag);

	ProfileSample* pSample = iter->second;
	__int64 timeDelta = liEndTime.QuadPart - pSample->liStartTime.QuadPart;
	pSample->iTotalTime += timeDelta;
	pSample->iCall++;


	// min, max 교체
	if (timeDelta < pSample->iMin[0])
	{
		pSample->iMin[1] = pSample->iMin[0];
		pSample->iMin[0] = timeDelta;
	}
	else if (timeDelta < pSample->iMin[1])
	{
		pSample->iMin[1] = timeDelta;
	}

	if (timeDelta > pSample->iMax[0])
	{
		pSample->iMax[1] = pSample->iMax[0];
		pSample->iMax[0] = timeDelta;
	}
	else if (timeDelta > pSample->iMax[1])
	{
		pSample->iMax[1] = timeDelta;
	}

	// 종료 플래그 설정
	_isEnd = true;

	// 현재 프로파일 샘플을 프로파일 계층 벡터에서 제거한다.
	mgr._vecProfileHierarchy.pop_back();

	// 프로파일링 자체에 소요된 시간 계산
	LARGE_INTEGER liProfileTime;
	QueryPerformanceCounter(&liProfileTime);
	__int64 profileTime = liProfileTime.QuadPart - liEndTime.QuadPart;
	mgr._iTotalProfileTime += profileTime;

	// 계층벡터 내에서 자신을 제외한 모든 샘플의 소요시간에서 프로파일링 자체 소요시간을 뺀다.
	for (int i = 0; i < (int)mgr._vecProfileHierarchy.size(); i++)
		*(mgr._vecProfileHierarchy[i]) -= profileTime;

}








CProfileManager::CProfileManager()
{
	_tlsIndex = TlsAlloc();
	if (_tlsIndex == TLS_OUT_OF_INDEXES)
	{
		printf("TlsAlloc failed. return value:%u, error:%u\n", _tlsIndex, GetLastError());
	}

	InitializeSRWLock(&_srwlThreadProfileManager);
}


CProfileManager::~CProfileManager()
{
	if(g_profileManager._bProfileStarted)
		ProfileDataOutText(L"");
}


// profiling 데이터를 Text 파일로 출력한다.
// Parameters: (const wchar_t*) 출력될 파일 이름
void CProfileManager::ProfileDataOutText(const wchar_t* strFilePath)
{
	// 전체 프로파일링 데이터를 리스트에 복사
	// 복사하는 중간에 ProfileSample 구조체 값이 변경될 수 있지만, 그정도 오차는 무시하기로 함.
	// 그리고 복사하는 중간에 CThreadProfileManager 의 map에 추가로 insert가 될수 있는데 그거때문에 오류나지는 않을듯
	std::list<ProfileSample> listProfileSample;
	__int64 totalProfileCall = 0;
	__int64 totalProfileTime = 0;

	CThreadProfileManager* pThreadMgr;
	AcquireSRWLockShared(&_srwlThreadProfileManager);
	for (size_t i = 0; i < _vecThreadProfileManager.size(); i++)
	{
		pThreadMgr = _vecThreadProfileManager[i];
		if (pThreadMgr->_bNeedReset == true) // 리셋해야하는데 아직 안했으면 넘어감
			continue;

		totalProfileCall += pThreadMgr->_iTotalProfileCall;
		totalProfileTime += pThreadMgr->_iTotalProfileTime;
		for (auto iter = pThreadMgr->_mapProfileSample.begin(); iter != pThreadMgr->_mapProfileSample.end(); ++iter)
		{
			listProfileSample.push_back(ProfileSample(*(iter->second)));
		}
	}
	ReleaseSRWLockShared(&_srwlThreadProfileManager);

	// tag가 같은 프로파일링 데이터를 합침
	for (auto iter = listProfileSample.begin(); iter != listProfileSample.end(); ++iter)
	{
		ProfileSample& sample = *iter;

		auto iterInner = iter;
		++iterInner;
		for (; iterInner != listProfileSample.end(); )
		{
			ProfileSample& sampleInner = *iterInner;
			if (sample.tagName == sampleInner.tagName)
			{
				// tag가 같은 2개 프로파일링 데이터를 합친다.
				sample.iTotalTime += sampleInner.iTotalTime;
				sample.iCall += sampleInner.iCall;

				if (sample.iMax[0] > sampleInner.iMax[0])
				{
					sample.iMax[1] = max(sample.iMax[1], sampleInner.iMax[0]);
				}
				else
				{
					sample.iMax[0] = sampleInner.iMax[0];
					sample.iMax[1] = max(sample.iMax[0], sampleInner.iMax[1]);
				}
				
				if (sample.iMin[0] < sampleInner.iMin[0])
				{
					sample.iMin[1] = min(sample.iMin[1], sampleInner.iMin[0]);
				}
				else
				{
					sample.iMin[0] = sampleInner.iMin[0];
					sample.iMin[1] = min(sample.iMin[0], sampleInner.iMin[1]);
				}
				

				// 합쳐진 데이터는 삭제
				iterInner = listProfileSample.erase(iterInner);
			}
			else
			{
				++iterInner;
			}
		}
	}


	// 파일명 설정
	struct tm curTime;
	time_t longTime;
	time(&longTime);
	localtime_s(&curTime, &longTime);
	WCHAR szProfileFileName[31];
	swprintf_s(szProfileFileName, 31, L"Profiling_%04d%02d%02d_%02d%02d%02d.txt"
		, curTime.tm_year, curTime.tm_mon + 1, curTime.tm_mday, curTime.tm_hour, curTime.tm_min, curTime.tm_sec);

	wstring strProfileFileName = strFilePath;
	strProfileFileName += szProfileFileName;


	// 파일 열기
	FILE* fProfile;
	_wfopen_s(&fProfile, strProfileFileName.c_str(), L"w, ccs=UTF-16LE");
	wstring strHeader = L"Name\tTotal(㎲)\tAverage(㎲)\tMin(㎲)\tMax(㎲)\tCall(num)\n";
	fwrite(strHeader.c_str(), 2, strHeader.size(), fProfile);

	// 프로파일링 데이터 출력 준비
	LARGE_INTEGER liFreq;
	QueryPerformanceFrequency(&liFreq);
	double dFreq = (double)liFreq.QuadPart;
	wostringstream oss;
	wstring strOutput;
	oss.flags(oss.fixed);
	oss.precision(4);


	// 프로파일링 데이터 출력
	for (auto iter = listProfileSample.begin(); iter != listProfileSample.end(); ++iter)
	{
		ProfileSample& pSample = *iter;
		__int64 iTotalTime = pSample.iTotalTime;
		__int64 iCall = pSample.iCall;

		if (iCall > 2)
		{
			iTotalTime = iTotalTime - pSample.iMin[0] - pSample.iMax[0];
			iCall -= 2;
		}

		double dTotal = (double)iTotalTime / dFreq * 1000000.0;
		double dAverage = (double)iTotalTime / (double)iCall / dFreq * 1000000.0;
		double dMin = (double)pSample.iMin[1] / dFreq * 1000000.0;
		double dMax = (double)pSample.iMax[1] / dFreq * 1000000.0;

		oss.str(L"");
		oss << pSample.tagName << L"\t" << dTotal << L"\t" << dAverage << L"\t" <<
			dMin << L"\t" << dMax << L"\t" << iCall << L"\n";
		strOutput = oss.str();
		fwrite(strOutput.c_str(), 2, strOutput.size(), fProfile);
	}


	// 프로파일링 자체 소요시간 출력
	oss.str(L"");
	oss << L"Total profiling time\t" << totalProfileTime << L"\t"
		<< (double)totalProfileTime / (double)totalProfileCall / dFreq * 1000000.0
		<< L"\t\t\t" << totalProfileCall << L"\n";
	strOutput = oss.str();
	fwrite(strOutput.c_str(), 2, strOutput.size(), fProfile);



	fclose(fProfile);
}



// 전체 프로파일링 데이터를 초기화한다.
// 초기화 플래그만 세팅하고 실제 초기화는 스레드별로 따로 수행한다. (프로파일 샘플에 lock을 걸지않기 위해)
void CProfileManager::ProfileReset(void)
{
	AcquireSRWLockShared(&_srwlThreadProfileManager);
	for (size_t i = 0; i < _vecThreadProfileManager.size(); i++)
	{
		_vecThreadProfileManager[i]->_bNeedReset = true;
	}
	ReleaseSRWLockShared(&_srwlThreadProfileManager);
}




CThreadProfileManager::CThreadProfileManager()
	:_iTotalProfileCall(0), _iTotalProfileTime(0), _bNeedReset(false)
{
	// 프로파일링 수행됨 플래그 on
	if (InterlockedExchange8((char*)&g_profileManager._bProfileStarted, true) == false)
	{
		// 플래그를 처음 on할 때, 콘솔 어플리케이션인 경우에는 console control handler 를 등록하여 사용자가 콘솔창 종료 시 프로파일링 결과가 파일로 출력될 수 있도록 함
	#ifdef _CONSOLE
		BOOL fSuccess = SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
	#endif
	}
}

CThreadProfileManager::~CThreadProfileManager()
{
}

// 스레드 내의 프로파일링 데이터를 초기화한다.
void CThreadProfileManager::Reset()
{
	ProfileSample* pSample;
	for (auto iter = _mapProfileSample.begin(); iter != _mapProfileSample.end(); ++iter)
	{
		pSample = iter->second;

		// pSample->liStartTime.QuadPart = 0; // liStartTime은 초기화하지 않음
		pSample->iTotalTime = 0;
		pSample->iMin[0] = INT64_MAX;
		pSample->iMin[1] = INT64_MAX;
		pSample->iMax[0] = 0;
		pSample->iMax[1] = 0;
		pSample->iCall = 0;
	}

	_iTotalProfileCall = 0;
	_iTotalProfileTime = 0;
	_bNeedReset = false;
}



