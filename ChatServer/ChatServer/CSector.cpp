#include <vector>

#include "CSector.h"


// static ¸â¹ö ÃÊ±âÈ­
CSector* CSector::_dummySector = new CSector(-1, -1);


CSector::CSector(int x, int y)
	:_x(x), _y(y), _arrAroundSector{ 0, }
{
	_arrAroundSector[(int)eSectorDirection::CENTER] = this;
}

CSector::~CSector()
{
}


int CSector::GetNumOfAroundPlayer()
{
	int numOfAroundPlayer = 0;
	for (int i = 0; i < 9; i++)
	{
		numOfAroundPlayer += _arrAroundSector[i]->GetNumOfPlayer();
	}
	return numOfAroundPlayer;
}


void CSector::AddAroundSector(int aroundX, int aroundY, CSector* pSector)
{
	eSectorDirection direction;
	if (aroundY > _y)
	{
		if (aroundX > _x)
			direction = eSectorDirection::RD;
		else if (aroundX == _x)
			direction = eSectorDirection::DD;
		else
			direction = eSectorDirection::LD;
	}
	else if (aroundY == _y)
	{
		if (aroundX > _x)
			direction = eSectorDirection::RR;
		else if (aroundX == _x)
			direction = eSectorDirection::CENTER;
		else
			direction = eSectorDirection::LL;
	}
	else
	{
		if (aroundX > _x)
			direction = eSectorDirection::RU;
		else if (aroundX == _x)
			direction = eSectorDirection::UU;
		else
			direction = eSectorDirection::LU;
	}

	_arrAroundSector[(int)direction] = pSector;
}


void CSector::AddPlayer(CPlayer* pPlayer) 
{
	_vecPlayer.push_back(pPlayer); 
}


void CSector::RemovePlayer(CPlayer* pPlayer)
{
	for (int i = 0; i < _vecPlayer.size(); i++)
	{
		if (_vecPlayer[i] == pPlayer)
		{
			_vecPlayer[i] = _vecPlayer.back();
			_vecPlayer.pop_back();
			break;
		}
	}
}