#pragma once

class CPlayer;


enum class eSectorDirection
{
	LU = 0,
	UU,
	RU,
	LL,
	CENTER,
	RR,
	LD,
	DD,
	RD
};

class CSector
{
	int _x;
	int _y;

	std::vector<CPlayer*> _vecPlayer;
	CSector* _arrAroundSector[9];

	static CSector* _dummySector;

public:
	CSector(int x, int y);
	~CSector();

public:
	int GetX() { return _x; }
	int GetY() { return _y; }
	int GetNumOfPlayer() { return (int)_vecPlayer.size(); }
	int GetNumOfAroundPlayer();
	const std::vector<CPlayer*>& GetPlayerVector() { return _vecPlayer; }
	CSector* GetAroundSector(eSectorDirection direction) { return _arrAroundSector[(int)direction]; }
	CSector** GetAllAroundSector() { return _arrAroundSector; }
	int GetNumOfAroundSector() { return 9; }

	void AddAroundSector(int aroundX, int aroundY, CSector* pSector);
	void AddPlayer(CPlayer* pPlayer);
	void RemovePlayer(CPlayer* pPlayer);

	static CSector* GetDummySector() { return _dummySector; }
};



