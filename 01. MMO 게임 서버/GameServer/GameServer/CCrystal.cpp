#include <iostream>
#include <Windows.h>
#include "defineGameServer.h"
#include "CCrystal.h"



CCrystal::CCrystal()
{

}

CCrystal::~CCrystal()
{
}

void CCrystal::Init(float posX, float posY, BYTE crystalType)
{
	CObject::Init();

	_posX = posX;
	_posY = posY;
	_tileX = dfFIELD_POS_TO_TILE(_posX);
	_tileY = dfFIELD_POS_TO_TILE(_posY);
	_sectorX = dfFIELD_TILE_TO_SECTOR(_tileX);
	_sectorY = dfFIELD_TILE_TO_SECTOR(_tileY);

	_crystalType = crystalType;
	_amount = crystalType * 10;
	
	_bAlive = true;
}


void CCrystal::SetDead()
{
	_bAlive = false;
}


// @Override
void CCrystal::Update()
{

}
