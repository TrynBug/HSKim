#include <windows.h>
#include <math.h>
#include "CBresenhamPath.h"



CBresenhamPath::CBresenhamPath()
	: _ptStart{}, _ptEnd{}
	, _x(0), _y(0)
	, _diffX(0), _diffY(0)
	, _error(0)
{
}


CBresenhamPath::CBresenhamPath(POINT ptStart, POINT ptEnd)
	: _ptStart(ptStart), _ptEnd(ptEnd)
{
	Reset();
}


void CBresenhamPath::SetParam(POINT ptStart, POINT ptEnd)
{
	_ptStart = ptStart;
	_ptEnd = ptEnd;

	Reset();
}


void CBresenhamPath::Reset()
{
	_x = _ptStart.x;
	_y = _ptStart.y;
	_diffX = _ptEnd.x - _ptStart.x;
	_diffY = _ptEnd.y - _ptStart.y;

	if (_diffX > 0)
	{
		if (_diffY > 0)
		{
			if (_diffX > _diffY)
			{
				// ---------กๆ
				//          ก้
				_case = eCase::RightDown;
				_error = max(0, _diffX / 2 / max(1, _diffY) - 1) * _diffY;
			}
			else
			{
				// ---กๆ
				//    |
				//    ก้
				_case = eCase::DownRight;
				_error = max(0, _diffY / 2 / max(1, _diffX) - 1) * _diffX;
			}
		}
		else
		{
			if (_diffX > -_diffY)
			{
				//          ก่
				// ---------กๆ
				_case = eCase::RightUp;
				_error = max(0, _diffX / 2 / max(1, -_diffY) - 1) * -_diffY;
			}
			else
			{
				//    ก่
				//    |
				// ---กๆ
				_case = eCase::UpRight;
				_error = max(0, -_diffY / 2 / max(1, _diffX) - 1) * _diffX;
			}
		}
	}
	else
	{
		if (_diffY > 0)
		{
			if (-_diffX > _diffY)
			{
				// ก็---------
				// ก้
				_case = eCase::LeftDown;
				_error = max(0, -_diffX / 2 / max(1, _diffY) - 1) * _diffY;
			}
			else
			{
				// ก็---
				// |
				// ก้
				_case = eCase::DownLeft;
				_error = max(0, _diffY / 2 / max(1, -_diffX) - 1) * -_diffX;
			}
		}
		else
		{
			if (_diffX < _diffY)
			{
				// ก่
				// ก็---------
				_case = eCase::LeftUp;
				_error = max(0, -_diffX / 2 / max(1, -_diffY) - 1) * -_diffY;
			}
			else
			{
				// ก่
				// |
				// ก็---
				_case = eCase::UpLeft;
				_error = max(0, -_diffY / 2 / max(1, -_diffX) - 1) * -_diffX;
			}
		}
	}

}

