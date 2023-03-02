#pragma once

// cellular automata 알고리즘으로 cave를 만든다.
// https://www.youtube.com/playlist?list=PLFt_AvWsXl0eZgMK_DT5_biRkWXftAOf9
struct Point
{
    int x;
    int y;

    Point(int x, int y)
        :x(x), y(y)
    {}
    Point()
        :x(0), y(0)
    {}
};

class CRoom
{
    friend class CCaveGenerator;

    std::vector<Point> _vecTiles;            // room에 포함된 tile
    std::vector<Point> _vecEdgeTiles;        // room의 가장자리 tile
    std::vector<CRoom*> _vecConnectedRooms;  // 연결된 room
    std::vector<Point> _vecConnectFrom;      // room 연결 시작좌표
    std::vector<Point> _vecConnectTo;        // room 연결 도착좌표
    bool _isAccessibleToMasterRoom;          // master room에 도달 가능 여부

    CRoom()
        : _isAccessibleToMasterRoom(false)
    {}
};

class CCaveGenerator
{
    char** _grid;
    int    _width;
    int    _height;

    std::vector<CRoom*> _vecRoom;

public:
    CCaveGenerator()
        :_grid(nullptr), _width(0), _height(0)
    {}

    ~CCaveGenerator()
    {
        for (size_t i = 0; i < _vecRoom.size(); i++)
        {
            delete _vecRoom[i];
        }
    }

public:
    // grid 에 cave를 생성한다.
    void Generate(char** grid, int width, int height, float randomFillPercent, int smoothCount);

    // get
    int GetNumOfRooms() { return (int)_vecRoom.size(); }
    int GetNumOfWays();

private:
    enum eWall
    {
        WAY = 0,
        WALL = 1
    };

    // cellular automata 알고리즘으로 cave를 만든다.
    void SmoothMap();
    // (x, y) 좌표 주변 8개 방향에서 wall 수를 계산한다.
    int GetSurroundingWallCount(int x, int y);

    // room을 생성한다.
    void GetRooms();
    // 최소거리를 가지는 2개 room들을 연결한다.
    void ConnectCloseRooms();
    // 연결된 room 사이에 길을 만든다.
    void MakePathBetweenRooms();


    // room 하나를 대표 room으로 정하고 모든 room이 대표 room에 도달할 수 있는지를 검사한다.
    bool _inspectAccessibilityToMasterRoom();
    // 대표 room에 도달할 수 없는 room 하나를 대표 room에 도달할 수 있는 room 중 가장 가까운 room과 연결한다.
    void _connectToMasterAccessibleRoom();
};



