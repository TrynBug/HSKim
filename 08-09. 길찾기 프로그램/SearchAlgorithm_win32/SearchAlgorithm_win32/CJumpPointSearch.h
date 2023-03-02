#pragma once

// CREATE_COLOR_GROUP이 정의되어 있으면 cell 들의 색상 그룹 정보를 생성한다.
// 색상 그룹 정보는 _arr2CellColorGroup 에 저장된다.
#define CREATE_COLOR_GROUP


enum class eJPSNodeType
{
	NONE,
	OPEN,
	CLOSE,
	END
};

enum class eJPSNodeDirection
{
	NONE,
	UU,
	RU,
	RR,
	RD,
	DD,
	LD,
	LL,
	LU,
};


class JPSNode
{
public:
	int _x;
	int _y;
	JPSNode* _pParent;
	float _valG;
	float _valH;
	float _valF;
	eJPSNodeType _type;
	eJPSNodeDirection _direction;

public:
	JPSNode(int x, int y, JPSNode* pParent, float valG, float valH, eJPSNodeType type, eJPSNodeDirection direction)
		: _x(x), _y(y), _pParent(pParent), _valG(valG), _valH(valH), _valF(valG + valH)
		, _type(type), _direction(direction)
	{ }

	JPSNode()
		: _x(0), _y(0), _pParent(nullptr), _valG(0.f), _valH(0.f), _valF(0.f)
		, _type(eJPSNodeType::NONE), _direction(eJPSNodeDirection::NONE)
	{}

	void SetMembers(int x, int y, JPSNode* pParent, float valG, float valH, eJPSNodeType type, eJPSNodeDirection direction)
	{
		_x = x;
		_y = y;
		_pParent = pParent;
		_valG = valG;
		_valH = valH;
		_valF = valG + valH;
		_type = type;
		_direction = direction;
	}
};




class CJumpPointSearch
{
private:
	enum class eCellInfo
	{
		FREE,
		OPEN,
		CLOSE,
		WALL,
		DESTINATION,
		END = 127,
	};

private:
	POINT _ptStart;
	POINT _ptEnd;
	int _gridRows;
	int _gridCols;

	bool _isSearchStepByStep;      // step by step search 시행중 여부
	bool _isFoundDest;
	char** _arr2CellInfo;          // cell 속성. (char)eCellInfo 를 값으로 가진다. 
	                               // 배열의 크기는 (_gridRows + 2) * (_gridCols + 2) 인데, 전체 grid를 둘러싸는 가상의 wall을 만들기 위해서이다.
	                               // 때문에 이 배열을 사용할 때는 (x, y) 좌표를 (x+1, y+1) 로 보정하여 사용한다.

	JPSNode* _pStartNode;
	std::multimap<float, JPSNode*> _mulmapOpenList;  // open list
	std::vector<JPSNode*> _vecCloseList;             // close list
	std::vector<POINT> _vecPath;                     // 경로 저장 벡터	
	std::vector<POINT> _vecSmoothPath;               // Bresenham 알고리즘으로 보정된 path



#ifdef CREATE_COLOR_GROUP
	unsigned char** _arr2CellColorGroup;   // cell 색상 그룹. _colorGroup 을 값으로 가진다. 배열의 크기는 _gridRows * _gridCols 이다.
	unsigned char _colorGroup;
#endif


public:
	CJumpPointSearch();
	~CJumpPointSearch();

public:
	// 파라미터 설정
	// _arr2Wall는 grid Wall 정보. 값이 1이면 wall, 0이면 wall이 아님.
	void SetParam(int startRow, int startCol, int endRow, int endCol, const char* const* arr2Wall, int gridRows, int gridCols);

	// search
	bool Search();

	// step by step search
	bool SearchStepByStep();  // 시작. 시작 전 SetParam 함수로 파라미터를 설정해야함
	bool SearchNextStep();    // 다음 step search

	// get
	const std::multimap<float, JPSNode*>& GetOpenList() { return _mulmapOpenList; }
	const std::vector<JPSNode*>& GetCloseList() { return _vecCloseList; }
	const std::vector<POINT>& GetPath() { return _vecPath; }
	const std::vector<POINT>& GetSmoothPath() { return _vecSmoothPath; }   // Bresenham 알고리즘으로 보정된 path 얻기
	int GetGridRows() { return _gridRows; }
	int GetGridCols() { return _gridCols; }
	bool IsSucceeded() { return _isFoundDest; }
	
	

#ifdef CREATE_COLOR_GROUP
	const unsigned char* const* GetCellColorGroup() { return _arr2CellColorGroup; }
	int GetNumOfColorGroup() { return (int)_colorGroup; }
#endif
	

	// clear
	void Clear();

private:
	void _createCellInfoArray();
	void _createDestNode(JPSNode* pParentNode, float valG, eJPSNodeDirection direction);

	void _searchUpward(JPSNode* pParentNode);
	void _searchRightUp(JPSNode* pParentNode);
	void _searchRight(JPSNode* pParentNode);
	void _searchRightDown(JPSNode* pParentNode);
	void _searchDown(JPSNode* pParentNode);
	void _searchLeftDown(JPSNode* pParentNode);
	void _searchLeft(JPSNode* pParentNode);
	void _searchLeftUp(JPSNode* pParentNode);

	bool _lookRight(int x, int y);
	bool _lookUpward(int x, int y);
	bool _lookDown(int x, int y);
	bool _lookLeft(int x, int y);




};