#pragma once
#include "CObject.h"


class CCrystal : public CObject
{
private:
	friend class CGameContentsField;

	float _posX;
	float _posY;
	int _tileX;
	int _tileY;
	int _sectorX;
	int _sectorY;
	BYTE _crystalType;
	int _amount;

	bool _bAlive;

public:
	CCrystal();
	virtual ~CCrystal();

public:
	void Init(float posX, float posY, BYTE crystalType);

	void SetDead();

	// @Override
	virtual void Update();
};