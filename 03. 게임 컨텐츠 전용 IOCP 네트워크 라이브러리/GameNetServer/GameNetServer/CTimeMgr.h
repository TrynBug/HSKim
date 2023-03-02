#pragma once

#include <queue>

namespace game_netserver
{

/* DT 계산 매니저 */
class CTimeMgr
{
public:
	CTimeMgr();
	~CTimeMgr();

private:
	/* timer */
	bool _timer1s;                       // 1초 주기 타이머(_DTAcc >= 1.0 일 때마다 true가 됨)

	/* DT */
	int _frameCount;                     // FPS 계산에 사용되는 값
	LARGE_INTEGER _liFirstDTTime;        // 최초 DT 측정 시간
	LARGE_INTEGER _liDTLastUpdateTime;   // 마지막으로 DT를 업데이트한 시간
	LARGE_INTEGER _liFrequency;          // frequency
	double _DT;                          // Delta Time
	float _fDT;                          // Delta Time(float)
	double _DTAcc;                       // 누적 DT
	int _FPS;                            // FPS
	bool _isRunOverOneSec;               // 초기화된지 1초가 지났는지 여부
	bool _isDTOverOneSec;                // 최초 DT 측정 후 1초가 지났는지 여부
	bool _isDTOverOneMin;                // 최초 DT 측정 후 1분이 지났는지 여부
	double _minDT[2];                    // 최소DT ([0] 최소값, [1] 최소값 바로위)
	double _maxDT[2];                    // 최대DT ([0] 최대값, [1] 최대값 바로아래)
	double _avgDT1s;                     // 지난 1초 평균 DT
	int _minFPS[2];                      // 최소FPS ([0] 최소값, [1] 최소값 바로위)
	int _maxFPS[2];                      // 최대FPS ([0] 최대값, [1] 최대값 바로아래)
	float _avgFPS1s;                     // 지난 1초 평균 FPS(_avgDT1s 값을 FPS로 환산)
	float _avgFPS1m;                     // 지난 1분 평균 FPS

	std::queue<double> _qDT1s;           // 1초 동안의 DT 기록
	std::queue<int>    _qFPS1m;          // 1분 동안의 FPS 기록

	double __extraDT;                    // 내부적으로 _avgDT1s 계산에 사용됨

public:
	bool Is1sTimerSet() { return _timer1s; }

	double GetDT() { return _DT; }
	float GetfDT() { return _fDT; }
	double GetAvgDT1s() { return _avgDT1s; }
	double GetMinDT1st() { return _minDT[0]; }
	double GetMinDT2nd() { return _minDT[1]; }
	double GetMaxDT1st() { return _maxDT[0]; }
	double GetMaxDT2nd() { return _maxDT[1]; }
	int GetFPS() { return _FPS; }
	float GetAvgFPS1s() { return _avgFPS1s; }
	float GetAvgFPS1m() { return _avgFPS1m; }
	int GetMinFPS1st() { return _minFPS[0]; }
	int GetMinFPS2nd() { return _minFPS[1]; }
	int GetMaxFPS1st() { return _maxFPS[0]; }
	int GetMaxFPS2nd() { return _maxFPS[1]; }


public:
	void Init();            // DT 초기화
	void Update();          // DT 업데이트

};



}