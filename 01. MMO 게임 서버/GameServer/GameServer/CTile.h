#pragma once

enum class ETileObjectType
{
	PLAYER,
	MONSTER,
	CRYSTAL,
	END
};

class CObject;

class CTile
{
	friend class CChatServer;

	int _x;
	int _y;

	std::vector<CObject*> _vecObject[(UINT)ETileObjectType::END];    // type 별 object 벡터
	CTile* _arrAroundTile[9];   // 주변 타일(자신 포함 최대 9개)
	int _numAroundTile;           // 주변 타일 수

public:
	CTile(int x, int y);
	virtual ~CTile();

public:
	int GetX() { return _x; }
	int GetY() { return _y; }
	int GetNumOfObject(ETileObjectType type) { return (int)_vecObject[(UINT)type].size(); }
	std::vector<CObject*>& GetObjectVector(ETileObjectType type) { return _vecObject[(UINT)type]; }

	void AddObject(ETileObjectType type, CObject* pObject);
	void RemoveObject(ETileObjectType type, CObject* pObject);

};



