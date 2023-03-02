#pragma once

class CGameContents : public game_netserver::CContents
{
	friend class CGameServer;

private:
	EContentsThread _eMyContentsType;   // 내 컨텐츠 타입
	CGameServer* _pGameServer;          // 게임 서버 포인터
	int _FPS;                           // FPS

protected:
	std::unordered_map<__int64, CPlayer*> _mapPlayer;  // 플레이어 맵

	/* 모니터링 */
	volatile __int64 _disconnByInvalidMessageType = 0;

public:
	CGameContents(EContentsThread myContentsType, CGameServer* pGameServer, int FPS);
	CGameContents(EContentsThread myContentsType, CGameServer* pGameServer, int FPS, DWORD sendMode);
	virtual ~CGameContents();

	/* Get */
	EContentsThread GetMyContentsType() const { return _eMyContentsType; }
	int GetNumPlayer() const { return (int)_mapPlayer.size(); }

	/* Get 모니터링 */
	__int64 GetDisconnByInvalidMsgType() const { return _disconnByInvalidMessageType; }


	/* Get 모니터링 (가상함수) */
	virtual __int64 GetQueryRunCount() const { return 0; }
	virtual float GetMaxQueryRunTime() const { return 0; }
	virtual float GetMinQueryRunTime() const { return 0; }
	virtual float GetAvgQueryRunTime() const { return 0; }
	virtual int GetRemainQueryCount() const { return 0; }


	/* config.json */
	CJsonParser* GetConfigJsonParser() { return _pGameServer->_configJsonParser; }

	/* redis */
	cpp_redis::client* GetRedisClient() { return _pGameServer->_pRedisClient; }



	/* session */
	void TransferSession(__int64 sessionId, EContentsThread destination, PVOID data);   // 세션을 destination 컨텐츠로 옮긴다. 세션에 대한 메시지는 현재 스레드에서는 더이상 처리되지 않는다.
	void DisconnectSession(__int64 sessionId) { CContents::DisconnectSession(sessionId); }

	/* 플레이어 */
	CPlayer* AllocPlayer() { return _pGameServer->_poolPlayer.Alloc(); }
	void FreePlayer(CPlayer* pPlayer);

private:
	/* 컨텐츠 클래스 callback 함수 */
	virtual void OnUpdate() = 0;                                     // 주기적으로 호출됨
	virtual void OnRecv(__int64 sessionId, game_netserver::CPacket* pPacket) = 0;    // 컨텐츠가 관리하는 세션의 메시지큐에 메시지가 들어왔을 때 호출됨
	virtual void OnSessionJoin(__int64 sessionId, PVOID data) = 0;   // 스레드에 세션이 들어왔을 때 호출됨
	virtual void OnSessionDisconnected(__int64 sessionId) = 0;       // 스레드 내에서 세션의 연결이 끊겼을때 호출됨
};