#pragma once

#include <vector>
#include <unordered_map>

/*
클래스 기반의 프로파일러
시간을 측정하려고 하는 코드 앞에 PROFILE_BEGIN을 호출한다.
PROFILE_BEGIN(TagName) 만 호출하면 Profiler 객체가 생성되어 블록 종료 시 자동으로 소멸자가 호출되어 시간이 측정된다.
만약 측정하고싶은 코드가 현재 블록의 끝까지라면 PROFILE_BEGIN(TagName)만 사용해도 된다.
만약 블록 중간에 측정을 멈추고 싶다면 PROFILE_END(TagName) 를 추가로 사용할 수 있다.
만약 하나의 블록 내에서 여러 부분의 코드를 따로 측정하고 싶다면 각 부분을 블록으로 감싼 뒤 블록 내부에서 PROFILE_BEGIN을 호출해야 한다.

블록 생성을 매크로로 하고 싶다면 PROFILE_BLOCK_BEGIN, PROFILE_BLOCK_END 를 함께 사용한다.
PROFILE_BLOCK_BEGIN 은 PROFILE_BEGIN 앞에 { 를 붙인것, 
PROFILE_BLOCK_END는 } 이다.
그래서 이 둘은 항상 함께 사용되어야 한다.

만약 조건에 따라 Tag 명을 다르게 하여 시간을 측정하고 싶다면,
먼저 PROFILE_READY_TO_DELAYED_BEGIN 를 호출하여 Tag가 지정되지 않은 Profiler 객체를 생성한다.
그런다음 if문 등에서 아래와 같이 PROFILE_DELAYED_BEGIN(TagName) 를 호출하여 시간 측정을 시작한다.
if (val == 0)
	PROFILE_DELAYED_BEGIN("when val is 0")
else
	PROFILE_DELAYED_BEGIN("when val isn't 0")
(만약 if문 아래에서 PROFILE_BEGIN(TagName) 를 사용하면 if문을 벗어나자마자 Profiler 객체의 소멸자가 호출되어버릴 것이다.)


!! 주의: 만약 다른 장소에서 같은 TagName을 사용한다면 문제가 발생할 수 있음
근데 문자열 리터럴의 주소가 프로그램이 종료될때까지 안바뀌는거 맞나...?
*/


//#define ENABLE_PROFILER

#ifdef ENABLE_PROFILER
#define PROFILE_BEGIN(TagName)  CProfiler __profiler(L##TagName);
#define PROFILE_END(TagName)    __profiler.End(L##TagName);

#define PROFILE_BLOCK_BEGIN(TagName) { CProfiler __profiler(L##TagName);
#define PROFILE_BLOCK_END            }

#define PROFILE_READY_TO_DELAYED_BEGIN CProfiler __profiler;
#define PROFILE_DELAYED_BEGIN(TagName) __profiler.Begin(L##TagName);


#else
#define PROFILE_BEGIN(TagName)            ;
#define PROFILE_END(TagName)			  ;
#define PROFILE_BLOCK_BEGIN(TagName)	  ;
#define PROFILE_BLOCK_END				  ;
#define PROFILE_READY_TO_DELAYED_BEGIN	  ;
#define PROFILE_DELAYED_BEGIN(TagName)	  ;
#endif




//// 컴파일 타임에 tag길이 계산
//int constexpr _profilerGetTagLength(const wchar_t* tag)
//{
//	return *tag == L'\0' ? 0 : _profilerGetTagLength(tag + 1) + 1;
//}
//
//// 이거 안쓰면 constexpr 함수 리턴값을 다른함수 파라미터로 사용할때 컴파일타임에 계산안됨
//template <int N>
//constexpr int _wrapConstexprInt() { return N; }
// 
//#define PROFILE_BEGIN(TagName)  Profiler __profiler(L##TagName, _wrapConstexprInt<_profilerGetTagLength(L##TagName)>());





namespace profiler
{
	void ResetProfilingData();  // 프로파일링 데이터 초기화
	void OutputProfilingData(); // 프로파일링 데이터 파일로 출력
}




// 프로파일링 결과 저장에 사용할 구조체
struct ProfileSample
{
	long          lFlag;        // option
	const wchar_t* tagName;     // 프로파일 샘플 이름
	LARGE_INTEGER liStartTime;  // 프로파일링 시작 시간
	__int64       iTotalTime;   // 전체 소요시간 seconds (출력시 호출횟수로 나누어 평균을 구함)
	__int64       iMin[2];      // 최소 소요시간 seconds ([0] 최소값, [1] 최소값 바로위)
	__int64       iMax[2];      // 최대 소요시간 seconds ([0] 최대값, [1] 최대값 바로아래)
	__int64       iCall;        // 누적 호출 횟수

	ProfileSample(const wchar_t* tagName)
		:tagName(tagName)
		, lFlag(0), liStartTime{ 0, }, iTotalTime(0), iMax{ 0, }, iCall(0)
	{
		iMin[0] = INT64_MAX;
		iMin[1] = INT64_MAX;
	}
};


// 각 프로파일링 시작과 종료를 수행하는 클래스
class CProfiler
{
private:
	const wchar_t*   _tag;
	bool             _isEnd;

public:
	CProfiler();
	CProfiler(const wchar_t* tag);
	~CProfiler();

public:
	void Begin();
	void Begin(const wchar_t* tag);
	void End(const wchar_t* tag);
};


// 프로파일링 전체 데이터 관리 클래스
// 이 클래스의 인스턴스는 profiler.cpp 파일 내에서 전역으로 1개 존재함
class CThreadProfileManager;
class CProfileManager
{
	friend class CProfiler;

public:
	bool _bProfileStarted;   // 프로파일링이 한번이라도 수행된적이 있음

public:
	CProfileManager();
	~CProfileManager();

private:
	DWORD _tlsIndex;
	std::vector<CThreadProfileManager*> _vecThreadProfileManager;
	SRWLOCK _srwlThreadProfileManager;

public:
	void ProfileReset();
	void ProfileDataOutText(const wchar_t* strFilePath);
};



// 1개 스레드 내의 프로파일링 데이터 관리 클래스
class CThreadProfileManager
{
	friend class CProfiler;
	friend class CProfileManager;

	std::unordered_map<const wchar_t*, ProfileSample*> _mapProfileSample;  // 프로파일 샘플
	std::vector<__int64*> _vecProfileHierarchy;   // 프로파일러 계층. 현재 vector의 맨 앞에 있는 샘플이 가장 높은 계층의 샘플이다.
	__int64 _iTotalProfileCall;   // 프로파일링 전체 시행 횟수
	__int64 _iTotalProfileTime;   // 프로파일링을 하는데 소요된 전체 시간
	
	bool _bNeedReset;  // reset 해야함 여부

public:
	CThreadProfileManager();
	~CThreadProfileManager();

private:
	void Reset();
};