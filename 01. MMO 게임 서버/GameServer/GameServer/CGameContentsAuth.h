#pragma once

class CDBConnector;

class CGameContentsAuth : public CGameContents
{
private:
	int _sessionTransferLimit;

	/* DB */
	CDBConnector* _pDBConn;

public:
	CGameContentsAuth(EContentsThread myContentsType, CGameServer* pGameServer, int FPS);
	virtual ~CGameContentsAuth();

	/* Get (가상함수) */
	// @Override
	virtual __int64 GetQueryRunCount() const;
	virtual float GetMaxQueryRunTime() const;
	virtual float GetMinQueryRunTime() const;
	virtual float GetAvgQueryRunTime() const;

	/* Set */
	void SetSessionTransferLimit(int limit) { _sessionTransferLimit = limit; }

private:
	/* 컨텐츠 클래스 callback 함수 구현 */
	virtual void OnUpdate();                                     // 주기적으로 호출됨
	virtual void OnRecv(__int64 sessionId, game_netserver::CPacket* pPacket);    // 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
	virtual void OnSessionJoin(__int64 sessionId, PVOID data);   // 스레드에 세션이 들어왔을 때 호출됨
	virtual void OnSessionDisconnected(__int64 sessionId);       // 스레드 내에서 세션의 연결이 끊겼을때 호출됨

};

