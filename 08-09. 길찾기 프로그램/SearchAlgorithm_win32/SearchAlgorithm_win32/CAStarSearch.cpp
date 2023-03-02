#include "CAStarSearch.h"
#include "CMemoryPool.h"

#define MAKE_KEY(high, low) (((__int64)(high) << 32) | (__int64)(low))
#define GET_X_VALUE(value) ((int)(value >> 32))
#define GET_Y_VALUE(value) ((int)(value))


CMemoryPool<AStarNode> g_pool(0, true, false);



CAStarSearch::CAStarSearch()
	:_ptStart{}, _ptEnd{}, _arr2Grid(nullptr)
	, _gridRows(0), _gridCols(0), _gridSize(0)
	, _pStartNode(nullptr)
	, _isSearchStepByStep(false)
	, _isFoundDest(false)
	, _numOfOpenNode(0)
{
}


CAStarSearch::~CAStarSearch()
{
	Clear();
}





void CAStarSearch::SetParam(int startRow, int startCol, int endRow, int endCol, char** arr2Grid, int gridRows, int gridCols, int gridSize)
{
	_ptStart.x = startCol;
	_ptStart.y = startRow;
	_ptEnd.x = endCol;
	_ptEnd.y = endRow;
	_arr2Grid = arr2Grid;
	_gridRows = gridRows;
	_gridCols = gridCols;
	_gridSize = gridSize;
	_isSearchStepByStep = false;
	_isFoundDest = false;

	Clear();
}

bool CAStarSearch::Search()
{
	SearchStepByStep();
	while (!SearchNextStep())
	{
		continue;
	}

	return true;
}

bool CAStarSearch::SearchStepByStep()
{
	Clear();

	// wall list에 벽 좌표 입력
	for (int row = 0; row < _gridRows; row++)
	{
		for (int col = 0; col < _gridCols; col++)
		{
			if (_arr2Grid[row][col] == 1)
			{
				_wallList.insert(MAKE_KEY(col, row));
			}
		}
	}

	// 시작 노드 생성
	_pStartNode = g_pool.Alloc();
	_pStartNode->SetMembers(_ptStart.x, _ptStart.y, nullptr
		, 0.f, (float)(abs(_ptEnd.x - _ptStart.x) + abs(_ptEnd.y - _ptStart.y))
		, eAStarNodeType::OPEN);

	// node list에 등록
	_nodeList.insert(std::make_pair(MAKE_KEY(_ptStart.x, _ptStart.y), _pStartNode));
	_mulmapFValue.insert(std::make_pair(_pStartNode->_valF, MAKE_KEY(_ptStart.x, _ptStart.y)));
	_numOfOpenNode++;

	// step by step search 실행중 체크
	_isSearchStepByStep = true;

	return true;
}


bool CAStarSearch::SearchNextStep()
{
	if (!_isSearchStepByStep)
		return true;

	// 남은 open node가 없으면 실패
	if (_numOfOpenNode == 0)
	{
		_isSearchStepByStep = false;
		return true;
	}

	// F value가 가장 작은 open 노드의 좌표 획득
	auto iterFValue = _mulmapFValue.begin();
	__int64 keyParent = (*iterFValue).second;
	int x = GET_X_VALUE(keyParent);
	int y = GET_Y_VALUE(keyParent);

	// 해당 좌표의 노드 획득
	AStarNode* pParentNode = (*_nodeList.find(keyParent)).second;

	// 방문한 노드를 open 에서 close로 변경
	pParentNode->_type = eAStarNodeType::CLOSE;
	_mulmapFValue.erase(iterFValue);
	_numOfOpenNode--;

	// 좌표가 목적지와 동일하다면 도착
	if (x == _ptEnd.x && y == _ptEnd.y)
	{
		// 목적지에서 시작점까지의 경로를 저장함
		AStarNode* pNextNode = pParentNode;
		while (pNextNode != _pStartNode)
		{
			_vecPath.push_back(POINT{ pNextNode->_x, pNextNode->_y });
			pNextNode = pNextNode->_pParent;
		}
		_vecPath.push_back(POINT{ pNextNode->_x, pNextNode->_y });

		_isSearchStepByStep = false;
		_isFoundDest = true;
		return true;
	}


	// 다음 노드 search
	// 8개 방향 좌표, G값 증가치 설정
	// 위, 왼쪽위, 왼쪽, 왼쪽아래, 아래, 오른쪽아래, 오른쪽, 오른쪽위
	int arrX[8] = { x     , x - 1, x - 1, x - 1, x    , x + 1, x + 1, x + 1 };
	int arrY[8] = { y - 1 , y - 1, y    , y + 1, y + 1, y + 1, y    , y - 1 };
	float arrG[8] = { 1.f, 1.5f, 1.f, 1.5f, 1.f, 1.5f, 1.f, 1.5f };

	// 8개 방향 search
	__int64 keyNew;
	AStarNode* pNewNode;
	AStarNode* pOpenNode;
	for (int i = 0; i < 8; i++)
	{
		if (arrX[i] < 0 || arrX[i] >= _gridCols
			|| arrY[i] < 0 || arrY[i] >= _gridRows)
		{
			continue;
		}

		// node list, wall list에서 좌표를 못찾았으면 노드를 생성하고,
		// node list 에서 좌표를 찾았는데 해당 노드가 open 이라면 F-value를 다시한번 검사함
		keyNew = MAKE_KEY(arrX[i], arrY[i]);
		auto iterNode = _nodeList.find(keyNew);


		// node list에서 좌표를 찾지 못함
		if (iterNode == _nodeList.end())
		{
			// wall이 아닐 경우 노드 생성
			if (_wallList.find(keyNew) == _wallList.end())
			{
				// 노드 생성
				pNewNode = g_pool.Alloc();
				pNewNode->SetMembers(arrX[i], arrY[i], pParentNode
					, pParentNode->_valG + arrG[i]
					, (float)(abs(arrX[i] - _ptEnd.x) + abs(arrY[i] - _ptEnd.y))
					, eAStarNodeType::OPEN);

				// node list에 등록
				_nodeList.insert(std::make_pair(keyNew, pNewNode));
				_mulmapFValue.insert(std::make_pair(pNewNode->_valF, keyNew));
				_numOfOpenNode++;
			}
		}
		// node list에서 좌표를 찾음
		else
		{
			pOpenNode = (*iterNode).second;
			// node list에서 좌표를 찾았는데 close 상태임
			if (pOpenNode->_type == eAStarNodeType::CLOSE)
			{
				continue;
			}
			// node list에서 좌표를 찾았는데 open 상태임
			else
			{
				float newValG = pParentNode->_valG + arrG[i];
				// 만약 새로 계산한 G값이 기존 노드의 G값보다 작다면 기존 노드의 parent를 교체
				if (newValG < pOpenNode->_valG)
				{
					pOpenNode->_pParent = pParentNode;
					pOpenNode->_valG = newValG;
					pOpenNode->_valF = newValG + pOpenNode->_valH;
				}
			}
		}
	}

	return false;
}






// 생성된 모든 노드를 지운다.
void CAStarSearch::Clear()
{
	for (auto iter = _nodeList.begin(); iter != _nodeList.end(); ++iter)
	{
		g_pool.Free((*iter).second);
	}

	_nodeList.clear();
	_wallList.clear();
	_mulmapFValue.clear();
	_vecPath.clear();
	_numOfOpenNode = 0;
}


