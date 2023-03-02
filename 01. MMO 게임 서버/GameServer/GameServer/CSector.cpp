#include <Windows.h>
#include <vector>
#include "defineGameServer.h"
#include "CSector.h"


CSector::CSector(int x, int y)
	:_x(x), _y(y), _arrAroundSector{ 0, }, _numAroundSector(0)
{

}

CSector::~CSector()
{
}



void CSector::AddAroundSector(CSector* pSector)
{
	_arrAroundSector[_numAroundSector] = pSector;
	_numAroundSector++;
}


void CSector::AddObject(ESectorObjectType type, CObject* pObject)
{
	_vecObject[(UINT)type].push_back(pObject);
}


void CSector::RemoveObject(ESectorObjectType type, CObject* pObject)
{
	bool remove = false;
	std::vector<CObject*>& vec = _vecObject[(UINT)type];
	for (int i = 0; i < vec.size(); i++)
	{
		if (vec[i] == pObject)
		{
			vec[i] = vec.back();
			vec.pop_back();
			remove = true;
			break;
		}
	}
	if (remove == false)
	{
		int* p = 0;
		*p = 0;
	}
}

