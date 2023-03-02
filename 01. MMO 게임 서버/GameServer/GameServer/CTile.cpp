#include <Windows.h>
#include <vector>

#include "CTile.h"


CTile::CTile(int x, int y)
	:_x(x), _y(y), _arrAroundTile{ 0, }, _numAroundTile(0)
{

}

CTile::~CTile()
{
}




void CTile::AddObject(ETileObjectType type, CObject* pObject)
{
	_vecObject[(UINT)type].push_back(pObject);
}


void CTile::RemoveObject(ETileObjectType type, CObject* pObject)
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

