#pragma once
/*
Bresenham 직선 알고리즘
*/

class CBresenhamPath
{
	POINT _ptStart;
	POINT _ptEnd;

	int _x;
	int _y;
	int _diffX;
	int _diffY;
	int _error;

	enum class eCase;
	eCase _case;

public:
	CBresenhamPath();
	CBresenhamPath(POINT ptStart, POINT ptEnd);

public:
	void SetParam(POINT ptStart, POINT ptEnd);
	void Reset();

	POINT GetStartPoint() { return _ptStart; }
	POINT GetEndPoint() { return _ptEnd; }

	inline bool Next();
	inline int GetX() { return _x; }
	inline int GetY() { return _y; }

private:
	enum class eCase
	{
		RightDown,
		DownRight,
		RightUp,
		UpRight,
		LeftDown,
		DownLeft,
		LeftUp,
		UpLeft,
		END
	};
};


bool CBresenhamPath::Next()
{
	if (_x == _ptEnd.x && _y == _ptEnd.y)
		return false;

	switch (_case)
	{
	case eCase::RightDown:
		// ---------→
		//          ↓
		_x++;
		_error += _diffY;
		if (_error >= _diffX)
		{
			_y++;
			_error -= _diffX;
		}
		break;

	case eCase::DownRight:
		// ---→
		//    |
		//    ↓
		_y++;
		_error += _diffX;
		if (_error >= _diffY)
		{
			_x++;
			_error -= _diffY;
		}
		break;

	case eCase::RightUp:
		//          ↑
		// ---------→
		_x++;
		_error -= _diffY;
		if (_error >= _diffX)
		{
			_y--;
			_error -= _diffX;
		}
		break;
	case eCase::UpRight:
		//    ↑
		//    |
		// ---→
		_y--;
		_error += _diffX;
		if (_error >= -_diffY)
		{
			_x++;
			_error += _diffY;
		}
		break;

	case eCase::LeftDown:
		// ←---------
		// ↓
		_x--;
		_error += _diffY;
		if (_error >= -_diffX)
		{
			_y++;
			_error += _diffX;
		}
		break;
	case eCase::DownLeft:
		// ←---
		// |
		// ↓
		_y++;
		_error -= _diffX;
		if (_error >= _diffY)
		{
			_x--;
			_error -= _diffY;
		}
		break;

	case eCase::LeftUp:
		// ↑
		// ←---------
		_x--;
		_error -= _diffY;
		if (_error >= -_diffX)
		{
			_y--;
			_error += _diffX;
		}
		break;

	case eCase::UpLeft:
		// ↑
		// |
		// ←---
		_y--;
		_error -= _diffX;
		if (_error >= -_diffY)
		{
			_x--;
			_error += _diffY;
		}
		break;


	}

	return true;
}


