#include <windows.h>
#include <map>
#include <vector>

#include "CJumpPointSearch.h"
#include "CBresenhamPath.h"
#include "CMemoryPool.h"

CMemoryPool<JPSNode> g_pool(0, true, false);

// 방향별 cell 속성 확인 매크로
#define CELL(x, y)        _arr2CellInfo[y][x]
#define UU_CELL(x, y)     _arr2CellInfo[y - 1][x]
#define RU_CELL(x, y)     _arr2CellInfo[y - 1][x + 1]
#define RR_CELL(x, y)     _arr2CellInfo[y][x + 1]
#define RD_CELL(x, y)     _arr2CellInfo[y + 1][x + 1]
#define DD_CELL(x, y)     _arr2CellInfo[y + 1][x]
#define LD_CELL(x, y)     _arr2CellInfo[y + 1][x - 1]
#define LL_CELL(x, y)     _arr2CellInfo[y][x - 1]
#define LU_CELL(x, y)     _arr2CellInfo[y - 1][x - 1]



// cell 색상그룹 지정 매크로
#ifdef CREATE_COLOR_GROUP
#define SET_COLOR_GROUP(x, y)  _arr2CellColorGroup[y][x] = _colorGroup;
#else 
#define SET_COLOR_GROUP(X, y)
#endif




CJumpPointSearch::CJumpPointSearch()
	:_ptStart{}, _ptEnd{}
	, _gridRows(0)
	, _gridCols(0)
	, _pStartNode(nullptr)
	, _isSearchStepByStep(false)
	, _isFoundDest(false)
	, _arr2CellInfo(nullptr)
{
#ifdef CREATE_COLOR_GROUP
	_arr2CellColorGroup = nullptr;
	_colorGroup = 1;
#endif
}


CJumpPointSearch::~CJumpPointSearch()
{
	Clear();
	if (_arr2CellInfo)
	{
		delete[] _arr2CellInfo[0];
		delete[] _arr2CellInfo;
	}

#ifdef CREATE_COLOR_GROUP
	if (_arr2CellColorGroup)
	{
		delete[] _arr2CellColorGroup[0];
		delete[] _arr2CellColorGroup;
	}
#endif
}


void CJumpPointSearch::SetParam(int startRow, int startCol, int endRow, int endCol, const char* const* arr2Wall, int gridRows, int gridCols)
{
	if (_gridRows != gridRows || _gridCols != gridCols)
	{
		_gridRows = gridRows;
		_gridCols = gridCols;
		_createCellInfoArray();
	}

	_ptStart.x = startCol;
	_ptStart.y = startRow;
	_ptEnd.x = endCol;
	_ptEnd.y = endRow;
	_gridRows = gridRows;
	_gridCols = gridCols;
	_isSearchStepByStep = false;
	_isFoundDest = false;

	Clear();

	// cell info에 벽, 도착지점 정보 입력
	for (int row = 0; row < _gridRows; row++)
	{
		for (int col = 0; col < _gridCols; col++)
		{
			if (arr2Wall[row][col] == 1)
			{
				_arr2CellInfo[row + 1][col + 1] = (char)eCellInfo::WALL;
			}
		}
	}
	_arr2CellInfo[_ptEnd.y + 1][_ptEnd.x + 1] = (char)eCellInfo::DESTINATION;
}



// search
bool CJumpPointSearch::Search()
{
	SearchStepByStep();
	while (!SearchNextStep())
	{
		continue;
	}

	return true;
}


// step by step search 시작
bool CJumpPointSearch::SearchStepByStep()
{
	// 시작 노드 생성
	_pStartNode = g_pool.Alloc();
	_pStartNode->SetMembers(_ptStart.x, _ptStart.y, nullptr
		, 0.f, (float)(abs(_ptEnd.x - _ptStart.x) + abs(_ptEnd.y - _ptStart.y))
		, eJPSNodeType::OPEN, eJPSNodeDirection::NONE);
	_arr2CellInfo[_ptStart.y + 1][_ptStart.x + 1] = (char)eCellInfo::OPEN;

	// step by step search 실행중 체크
	_isSearchStepByStep = true;

	// 8개 방향 search
	_searchUpward(_pStartNode);
	_searchRightUp(_pStartNode);
	_searchRight(_pStartNode);
	_searchRightDown(_pStartNode);
	_searchDown(_pStartNode);
	_searchLeftDown(_pStartNode);
	_searchLeft(_pStartNode);
	_searchLeftUp(_pStartNode);


	// close list에 시작 노드 등록
	_pStartNode->_type = eJPSNodeType::CLOSE;
	_vecCloseList.push_back(_pStartNode);

	return true;
}


// step by step search 다음 노드 탐색
bool CJumpPointSearch::SearchNextStep()
{
	if (!_isSearchStepByStep)
		return true;

	// 남은 open node가 없으면 실패
	if (_mulmapOpenList.size() == 0)
	{
		_isSearchStepByStep = false;
		return true;
	}

	// F value가 가장 작은 open node 획득
	JPSNode* pParentNode;
	auto iterFValue = _mulmapOpenList.begin();
	pParentNode = (*iterFValue).second;

	// 현재 노드를 open 에서 close로 변경
	pParentNode->_type = eJPSNodeType::CLOSE;
	_mulmapOpenList.erase(iterFValue);
	_vecCloseList.push_back(pParentNode);

	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;

	// 현재 노드가 목적지라면 도착
	if (CELL(x, y) == (char)eCellInfo::DESTINATION)
	{
		// 목적지에서 시작점까지의 경로를 저장함
		JPSNode* pNextNode = pParentNode;
		while (pNextNode != _pStartNode)
		{
			_vecPath.push_back(POINT{ pNextNode->_x, pNextNode->_y });
			pNextNode = pNextNode->_pParent;
		}
		_vecPath.push_back(POINT{ pNextNode->_x, pNextNode->_y });


		// Bresenham 알고리즘으로 보정된 경로 저장
		CBresenhamPath BPath;
		bool isAccessible = true;
		_vecSmoothPath.push_back(_vecPath[0]);
		for (int from = 0; from < (int)_vecPath.size() - 1; )
		{
			for (int to = (int)_vecPath.size() - 1; to > from; to--)
			{
				isAccessible = true;
				BPath.SetParam(_vecPath[from], _vecPath[to]);
				while (BPath.Next())
				{
					if (CELL(BPath.GetX() + 1, BPath.GetY() + 1) == (char)eCellInfo::WALL)
					{
						isAccessible = false;
						break;
					}
				}

				if (isAccessible)
				{
					_vecSmoothPath.push_back(_vecPath[to]);
					from = to;
					break;
				}
			}
		}

		_isSearchStepByStep = false;
		return true;
	}


#ifdef CREATE_COLOR_GROUP
	_colorGroup++;
#endif
	// 다음 노드 search
	switch (pParentNode->_direction)
	{
	case eJPSNodeDirection::UU:
	{
		_searchUpward(pParentNode);

		_searchLeftUp(pParentNode);
		_searchRightUp(pParentNode);
	}
	break;

	case eJPSNodeDirection::RU:
	{
		_searchUpward(pParentNode);
		_searchRight(pParentNode);
		_searchRightUp(pParentNode);

		_searchLeftUp(pParentNode);
		_searchRightDown(pParentNode);
	}
	break;

	case eJPSNodeDirection::RR:
	{
		_searchRight(pParentNode);

		_searchRightUp(pParentNode);
		_searchRightDown(pParentNode);
	}
	break;

	case eJPSNodeDirection::RD:
	{
		_searchRight(pParentNode);
		_searchDown(pParentNode);
		_searchRightDown(pParentNode);

		_searchLeftDown(pParentNode);
		_searchRightUp(pParentNode);
	}
	break;

	case eJPSNodeDirection::DD:
	{
		_searchDown(pParentNode);

		_searchLeftDown(pParentNode);
		_searchRightDown(pParentNode);
	}
	break;


	case eJPSNodeDirection::LD:
	{
		_searchLeft(pParentNode);
		_searchDown(pParentNode);
		_searchLeftDown(pParentNode);

		_searchLeftUp(pParentNode);
		_searchRightDown(pParentNode);
	}
	break;


	case eJPSNodeDirection::LL:
	{
		_searchLeft(pParentNode);

		_searchLeftUp(pParentNode);
		_searchLeftDown(pParentNode);
	}
	break;


	case eJPSNodeDirection::LU:
	{
		_searchUpward(pParentNode);
		_searchLeft(pParentNode);
		_searchLeftUp(pParentNode);

		_searchLeftDown(pParentNode);
		_searchRightUp(pParentNode);
	}
	break;

	}
	return false;
}



void CJumpPointSearch::_searchUpward(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		y -= 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::UU);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (RR_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::UU);

			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}



void CJumpPointSearch::_searchRightUp(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x += 1;
		y -= 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::RU);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL)
			|| _lookRight(x, y)
			|| _lookUpward(x, y))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::RU);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}



void CJumpPointSearch::_searchRight(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x += 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::RR);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((UU_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::RR);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}



void CJumpPointSearch::_searchRightDown(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x += 1;
		y += 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::RD);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL)
			|| (UU_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL)
			|| _lookRight(x, y)
			|| _lookDown(x, y))

		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::RD);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}




void CJumpPointSearch::_searchDown(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		y += 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::DD);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL)
			|| (RR_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::DD);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}





void CJumpPointSearch::_searchLeftDown(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x -= 1;
		y += 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::LD);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((RR_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL)
			|| (UU_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| _lookLeft(x, y)
			|| _lookDown(x, y))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::LD);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}






void CJumpPointSearch::_searchLeft(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x -= 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::LL);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((UU_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::LL);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}



void CJumpPointSearch::_searchLeftUp(JPSNode* pParentNode)
{
	int x = pParentNode->_x + 1;
	int y = pParentNode->_y + 1;
	float valG = pParentNode->_valG;
	while (true)
	{
		x -= 1;
		y -= 1;
		valG += 1.f;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				_createDestNode(pParentNode, valG, eJPSNodeDirection::LU);
			}
			break;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((RR_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL)
			|| _lookLeft(x, y)
			|| _lookUpward(x, y))

		{
			JPSNode* pNewNode = g_pool.Alloc();
			pNewNode->SetMembers(x - 1, y - 1, pParentNode
				, valG, (float)(abs(_ptEnd.x - x - 1) + abs(_ptEnd.y - y - 1))
				, eJPSNodeType::OPEN, eJPSNodeDirection::LU);
			_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
			CELL(x, y) = (char)eCellInfo::OPEN;
			break;
		}
	}
}





bool CJumpPointSearch::_lookRight(int x, int y)
{
	while (true)
	{
		x += 1;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				return true;
			}
			return false;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((UU_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			return true;
		}


	}
}


bool CJumpPointSearch::_lookUpward(int x, int y)
{
	while (true)
	{
		y -= 1;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				return true;
			}
			return false;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (RR_CELL(x, y) == (char)eCellInfo::WALL && RU_CELL(x, y) != (char)eCellInfo::WALL))
		{
			return true;
		}
	}
}


bool CJumpPointSearch::_lookDown(int x, int y)
{
	while (true)
	{
		y += 1;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				return true;
			}
			return false;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((LL_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL)
			|| (RR_CELL(x, y) == (char)eCellInfo::WALL && RD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			return true;
		}
	}
}



bool CJumpPointSearch::_lookLeft(int x, int y)
{
	while (true)
	{
		x -= 1;

		if (CELL(x, y) != (char)eCellInfo::FREE)
		{
			if (CELL(x, y) == (char)eCellInfo::DESTINATION)
			{
				return true;
			}
			return false;
		}

		SET_COLOR_GROUP(x - 1, y - 1);

		if ((UU_CELL(x, y) == (char)eCellInfo::WALL && LU_CELL(x, y) != (char)eCellInfo::WALL)
			|| (DD_CELL(x, y) == (char)eCellInfo::WALL && LD_CELL(x, y) != (char)eCellInfo::WALL))
		{
			return true;
		}
	}
}





void CJumpPointSearch::_createDestNode(JPSNode* pParentNode, float valG, eJPSNodeDirection direction)
{
	if (_isFoundDest == false)
	{
		_isFoundDest = true;
		JPSNode* pNewNode = g_pool.Alloc();
		pNewNode->SetMembers(_ptEnd.x, _ptEnd.y, pParentNode
			, valG, 0.f
			, eJPSNodeType::OPEN, direction);
		_mulmapOpenList.insert(std::make_pair(pNewNode->_valF, pNewNode));
	}
}



// 생성된 모든 노드를 지우고 cell info 배열을 초기화한다.
void CJumpPointSearch::Clear()
{
	for (auto iter = _mulmapOpenList.begin(); iter != _mulmapOpenList.end(); ++iter)
	{
		g_pool.Free((*iter).second);
	}

	for (size_t i = 0; i < _vecCloseList.size(); i++)
	{
		g_pool.Free(_vecCloseList[i]);
	}

	_mulmapOpenList.clear();
	_vecCloseList.clear();
	_vecPath.clear();
	_vecSmoothPath.clear();

	// cell info 배열을 초기화할 때 전체 grid는 FREE로, grid를 둘러싸는 테두리는 WALL로 설정한다.
	if (_arr2CellInfo)
	{
		memset(_arr2CellInfo[0], (char)eCellInfo::FREE, (_gridRows + 2) * (_gridCols + 2));
		memset(_arr2CellInfo[0], (char)eCellInfo::WALL, _gridCols + 2);
		memset(_arr2CellInfo[_gridRows + 1], (char)eCellInfo::WALL, _gridCols + 2);
		for (int row = 1; row < _gridRows + 1; row++)
		{
			_arr2CellInfo[row][0] = (char)eCellInfo::WALL;
			_arr2CellInfo[row][_gridCols + 1] = (char)eCellInfo::WALL;
		}
	}

#ifdef CREATE_COLOR_GROUP
	_colorGroup = 1;
	if (_arr2CellColorGroup)
		memset(_arr2CellColorGroup[0], 0, _gridRows * _gridCols);
#endif

}


// cell info 배열을 재생성한다.
void CJumpPointSearch::_createCellInfoArray()
{
	if (_arr2CellInfo)
	{
		delete[] _arr2CellInfo[0];
		delete[] _arr2CellInfo;
	}

	_arr2CellInfo = new char* [_gridRows + 2];
	_arr2CellInfo[0] = new char[(_gridRows + 2) * (_gridCols + 2)];
	for (int i = 1; i < _gridRows + 2; i++)
	{
		_arr2CellInfo[i] = _arr2CellInfo[0] + (i * (_gridCols + 2));
	}

#ifdef CREATE_COLOR_GROUP
	_colorGroup = 1;

	if (_arr2CellColorGroup)
	{
		delete[] _arr2CellColorGroup[0];
		delete[] _arr2CellColorGroup;
	}

	_arr2CellColorGroup = new unsigned char* [_gridRows];
	_arr2CellColorGroup[0] = new unsigned char[_gridRows * _gridCols];
	for (int i = 1; i < _gridRows; i++)
	{
		_arr2CellColorGroup[i] = _arr2CellColorGroup[0] + (i * _gridCols);
	}
#endif
}






/*
대각성방향 코너 찾기 규칙:
오른쪽위로갈때: 왼쪽막힘+왼쪽위뚫림, 아래막힘+오른쪽아래뚫림
오른쪽아래로갈때: 왼쪽막힘+왼쪽아래뚫림, 위막힘+오른쪽위뚫림
왼쪽위로갈때: 오른쪽막힘+오른쪽위뚫림, 아래막힘+왼쪽아래뚫림
왼쪽아래로갈때: 오른쪽막힘+오른쪽아래뚫림, 위막힘+왼쪽위뚫림
*/