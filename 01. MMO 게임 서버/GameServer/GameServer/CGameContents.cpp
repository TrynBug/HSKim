#include "CGameServer.h"
#include "CGameContents.h"


CGameContents::CGameContents(EContentsThread myContentsType, CGameServer* pGameServer, int FPS)
	: CContents(FPS)
	, _eMyContentsType(myContentsType), _pGameServer(pGameServer), _FPS(FPS)
{
}

CGameContents::CGameContents(EContentsThread myContentsType, CGameServer* pGameServer, int FPS, DWORD sendMode)
	: CContents(FPS, sendMode)
	, _eMyContentsType(myContentsType), _pGameServer(pGameServer), _FPS(FPS)
{
}

CGameContents::~CGameContents()
{

}





/* session */
// 세션을 destination 컨텐츠로 옮긴다. 세션에 대한 메시지는 현재 스레드에서는 더이상 처리되지 않는다.
void CGameContents::TransferSession(__int64 sessionId, EContentsThread destination, PVOID data)
{
	CContents::TransferSession(sessionId, _pGameServer->GetContentsPtr(destination), data);
}



/* 플레이어 */
void CGameContents::FreePlayer(CPlayer* pPlayer)
{
	for (int i = 0; i < pPlayer->_vecPacketBuffer.size(); i++)
	{
		pPlayer->_vecPacketBuffer[i]->SubUseCount();
	}
	pPlayer->_vecPacketBuffer.clear();
	_pGameServer->_poolPlayer.Free(pPlayer);
}