#include "framework.h"
#include "resource.h"
#include <stdio.h>
#include <process.h>
#include <conio.h>
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <ctime>
#include <vector>
#include <map>


#include "utils.h"
#include "CAStarSearch.h"
#include "CJumpPointSearch.h"
#include "CBresenhamPath.h"
#include "CCaveGenerator.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Winmm.lib")

// 색상
#define dfCOLOR_BACKGROUND    RGB(199, 209, 225)
#define dfCOLOR_GRID          RGB(150, 150, 150)
#define dfCOLOR_WALL          DKGRAY_BRUSH
#define dfCOLOR_START_POINT   RGB(0, 128, 64)
#define dfCOLOR_END_POINT     RGB(237, 28, 36)
#define dfCOLOR_RESIZE_BORDER RGB(50, 50, 50)
#define dfCOLOR_OPEN_NODE     RGB(100, 130, 255)
#define dfCOLOR_CLOSE_NODE    RGB(255, 255, 66)
#define dfCOLOR_PATH_LINE     RGB(60, 60, 60)
#define dfCOLOR_SMOOTH_PATH_LINE  RGB(163, 73, 164)
#define dfCOLOR_BRESENHAM     RGB(255, 151, 185)

// 그라데이션 그림자 단계
#define dfSHADOW_STEP         10

// 그리드, 그리기 영역 최대크기, 최소크기
#define dfGRID_SIZE_MAX       100
#define dfGRID_SIZE_MIN       5
#define dfPAINT_SIZE_MAX_X    3000
#define dfPAINT_SIZE_MAX_Y    3000
#define dfPAINT_SIZE_MIN_X    100
#define dfPAINT_SIZE_MIN_Y    100

#define dfGRID_WAY      0
#define dfGRID_WALL     1

// jump point search에서 cell 색칠에 사용할 색상 갯수
#define dfNUM_OF_CELL_COLOR_GROUP    6


// handle
HINSTANCE g_hInst;
HWND g_hWnd;
HDC g_hDC;
HWND g_hToolbar; // toolbar

// memory DC
HBITMAP g_hMemBit;
HDC g_hMemDC;

// 클라이언트 크기
RECT g_rtClientSize;
POINT g_ptMemBitSize;  // 메모리 비트맵 크기

// 타이머 
#define dfTIMER_ID     1     // ID
int g_timerFrequency = 300;  // 주기
int g_timerFrequencyIncrease = 100; // 주기 증감치
bool g_bResetTimer = false;  // 타이머 재설정 여부
int g_searchSpeedLevel = 0;  // 속도(값이 작으면 빠름, 음수 가능)


// 커서
bool g_isCursorChanged;   // 커서가 변경되어야하는지 여부
HCURSOR g_hDefaultCursor; // 기본 커서
HCURSOR g_hCurrentCursor; // 현재 커서

// grid array. cell의 wall 여부를 저장함
char** g_arr2Grid = nullptr;

// 그리드 그림을 그리기 위해 필요한 값들
struct Painting
{
    POINT ptPos;   // 그리기 영역 시작좌표
    POINT ptSize;  // 그리기 영역 크기
    int gridSize;  // grid cell 크기
    int gridRows;  // grid 행 수
    int gridCols;  // grid 열 수
    int startRow;  // 길찾기 시작점 row
    int startCol;  // 길찾기 시작점 col
    int endRow;    // 길찾기 목적지 row
    int endCol;    // 길찾기 목적지 col
    int gridRows_old;
    int gridCols_old;

    // set size. size는 SetSize 함수를 통해서만 설정되어야 한다.
    void SetSize(int sizeGrid, int sizeX, int sizeY)
    {
        gridSize = min(max(sizeGrid, dfGRID_SIZE_MIN), dfGRID_SIZE_MAX);
        SetSize(sizeX, sizeY);
    }
    // set size. size는 SetSize 함수를 통해서만 설정되어야 한다.
    void SetSize(int sizeX, int sizeY)
    {
        gridRows_old = gridRows;
        gridCols_old = gridCols;

        // 그리기영역 크기 설정
        ptSize.x = min(max(sizeX, dfPAINT_SIZE_MIN_X), dfPAINT_SIZE_MAX_X);
        ptSize.y = min(max(sizeY, dfPAINT_SIZE_MIN_Y), dfPAINT_SIZE_MAX_Y);

        // 그리기영역 크기가 gridSize 의 배수가 되도록 조정함
        int remainderX = ptSize.x % gridSize;
        if (remainderX < gridSize / 2)
            ptSize.x = ptSize.x - remainderX;
        else
            ptSize.x = ptSize.x - remainderX + gridSize;

        int remainderY = ptSize.y % gridSize;
        if (remainderY < gridSize / 2)
            ptSize.y = ptSize.y - remainderY;
        else
            ptSize.y = ptSize.y - remainderY + gridSize;

        // 그리드 행, 열 수 계산
        gridRows = ptSize.y / gridSize;
        gridCols = ptSize.x / gridSize;

        // 시작점, 도착점이 그리기영역을 벗어났을 경우 지움
        if (startCol > gridCols - 1 || startRow > gridRows - 1)
        {
            startCol = -1;
            startRow = -1;
        }
        if (endCol > gridCols - 1 || endRow > gridRows - 1)
        {
            endCol = -1;
            endRow = -1;
        }
    }
};
// 전역 Painting 객체
Painting g_painting = { {30, 48}, {1280, 720}, 20, 720 / 20, 1280 / 20, -1, -1, -1, -1 };


// 스크롤 관련
int g_hScrollPos = 0;  // 수평 스크롤 위치
int g_vScrollPos = 0;  // 수직 스크롤 위치
int g_hScrollMax = 0;  // 수평 스크롤 최대위치
int g_vScrollMax = 0;  // 수직 스크롤 최대위치
bool g_isRenderFromScrolling = false; // 렌더링이 스크롤링에 의한 것인지 여부. 스크롤링에 의한 것이면 object를 다시 그리지 않는다.

// 툴바
int g_tbBtnTool = IDM_TOOLBAR_WALL; // 현재 눌려진 그리기도구
int g_tbBtnAlgorithm = IDM_TOOLBAR_JUMP_POINT_SEARCH; // 현재 눌려진 길찾기 알고리즘
bool g_tbBtnIsFValueChecked = false;   // F-Value 버튼이 눌려졌는지 여부
int g_tbHeight = 28;  // 툴바 높이

// 마우스 관련 변수
int g_mouseX;
int g_mouseY;
int g_mouseClickX;
int g_mouseClickY;
bool g_clickPainting = false;
bool g_erase = false;
bool g_clickResizeVertical = false;
bool g_clickResizeDiagonal = false;
bool g_clickResizeHorizontal = false;

// 마우스 좌표얻기 매크로
#define mcGET_MOUSE_X_LPARAM(lParam) GET_X_LPARAM((lParam)) + g_hScrollPos
#define mcGET_MOUSE_Y_LPARAM(lParam) GET_Y_LPARAM((lParam)) + g_vScrollPos
// 마우스 좌표를 grid row, col 로 변환
#define mcMOUSE_Y_TO_GRID_ROW(mouseY)   (((mouseY) - g_painting.ptPos.y) / g_painting.gridSize)
#define mcMOUSE_X_TO_GRID_COL(mouseX)   (((mouseX) - g_painting.ptPos.x) / g_painting.gridSize)

// 길찾기
CAStarSearch g_AStarSearch;          // A* search
CJumpPointSearch g_jumpPointSearch;  // Jump Point Search

// 테스트
bool g_isTestRunning = false;
bool g_isTestDoRendering = true;
bool g_isPrintTestInfo = false;
unsigned int g_testRandomSeed;
int g_numOfCaveWays;


// windows procedure
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    WndProcDialogSizeAdj(HWND, UINT, WPARAM, LPARAM);

// Create Toolbar, Rebar
HWND CreateToolbar(HINSTANCE hInstance, HWND hWnd);

/* Utils */
// 창의 크기가 바뀔 때 bitmap, dc를 크기에 맞게 다시 만듬
void ResizeWindow();
// g_arr2Grid 전역 변수에 grid를 동적할당한다.
void InitGrid();
// g_arr2Grid 의 내용을 지운다.
void ClearGrid();
// grid를 다시 만들고 이전 내용을 복사한다.
void ResizeGrid();
// 스크롤을 재조정한다.
void ReadjustScroll();
// 테스트 중단 키보드 입력 감지 스레드
unsigned WINAPI ThreadProcQuitRunTest(PVOID vParam);



int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    // set random seed
    srand((unsigned int)time(0));

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);


    // 창 클래스를 등록합니다.
    g_hDefaultCursor = LoadCursor(nullptr, IDC_ARROW);
    g_hCurrentCursor = g_hDefaultCursor;
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_MY0608ASTARSEARCH));
    wcex.hCursor = g_hDefaultCursor;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_MY0608ASTARSEARCH);
    wcex.lpszClassName = L"WinClient";
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    RegisterClassExW(&wcex);




    // 애플리케이션 초기화를 수행합니다
    g_hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.

    g_hWnd = CreateWindowW(L"WinClient", L"Client", WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd)
    {
        DWORD dwError = GetLastError();
        return FALSE;
    }

    // Initialize common controls.
    INITCOMMONCONTROLSEX stInitCommonControls;
    stInitCommonControls.dwSize = sizeof(INITCOMMONCONTROLSEX);
    stInitCommonControls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&stInitCommonControls);

    // toolbar 만들기. g_hToolbar 변수에 toolbar 핸들을 저장한다.
    g_hToolbar = CreateToolbar(hInstance, g_hWnd);


    // 메인 윈도우의 device context를 얻는다.
    g_hDC = GetDC(g_hWnd);
    // 현재 윈도우 크기를 얻는다.
    GetClientRect(g_hWnd, &g_rtClientSize);

    // 이중 버퍼링 용도의 비트맵과 DC를 만든다.
    g_ptMemBitSize.x = g_rtClientSize.right;
    g_ptMemBitSize.y = g_rtClientSize.bottom;
    g_hMemBit = CreateCompatibleBitmap(g_hDC, g_ptMemBitSize.x, g_ptMemBitSize.y);
    g_hMemDC = CreateCompatibleDC(g_hDC);
    HBITMAP hOldBit = (HBITMAP)SelectObject(g_hMemDC, g_hMemBit);
    DeleteObject(hOldBit);

    // grid 초기화
    InitGrid();

    // Show Window
    ShowWindow(g_hWnd, nCmdShow);  // nCmdShow 는 윈도우를 최대화로 생성할 것인지, 최소화로 생성할 것인지 등을 지정하는 옵션이다.
    UpdateWindow(g_hWnd);          // 윈도우를 한번 갱신시킨다.



#ifdef _DEBUG
    // DEBUG 모드일 경우 콘솔 생성
    //AllocConsole();
    //FILE* stream;
    //freopen_s(&stream, "CONIN$", "r", stdin);
    //freopen_s(&stream, "CONOUT$", "w", stdout);
    //freopen_s(&stream, "CONOUT$", "w", stderr);
#endif


    // 단축키 사용 지정
    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MY0608ASTARSEARCH));

    // main loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}






// windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef _DEBUG
    // 디버깅 모드 중에 visual studio 출력 창에 메시지 값 출력하기
    char DebugLog[50];
    sprintf_s(DebugLog, "%llu, %u, %llu, %llu\n", (unsigned __int64)hWnd, message, (unsigned __int64)wParam, (unsigned __int64)lParam);
    OutputDebugStringA(DebugLog);
#endif

    // static GDI
    static HBRUSH sBrushOld;
    static HPEN sPenOld;

    static HBRUSH sBrushBackground;
    static HPEN sPenGrid;
    static HBRUSH sBrushOpenNode;
    static HBRUSH sBrushCloseNode;
    static HBRUSH sBrushStartPoint;
    static HBRUSH sBrushEndPoint;
    static HBRUSH sBrushWall;
    static HPEN sPenPath;
    static HPEN sPenSmoothPath;
    static HPEN arrSPenShadow[dfSHADOW_STEP];
    static HPEN sPenResizeObject;
    static HBRUSH sBrushResizeObject;
    static HPEN sPenResizeBorder;
    static HBRUSH sBrushResizeBorder;
    static HBRUSH sBrushCellColor[dfNUM_OF_CELL_COLOR_GROUP];
    static HBRUSH sBrushBresenhamLine;

    static HFONT sFontGridInfo;

    // memDC에 wall 그리는 용도의 DC, bitmap
    static HDC shWallDC;
    static HBITMAP shWallBit;
    static int sGridSize_old;
    static int sGridCols_old;


    switch (message)
    {
    case WM_CREATE:
    {
        // create static GDI
        HDC hdc = GetDC(g_hWnd);
        sBrushOld = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
        sPenOld = (HPEN)GetCurrentObject(hdc, OBJ_PEN);
        ReleaseDC(g_hWnd, hdc);

        sBrushBackground = CreateSolidBrush(dfCOLOR_BACKGROUND);
        sPenGrid = CreatePen(PS_SOLID, 1, dfCOLOR_GRID);
        sBrushOpenNode = CreateSolidBrush(dfCOLOR_OPEN_NODE);
        sBrushCloseNode = CreateSolidBrush(dfCOLOR_CLOSE_NODE);
        sBrushStartPoint = CreateSolidBrush(dfCOLOR_START_POINT);
        sBrushEndPoint = CreateSolidBrush(dfCOLOR_END_POINT);
        sBrushWall = (HBRUSH)GetStockObject(dfCOLOR_WALL);
        sPenPath = CreatePen(PS_SOLID, 3, dfCOLOR_PATH_LINE);
        sPenSmoothPath = CreatePen(PS_SOLID, 3, dfCOLOR_SMOOTH_PATH_LINE);

        int r0 = GetRValue(dfCOLOR_BACKGROUND) - 28;   // 그림자 시작 색
        int g0 = GetGValue(dfCOLOR_BACKGROUND) - 25;
        int b0 = GetBValue(dfCOLOR_BACKGROUND) - 23;
        int r1 = GetRValue(dfCOLOR_BACKGROUND);        // 그림자 종료 색
        int g1 = GetGValue(dfCOLOR_BACKGROUND);
        int b1 = GetBValue(dfCOLOR_BACKGROUND);
        for (int i = 0; i < dfSHADOW_STEP; i++)
        {
            int r = r0 + ((r1 - r0) * i / dfSHADOW_STEP);
            int g = g0 + ((g1 - g0) * i / dfSHADOW_STEP);
            int b = b0 + ((b1 - b0) * i / dfSHADOW_STEP);

            arrSPenShadow[i] = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        }

        sPenResizeObject = (HPEN)GetStockObject(BLACK_PEN);
        sBrushResizeObject = (HBRUSH)GetStockObject(WHITE_BRUSH);
        sPenResizeBorder = CreatePen(PS_DOT, 1, dfCOLOR_RESIZE_BORDER);
        sBrushResizeBorder = (HBRUSH)GetStockObject(HOLLOW_BRUSH);

        COLORREF arrRGB[dfNUM_OF_CELL_COLOR_GROUP];
        ggColorSlice(dfNUM_OF_CELL_COLOR_GROUP, arrRGB, 0.3);
        for (int i = 0; i < dfNUM_OF_CELL_COLOR_GROUP; i++)
        {
            sBrushCellColor[i] = CreateSolidBrush(arrRGB[i]);
        }

        sBrushBresenhamLine = CreateSolidBrush(dfCOLOR_BRESENHAM);

        sFontGridInfo = CreateFont(15, 0, 0, 0, 0
            , false, false, false
            , ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY
            , DEFAULT_PITCH | FF_DONTCARE
            , NULL);



        // memDC에 wall 그리는 용도의 DC
        shWallDC = CreateCompatibleDC(g_hDC);
        sGridSize_old = -1;
        sGridCols_old = -1;
    }
    break;

    /* 마우스 왼쪽 클릭 시 */
    case WM_LBUTTONDOWN:
    {
        int xPos = mcGET_MOUSE_X_LPARAM(lParam);
        int yPos = mcGET_MOUSE_Y_LPARAM(lParam);
        g_mouseClickX = xPos;
        g_mouseClickY = yPos;

#ifdef _DEBUG
        printf("WM_LBUTTONDOWN  xPos %d, yPos %d, g_hScrollPos %d, g_vScrollPos %d\n", xPos, yPos, g_hScrollPos, g_vScrollPos);
#endif
        // 현재 마우스 좌표가 그리기영역 위일 경우 click 판정
        if (xPos >= g_painting.ptPos.x && xPos < g_painting.ptPos.x + g_painting.ptSize.x
            && yPos >= g_painting.ptPos.y && yPos < g_painting.ptPos.y + g_painting.ptSize.y)
        {
            g_clickPainting = true;
            int row = mcMOUSE_Y_TO_GRID_ROW(yPos);
            int col = mcMOUSE_X_TO_GRID_COL(xPos);

            // 현재 선택된 toolbar 버튼이 Wall 일 경우 grid 상태를 바꿈
            if (g_tbBtnTool == IDM_TOOLBAR_WALL)
            {
                if (g_arr2Grid[row][col] == dfGRID_WALL)
                {
                    g_erase = true;
                    g_arr2Grid[row][col] = dfGRID_WAY;
                }
                else
                {
                    g_erase = false;
                    g_arr2Grid[row][col] = dfGRID_WALL;
                }
            }

            // 현재 선택된 toolbar 버튼이 Start Point 일 경우 start point 좌표를 설정함
            else if (g_tbBtnTool == IDM_TOOLBAR_STARTPOINT)
            {
                if (row == g_painting.startRow && col == g_painting.startCol)
                {
                    g_painting.startRow = -1;
                    g_painting.startCol = -1;
                }
                else
                {
                    g_painting.startRow = row;
                    g_painting.startCol = col;
                }
            }

            // 현재 선택된 toolbar 버튼이 End Point 일 경우 end point 좌표를 설정함
            else if (g_tbBtnTool == IDM_TOOLBAR_ENDPOINT)
            {

                if (row == g_painting.endRow && col == g_painting.endCol)
                {
                    g_painting.endRow = -1;
                    g_painting.endCol = -1;
                }
                else
                {
                    g_painting.endRow = row;
                    g_painting.endCol = col;
                }
            }

            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }


        int resizeObjectLeft[3] = { g_painting.ptPos.x + g_painting.ptSize.x / 2 - 2, g_painting.ptPos.x + g_painting.ptSize.x, g_painting.ptPos.x + g_painting.ptSize.x };
        int resizeObjectTop[3] = { g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y / 2 - 2 };
        // 마우스가 중간아래 그리기영역 크기조정 object 위에 있을 경우 해당 object 클릭 체크
        if (xPos >= resizeObjectLeft[0] && xPos < resizeObjectLeft[0] + 5
            && yPos >= resizeObjectTop[0] && yPos < resizeObjectTop[0] + 5)
        {
            g_clickResizeVertical = true;
        }
        // 마우스가 오른쪽아래 그리기영역 크기조정 object 위에 있을 경우 해당 object 클릭 체크
        else if (xPos >= resizeObjectLeft[1] && xPos < resizeObjectLeft[1] + 5
            && yPos >= resizeObjectTop[1] && yPos < resizeObjectTop[1] + 5)
        {
            g_clickResizeDiagonal = true;
        }
        // 마우스가 오른쪽중간 그리기영역 크기조정 object 위에 있을 경우 해당 object 클릭 체크
        else if (xPos >= resizeObjectLeft[2] && xPos < resizeObjectLeft[2] + 5
            && yPos >= resizeObjectTop[2] && yPos < resizeObjectTop[2] + 5)
        {
            g_clickResizeHorizontal = true;
        }
    }
    break;


    /* 마우스 왼쪽버튼을 뗐을 때 */
    case WM_LBUTTONUP:
    {
        g_clickPainting = false;

        // 그리기영역 크기조정 object를 클릭 중일 때 마우스를 뗐으면 memory bitmap을 크기에 맞게 다시 만든다.
        if (g_clickResizeVertical || g_clickResizeDiagonal || g_clickResizeHorizontal)
        {
            // 마우스 좌표와 그리기영역 크기를 계산한다.
            int xPos = mcGET_MOUSE_X_LPARAM(lParam);
            int yPos = mcGET_MOUSE_Y_LPARAM(lParam);
            int newSizeX = min(max(xPos - g_painting.ptPos.x, dfPAINT_SIZE_MIN_X), dfPAINT_SIZE_MAX_X);
            int newSizeY = min(max(yPos - g_painting.ptPos.y, dfPAINT_SIZE_MIN_Y), dfPAINT_SIZE_MAX_Y);
            if (g_clickResizeVertical)
                newSizeX = g_painting.ptSize.x;
            if (g_clickResizeHorizontal)
                newSizeY = g_painting.ptSize.y;

            // g_painting 에 크기 저장
            g_painting.SetSize(newSizeX, newSizeY);
            ResizeGrid(); // 그리드 재생성

            // bitmap 다시 만들기
            ResizeWindow();
            ReadjustScroll();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        g_clickResizeVertical = false;
        g_clickResizeHorizontal = false;
        g_clickResizeDiagonal = false;



    }
    break;


    /* 마우스를 움직일 때 */
    case WM_MOUSEMOVE:
    {
        g_isCursorChanged = false;
        g_hCurrentCursor = g_hDefaultCursor;

        int xPos = mcGET_MOUSE_X_LPARAM(lParam);
        int yPos = mcGET_MOUSE_Y_LPARAM(lParam);
        g_mouseX = xPos;
        g_mouseY = yPos;

        // 마우스가 그리기영역에 있을 경우
        if (xPos >= g_painting.ptPos.x && xPos < g_painting.ptPos.x + g_painting.ptSize.x
            && yPos >= g_painting.ptPos.y && yPos < g_painting.ptPos.y + g_painting.ptSize.y)
        {
            // 클릭 중일 경우
            if (g_clickPainting)
            {
                // 현재 toolbar의 wall 버튼이 눌려져 있을 경우
                if (g_tbBtnTool == IDM_TOOLBAR_WALL)
                {
                    int row = mcMOUSE_Y_TO_GRID_ROW(yPos);
                    int col = mcMOUSE_X_TO_GRID_COL(xPos);
                    // grid의 상태를 바꾼다.
                    if (g_erase == true)
                        g_arr2Grid[row][col] = dfGRID_WAY;
                    else
                        g_arr2Grid[row][col] = dfGRID_WALL;

                    InvalidateRect(hWnd, NULL, FALSE);
                }
                // 현재 toolbar의 Bresenham Line 버튼이 눌려져 있을 경우 WM_PAIN 에서 선을 그림
                if (g_tbBtnTool == IDM_TOOLBAR_BRESENHAM_LINE)
                {
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }


        int resizeObjectLeft[3] = { g_painting.ptPos.x + g_painting.ptSize.x / 2 - 2, g_painting.ptPos.x + g_painting.ptSize.x, g_painting.ptPos.x + g_painting.ptSize.x };
        int resizeObjectTop[3] = { g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y / 2 - 2 };
        // 마우스가 중간아래 그리기영역 크기조정 object 위에 있거나, 마우스를 누르고 있을 경우
        if (xPos >= resizeObjectLeft[0] && xPos < resizeObjectLeft[0] + 5
            && yPos >= resizeObjectTop[0] && yPos < resizeObjectTop[0] + 5
            || g_clickResizeVertical == true)
        {
            g_hCurrentCursor = LoadCursor(nullptr, IDC_SIZENS);  // 커서 변경
            g_isCursorChanged = true;
            if (g_clickResizeVertical == true)                    // 마우스를 클릭한채로 움직이면 화면 갱신
                InvalidateRect(hWnd, NULL, FALSE);
        }
        // 마우스가 오른쪽아래 그리기영역 크기조정 object 위에 있을 경우
        else if (xPos >= resizeObjectLeft[1] && xPos < resizeObjectLeft[1] + 5
            && yPos >= resizeObjectTop[1] && yPos < resizeObjectTop[1] + 5
            || g_clickResizeDiagonal == true)
        {
            g_hCurrentCursor = LoadCursor(nullptr, IDC_SIZENWSE);  // 커서 변경
            g_isCursorChanged = true;
            if (g_clickResizeDiagonal == true)                      // 마우스를 클릭한채로 움직이면 화면 갱신
                InvalidateRect(hWnd, NULL, FALSE);
        }
        // 마우스가 오른쪽중간 그리기영역 크기조정 object 위에 있을 경우
        else if (xPos >= resizeObjectLeft[2] && xPos < resizeObjectLeft[2] + 5
            && yPos >= resizeObjectTop[2] && yPos < resizeObjectTop[2] + 5
            || g_clickResizeHorizontal == true)
        {
            g_hCurrentCursor = LoadCursor(nullptr, IDC_SIZEWE);  // 커서 변경
            g_isCursorChanged = true;
            if (g_clickResizeHorizontal == true)                 // 마우스를 클릭한채로 움직이면 화면 갱신
                InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    /* 마우스 휠을 굴릴 때 */
    case WM_MOUSEWHEEL:
    {
        int fwKeys = GET_KEYSTATE_WPARAM(wParam);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        // 현재 Ctrl 키가 눌려져 있다면 grid 크기조정
        if (fwKeys & MK_CONTROL)
        {
            // 마우스 휠을 위로 굴렸을 때 grid 크기 + 1
            if (zDelta > 0)
            {
                // grid 크기 + 1
                int newGridSize = g_painting.gridSize + 1;
                if (newGridSize > dfGRID_SIZE_MAX)
                    break;

                int newSizeX = newGridSize * g_painting.gridCols;
                int newSizeY = newGridSize * g_painting.gridRows;
                g_painting.SetSize(newGridSize, newSizeX, newSizeY);
                ResizeGrid(); // 그리드 재생성

                // bitmap 다시 만들기, 스크롤바 조정
                ResizeWindow();
                ReadjustScroll();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            // 마우스 휠을 아래로 굴렸을 때 grid 크기 - 1
            else if (zDelta < 0)
            {
                // grid 크기 - 1
                int newGridSize = g_painting.gridSize - 1;
                if (newGridSize < dfGRID_SIZE_MIN)
                    break;

                int newSizeX = newGridSize * g_painting.gridCols;
                int newSizeY = newGridSize * g_painting.gridRows;
                g_painting.SetSize(newGridSize, newSizeX, newSizeY);
                ResizeGrid(); // 그리드 재생성

                // bitmap 다시 만들기, 스크롤바 조정
                ResizeWindow();
                ReadjustScroll();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }

        // 현재 Ctrl 키가 눌려져 있지 않다면 수직 스크롤링
        else
        {
            // 마우스 휠을 위로 굴렸을 때 위로 스크롤링
            if (zDelta > 0)
            {
                PostMessage(g_hWnd, WM_VSCROLL, (WPARAM)MAKELONG(SB_LINEUP, 0), (LPARAM)NULL);
            }
            // 마우스 휠을 위로 굴렸을 때 아래로 스크롤링
            else if (zDelta < 0)
            {
                PostMessage(g_hWnd, WM_VSCROLL, (WPARAM)MAKELONG(SB_LINEDOWN, 0), (LPARAM)NULL);
            }
        }
    }
    break;



    /* 창의 크기가 바뀔 때 */
    case WM_SIZE:
    {
        // mem bitmap, 스크롤, toolbar를 화면 크기에 맞게 다시 만든다.
        ResizeWindow();
        ReadjustScroll();


        // invalid rect
        InvalidateRect(hWnd, NULL, FALSE);
        SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);

    }
    break;


    /* 커서 교체 (마우스를 움직일 때마다 시행됨) */
    case WM_SETCURSOR:
    {
        // 커서가 변경되어야 할 때만 custom cursor로 변경함
        if (g_isCursorChanged)
        {
            SetCursor(g_hCurrentCursor);
        }
        // 그렇지 않으면 윈도우 기본 동작에 맡김
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }


    /* menu 또는 toolbar 클릭 */
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId)
        {
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;

            /* toolbar :: 새로그리기 버튼 클릭 */
        case IDM_TOOLBAR_NEW:
            ClearGrid();
            g_AStarSearch.Clear();
            g_jumpPointSearch.Clear();
            InvalidateRect(hWnd, NULL, FALSE);
            break;

            /* toolbar :: 크기조정 버튼 클릭 */
        case IDM_TOOLBAR_SIZE_ADJ:
        {
            // IDD_SIZE_ADJ 다이얼로그를 띄운다.
            // IDD_SIZE_ADJ 다이얼로그 에서는 g_painting 구조체의 값을 변경한다.
            bool isOk = (bool)DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SIZE_ADJ), hWnd, WndProcDialogSizeAdj);
            // 다이얼로그에서 OK를 눌렀을 경우 memory bitmap과 grid를 새로운 크기로 다시 만든다.
            if (isOk)
            {
                ResizeGrid();
                ResizeWindow();
                ReadjustScroll();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;

        /* toolbar :: Wall 그리기 버튼 클릭 */
        case IDM_TOOLBAR_WALL:
            g_tbBtnTool = IDM_TOOLBAR_WALL;
            break;
            /* toolbar :: StartPoint 그리기 버튼 클릭 */
        case IDM_TOOLBAR_STARTPOINT:
            g_tbBtnTool = IDM_TOOLBAR_STARTPOINT;
            break;
            /* toolbar :: EndPoint 그리기 버튼 클릭 */
        case IDM_TOOLBAR_ENDPOINT:
            g_tbBtnTool = IDM_TOOLBAR_ENDPOINT;
            break;
            /* toolbar :: Bresenham Line 그리기 버튼 클릭 */
        case IDM_TOOLBAR_BRESENHAM_LINE:
            g_tbBtnTool = IDM_TOOLBAR_BRESENHAM_LINE;
            break;


            /* toolbar :: A* Search 버튼 클릭 */
        case IDM_TOOLBAR_ASTAR_SEARCH:
            g_timerFrequency = 55;
            g_timerFrequencyIncrease = 20;
            g_tbBtnAlgorithm = IDM_TOOLBAR_ASTAR_SEARCH;
            break;
            /* toolbar :: Jump Point Search 버튼 클릭 */
        case IDM_TOOLBAR_JUMP_POINT_SEARCH:
            g_timerFrequency = 300;
            g_timerFrequencyIncrease = 100;
            g_tbBtnAlgorithm = IDM_TOOLBAR_JUMP_POINT_SEARCH;
            break;


            /* toolbar :: Search 버튼 클릭 */
        case IDM_TOOLBAR_SEARCH:
        {
            // 시작점, 도착점 존재 여부 체크
            if (g_painting.startCol < 0
                || g_painting.startRow < 0
                || g_painting.startCol >= g_painting.gridCols
                || g_painting.startRow >= g_painting.gridRows
                || g_painting.endCol < 0
                || g_painting.endRow < 0
                || g_painting.endCol >= g_painting.gridCols
                || g_painting.endRow >= g_painting.gridRows)
            {
                break;
            }

            if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
            {
                // A* search 파라미터 설정
                g_AStarSearch.SetParam(g_painting.startRow, g_painting.startCol
                    , g_painting.endRow, g_painting.endCol
                    , g_arr2Grid
                    , g_painting.gridRows
                    , g_painting.gridCols
                    , g_painting.gridSize);

                // step by step 길찾기 시작
                g_AStarSearch.SearchStepByStep();
            }
            else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
            {
                // jump point search 파라미터 설정
                g_jumpPointSearch.SetParam(g_painting.startRow, g_painting.startCol
                    , g_painting.endRow, g_painting.endCol
                    , g_arr2Grid
                    , g_painting.gridRows
                    , g_painting.gridCols);

                // step by step 길찾기 시작
                g_jumpPointSearch.SearchStepByStep();
            }

            // 타이머 설정
            SetTimer(g_hWnd, dfTIMER_ID, max(0, g_timerFrequency + g_searchSpeedLevel * g_timerFrequencyIncrease), NULL);
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        /* toolbar :: F-Value 버튼 클릭 */
        case IDM_TOOLBAR_FVALUE:
        {
            // F-Value 버튼 상태 확인
            LRESULT result = SendMessage(g_hToolbar, TB_GETSTATE, (WPARAM)IDM_TOOLBAR_FVALUE, 0);
            // 버튼이 check 상태인지 여부 기록
            g_tbBtnIsFValueChecked = result & TBSTATE_CHECKED;
            SleepEx(0, TRUE);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

        /* toolbar :: Speed Up 버튼 클릭 */
        case IDM_TOOLBAR_SPEEDUP:
        {
            g_searchSpeedLevel--;
            if (g_searchSpeedLevel < -3)
                g_searchSpeedLevel = -3;

            g_bResetTimer = true;
        }
        break;

        /* toolbar :: Speed Down 버튼 클릭 */
        case IDM_TOOLBAR_SPEEDDOWN:
        {
            g_searchSpeedLevel++;
            if (g_searchSpeedLevel > 10)
                g_searchSpeedLevel = 10;

            g_bResetTimer = true;
        }
        break;

        /* toolbar :: Go to End 버튼 클릭 */
        case IDM_TOOLBAR_GOTOEND:
        {
            // 타이머 kill
            KillTimer(g_hWnd, dfTIMER_ID);

            // 길찾기가 끝날 때까지 길찾기 수행
            if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
            {
                while (!g_AStarSearch.SearchNextStep())
                {
                    continue;
                }
            }
            else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
            {
                while (!g_jumpPointSearch.SearchNextStep())
                {
                    continue;
                }
            }

            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;


        /* toolbar :: Make Maze 버튼 클릭 */
        case IDM_TOOLBAR_MAKE_MAZE:
        {
            g_AStarSearch.Clear();
            g_jumpPointSearch.Clear();

            // maze 생성
            MakeMaze(g_painting.gridCols, g_painting.gridRows, g_arr2Grid);

            // start, end point 지정
            int startRow = rand() % g_painting.gridRows;
            int startCol = rand() % g_painting.gridCols;
            int endRow = rand() % g_painting.gridRows;
            int endCol = rand() % g_painting.gridCols;
            while (g_arr2Grid[startRow][startCol] == dfGRID_WALL)
            {
                startRow = rand() % g_painting.gridRows;
                startCol = rand() % g_painting.gridCols;
            }
            while (g_arr2Grid[endRow][endCol] == dfGRID_WALL
                || (endRow == startRow && endCol == startCol))
            {
                endRow = rand() % g_painting.gridRows;
                endCol = rand() % g_painting.gridCols;
            }
            g_painting.startCol = startCol;
            g_painting.startRow = startRow;
            g_painting.endCol = endCol;
            g_painting.endRow = endRow;


            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;


        /* toolbar :: Make Cave 버튼 클릭 */
        case IDM_TOOLBAR_MAKE_CAVE:
        {
            g_AStarSearch.Clear();
            g_jumpPointSearch.Clear();

            // cave 생성
            CCaveGenerator cave;
            cave.Generate(g_arr2Grid, g_painting.gridCols, g_painting.gridRows, 0.45f, 3);

            // start, end point 지정
            g_numOfCaveWays = cave.GetNumOfWays();
            if (g_numOfCaveWays > 1)
            {
                int startRow = rand() % g_painting.gridRows;
                int startCol = rand() % g_painting.gridCols;
                int endRow = rand() % g_painting.gridRows;
                int endCol = rand() % g_painting.gridCols;
                while (g_arr2Grid[startRow][startCol] == dfGRID_WALL)
                {
                    startRow = rand() % g_painting.gridRows;
                    startCol = rand() % g_painting.gridCols;
                }
                while (g_arr2Grid[endRow][endCol] == dfGRID_WALL
                    || (endRow == startRow && endCol == startCol))
                {
                    endRow = rand() % g_painting.gridRows;
                    endCol = rand() % g_painting.gridCols;
                }
                g_painting.startCol = startCol;
                g_painting.startRow = startRow;
                g_painting.endCol = endCol;
                g_painting.endRow = endRow;
            }
            else
            {
                g_painting.startCol = -1;
                g_painting.startRow = -1;
                g_painting.endCol = -1;
                g_painting.endRow = -1;
            }

            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;


        /* toolbar :: Make Random 버튼 클릭 */
        case IDM_TOOLBAR_MAKE_RANDOM:
        {
            g_AStarSearch.Clear();
            g_jumpPointSearch.Clear();

            // random wall 생성
            for (int row = 0; row < g_painting.gridRows; row++)
            {
                for (int col = 0; col < g_painting.gridCols; col++)
                {
                    g_arr2Grid[row][col] = rand() > 16384 ? dfGRID_WAY : dfGRID_WALL;
                }
            }

            // start, end point 지정
            g_painting.startCol = rand() % g_painting.gridCols;
            g_painting.startRow = rand() % g_painting.gridRows;
            g_painting.endCol = rand() % g_painting.gridCols;
            g_painting.endRow = rand() % g_painting.gridRows;
            while (g_painting.endRow == g_painting.startRow
                && g_painting.endCol == g_painting.startCol)
            {
                g_painting.endRow = rand() % g_painting.gridRows;
                g_painting.endCol = rand() % g_painting.gridCols;
            }

            // 길뚫기
            g_arr2Grid[g_painting.startRow][g_painting.startCol] = dfGRID_WAY;
            g_arr2Grid[g_painting.endRow][g_painting.endCol] = dfGRID_WAY;
            int numOfWayPoint = (int)(pow(log(min(g_painting.gridRows, g_painting.gridCols)), 2.0) / 2.0);
            CBresenhamPath BPath;
            POINT ptStart;
            POINT ptEnd;
            for (int i = 0; i < numOfWayPoint; i++)
            {
                if (i == 0)
                {
                    ptStart.x = g_painting.startCol;
                    ptStart.y = g_painting.startRow;
                }
                else
                {
                    ptStart.x = ptEnd.x;
                    ptStart.y = ptEnd.y;
                }

                if (i == numOfWayPoint - 1)
                {
                    ptEnd.x = g_painting.endCol;
                    ptEnd.y = g_painting.endRow;
                }
                else
                {
                    ptEnd.x = rand() % g_painting.gridCols;
                    ptEnd.y = rand() % g_painting.gridRows;
                    while (ptEnd.x == ptStart.x && ptEnd.y == ptStart.y)
                    {
                        ptEnd.x = rand() % g_painting.gridCols;
                        ptEnd.y = rand() % g_painting.gridRows;
                    }
                }

                BPath.SetParam(ptStart, ptEnd);
                while (BPath.Next())
                {
                    g_arr2Grid[BPath.GetY()][BPath.GetX()] = dfGRID_WAY;
                }
            }


            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;


        /* toolbar :: Run Test 버튼 클릭 */
        case IDM_TOOLBAR_RUN_TEST:
            static int sTestCount;                // 테스트 loop 제어용 카운트
            static int sTestTotalSearchCount;     // 길찾기를 수행한 횟수
            static int sTestPrevSearchCount;      // 이전 테스트 진행상황 출력 때의 길찾기 수행 횟수
            static ULONGLONG sTestStartTime;      // 테스트 시작 시간
            static ULONGLONG sTestPrevPrintTime;  // 이전 테스트 진행상황 출력 시간
            static LARGE_INTEGER sllFrequency;
            static LARGE_INTEGER sllTotalTime;  // 길찾기에 소요된 총 시간
            {
                QueryPerformanceFrequency(&sllFrequency);
                sllTotalTime.QuadPart = 28185624038i64;//0;

                if (g_isTestRunning == false)
                {
                    g_isTestRunning = true;
                    g_isTestDoRendering = true;
                    g_isPrintTestInfo = true;
                    sTestCount = 0;
                    sTestTotalSearchCount = 8598420;// -1;
                    sTestPrevSearchCount = 0;

                    // 콘솔 생성
                    AllocConsole();
                    FILE* stream;
                    freopen_s(&stream, "CONIN$", "r", stdin);
                    freopen_s(&stream, "CONOUT$", "w", stdout);
                    freopen_s(&stream, "CONOUT$", "w", stderr);

                    // 콘솔에서 키보드 입력을 감지하는 스레드 실행
                    _beginthreadex(NULL, 0, ThreadProcQuitRunTest, NULL, 0, NULL);
                }

                g_testRandomSeed = (unsigned int)time(0);
                srand(g_testRandomSeed);

                sTestStartTime = GetTickCount64() - (27360 * 1000); // GetTickCount64();
                sTestPrevPrintTime = sTestStartTime;


                // 첫 맵 생성
                PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_MAKE_CAVE, 0), (LPARAM)NULL);

                // IDM_TOOLBAR_RUN_TEST_INTERNAL 메시지 생성
                PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_RUN_TEST_INTERNAL, 0), (LPARAM)NULL);
            }
            break;

        /* toolbar :: Run Test 버튼 클릭 후 발생되는 테스트 무한반복 메시지 */
        case IDM_TOOLBAR_RUN_TEST_INTERNAL:
        {
            if (g_isTestRunning)
            {
                sTestCount++;
                sTestTotalSearchCount++;

                // 1초마다 출력
                if (GetTickCount64() - sTestPrevPrintTime > 1000)
                {
                    printf("Total test time: %lld sec, Test per sec: %d, Total search count: %d, average search time(ms): %f\n"
                        , (GetTickCount64() - sTestStartTime) / 1000
                        , sTestTotalSearchCount - sTestPrevSearchCount
                        , sTestTotalSearchCount
                        , (double)sllTotalTime.QuadPart / (double)sTestTotalSearchCount / (double)sllFrequency.QuadPart * 1000.0);

                    sTestPrevPrintTime = GetTickCount64();
                    sTestPrevSearchCount = sTestTotalSearchCount;
                }


                // 이전 결과 확인
                bool searchResult = true;
                if (sTestTotalSearchCount != 8598421)//0)
                {
                    if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
                    {
                        searchResult = g_AStarSearch.IsSucceeded();
                    }
                    else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
                    {
                        searchResult = g_jumpPointSearch.IsSucceeded();
                    }
                }

                // 목적지를 찾지 못했다면 오류이므로 테스트 종료
                if (searchResult == false)
                {
                    ReadjustScroll();
                    PostMessage(g_hWnd, WM_PAINT, (WPARAM)MAKEWPARAM(0, 0), (LPARAM)NULL);
                    g_isTestRunning = false;
                    printf("!!ERROR!! Cannot find the destination.\n");
                    printf("Seed : %u, Total search : %d\n", g_testRandomSeed, sTestTotalSearchCount);

                    break;
                }

                // 만약 이전에 생성한 cave에서 way 수가 2보다 작을 경우 다시 만듬
                if (g_numOfCaveWays < 2)
                {
                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_MAKE_CAVE, 0), (LPARAM)NULL);
                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_RUN_TEST_INTERNAL, 0), (LPARAM)NULL);
                    sTestTotalSearchCount--;
                    break;
                }

                // 맵 생성, 그리드 크기 조정
                if (sTestCount == 50)
                {
                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_MAKE_MAZE, 0), (LPARAM)NULL);
                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_RUN_TEST_INTERNAL, 0), (LPARAM)NULL);
                    sTestTotalSearchCount--;
                    break;
                }
                else if (sTestCount == 80)
                {
                    int gridSize = min(40, g_painting.gridSize);
                    int randomWidth = (rand() % 130 + 20) * gridSize;
                    int randomHeight = (rand() % 80 + 20) * gridSize;

                    g_painting.SetSize(gridSize, randomWidth, randomHeight);
                    ResizeGrid();
                    ResizeWindow();
                    if (g_isTestDoRendering)
                        ReadjustScroll();

                    sTestCount = -1;

                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_MAKE_CAVE, 0), (LPARAM)NULL);
                    PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_RUN_TEST_INTERNAL, 0), (LPARAM)NULL);
                    sTestTotalSearchCount--;
                    break;
                }
                //PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_MAKE_RANDOM, 0), (LPARAM)NULL);

                // start, end point 지정
                int startRow = rand() % g_painting.gridRows;
                int startCol = rand() % g_painting.gridCols;
                int endRow = rand() % g_painting.gridRows;
                int endCol = rand() % g_painting.gridCols;
                while (g_arr2Grid[startRow][startCol] == dfGRID_WALL)
                {
                    startRow = rand() % g_painting.gridRows;
                    startCol = rand() % g_painting.gridCols;
                }
                while (g_arr2Grid[endRow][endCol] == dfGRID_WALL
                    || (endRow == startRow && endCol == startCol))
                {
                    endRow = rand() % g_painting.gridRows;
                    endCol = rand() % g_painting.gridCols;
                }
                g_painting.startCol = startCol;
                g_painting.startRow = startRow;
                g_painting.endCol = endCol;
                g_painting.endRow = endRow;
                

                // 실패 테스트
                //if (sTestTotalSearchCount == 10000)
                //{
                //    g_arr2Grid[endRow + 1][endCol + 1] = dfGRID_WALL;
                //    g_arr2Grid[endRow + 1][endCol] = dfGRID_WALL;
                //    g_arr2Grid[endRow + 1][endCol - 1] = dfGRID_WALL;
                //    g_arr2Grid[endRow][endCol + 1] = dfGRID_WALL;
                //    g_arr2Grid[endRow][endCol - 1] = dfGRID_WALL;
                //    g_arr2Grid[endRow - 1][endCol + 1] = dfGRID_WALL;
                //    g_arr2Grid[endRow - 1][endCol] = dfGRID_WALL;
                //    g_arr2Grid[endRow - 1][endCol - 1] = dfGRID_WALL;
                //}

                // search 
                LARGE_INTEGER llSearchStartTime;
                LARGE_INTEGER llSearchEndTime;
                QueryPerformanceCounter(&llSearchStartTime);
                if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
                {
                    // A* search 파라미터 설정
                    g_AStarSearch.SetParam(g_painting.startRow, g_painting.startCol
                        , g_painting.endRow, g_painting.endCol
                        , g_arr2Grid
                        , g_painting.gridRows
                        , g_painting.gridCols
                        , g_painting.gridSize);

                    // 길찾기
                    g_AStarSearch.Search();
                }
                else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
                {
                    // jump point search 파라미터 설정
                    g_jumpPointSearch.SetParam(g_painting.startRow, g_painting.startCol
                        , g_painting.endRow, g_painting.endCol
                        , g_arr2Grid
                        , g_painting.gridRows
                        , g_painting.gridCols);

                    // 길찾기
                    g_jumpPointSearch.Search();
                }
                QueryPerformanceCounter(&llSearchEndTime);
                sllTotalTime.QuadPart += llSearchEndTime.QuadPart - llSearchStartTime.QuadPart;

                // rendering
                if (g_isTestDoRendering)
                {
                    PostMessage(g_hWnd, WM_PAINT, (WPARAM)MAKEWPARAM(0, 0), (LPARAM)NULL);
                }

                // 계속 테스트 수행
                PostMessage(g_hWnd, WM_COMMAND, (WPARAM)MAKEWPARAM(IDM_TOOLBAR_RUN_TEST_INTERNAL, 0), (LPARAM)NULL);
            }

        }
        break;



        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }


    }
    break;




    /* 키보드 입력 */
    case WM_KEYDOWN:
    {

        switch (wParam)
        {
            // 방향키를 누르면 스크롤 이동
        case VK_LEFT:
            PostMessage(g_hWnd, WM_HSCROLL, (WPARAM)MAKELONG(SB_LINEUP, 0), (LPARAM)NULL);
            break;

        case VK_RIGHT:
            PostMessage(g_hWnd, WM_HSCROLL, (WPARAM)MAKELONG(SB_LINEDOWN, 0), (LPARAM)NULL);
            break;

        case VK_UP:
            PostMessage(g_hWnd, WM_VSCROLL, (WPARAM)MAKELONG(SB_LINEUP, 0), (LPARAM)NULL);
            break;

        case VK_DOWN:
            PostMessage(g_hWnd, WM_VSCROLL, (WPARAM)MAKELONG(SB_LINEDOWN, 0), (LPARAM)NULL);
            break;
        }
    }
    break;









    /* 수평 스크롤바 이벤트 */
    case WM_HSCROLL:
    {
        // 수평 스크롤바 현재 정보 얻기
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(g_hWnd, SB_HORZ, &si);

        // 스크롤바 현재 위치 저장
        g_hScrollPos = si.nPos;

        // 스크롤바 이벤트 처리
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:  // 스크롤바 왼쪽 화살표를 누름
            si.nPos -= 15;
            break;

        case SB_LINEDOWN:  // 스크롤바 오른쪽 화살표를 누름
            si.nPos += 15;
            break;

        case SB_PAGEUP:  // 스크롤바 왼쪽 shaft를 누름
            si.nPos -= si.nPage;
            break;

        case SB_PAGEDOWN:  // 스크롤바 오른쪽 shaft를 누름
            si.nPos += si.nPage;
            break;

        case SB_THUMBPOSITION:  // 스크롤바를 잡고 드래그함
            si.nPos = si.nTrackPos;
            break;
        }
        si.nPos = min(max(0, si.nPos), g_hScrollMax);  // min, max 조정

        // 스크롤바의 위치를 설정하고 정보를 다시 얻음
        si.fMask = SIF_POS;
        SetScrollInfo(g_hWnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(g_hWnd, SB_HORZ, &si);

        // 만약 스크롤바의 위치가 변경됐다면 윈도우와 스크롤을 갱신한다.
        if (si.nPos != g_hScrollPos)
        {
            ScrollWindow(g_hWnd, 0, g_hScrollPos - si.nPos, NULL, NULL);

            //UpdateWindow(g_hWnd);
            g_isRenderFromScrolling = true;  // 렌더링이 스크롤링에 의한 것임을 표시
            InvalidateRect(hWnd, NULL, FALSE);
            SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
        }
        // 현재 스크롤바 위치 갱신
        g_hScrollPos = si.nPos;

    }
    break;



    /* 수직 스크롤바 이벤트 */
    case WM_VSCROLL:
    {
        // 수직 스크롤바 현재 정보 얻기
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(g_hWnd, SB_VERT, &si);

        // 스크롤바 현재 위치 저장
        g_vScrollPos = si.nPos;

        // 스크롤바 이벤트 처리
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:  // 스크롤바 위 화살표를 누름
            si.nPos -= 15;
            break;

        case SB_LINEDOWN:  // 스크롤바 아래 화살표를 누름
            si.nPos += 15;
            break;

        case SB_PAGEUP:  // 스크롤바 위 shaft를 누름
            si.nPos -= si.nPage;
            break;

        case SB_PAGEDOWN:  // 스크롤바 아래 shaft를 누름
            si.nPos += si.nPage;
            break;

        case SB_THUMBPOSITION:  // 스크롤바를 잡고 드래그함
            si.nPos = si.nTrackPos;
            break;
        }
        si.nPos = min(max(0, si.nPos), g_vScrollMax);  // min, max 조정

        // 스크롤바의 위치를 설정하고 정보를 다시 얻음
        si.fMask = SIF_POS;
        SetScrollInfo(g_hWnd, SB_VERT, &si, TRUE);
        GetScrollInfo(g_hWnd, SB_VERT, &si);

        // 만약 스크롤바의 위치가 변경됐다면 윈도우와 스크롤을 갱신한다.
        if (si.nPos != g_vScrollPos)
        {
            ScrollWindow(g_hWnd, 0, g_vScrollPos - si.nPos, NULL, NULL);
            //UpdateWindow(g_hWnd);
            g_isRenderFromScrolling = true;  // 렌더링이 스크롤링에 의한 것임을 표시
            InvalidateRect(hWnd, NULL, FALSE);
            SendMessage(g_hToolbar, TB_AUTOSIZE, 0, 0);
        }
        // 현재 스크롤바 위치 갱신
        g_vScrollPos = si.nPos;

    }
    break;



    /* search 타이머 */
    case WM_TIMER:
    {
        // speed up 또는 speed down 버튼을 눌러 타이머를 다시설정해야함
        if (g_bResetTimer)
        {

            SetTimer(g_hWnd, dfTIMER_ID, max(0, g_timerFrequency + g_searchSpeedLevel * g_timerFrequencyIncrease), NULL);
            g_bResetTimer = false;
        }

        // 길찾기 다음 step 실행
        bool isCompleted = false;
        if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
        {
            isCompleted = g_AStarSearch.SearchNextStep();
        }
        else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
        {
            isCompleted = g_jumpPointSearch.SearchNextStep();
        }


        // 완료되면 타이머 kill
        if (isCompleted)
        {
            KillTimer(g_hWnd, dfTIMER_ID);
        }

        InvalidateRect(hWnd, NULL, FALSE);
    }
    break;



    /* WM_PAINT */
    case WM_PAINT:
    {
        // 스크롤링에 의한 렌더링이 아닐 경우 전체를 다시 그린다.
        if (!g_isRenderFromScrolling)
        {
            /* 그리기 영역 초기화 */
            PatBlt(g_hMemDC, g_painting.ptPos.x, g_painting.ptPos.y, g_painting.ptSize.x, g_painting.ptSize.y, WHITENESS);


            /* 격자 그리기 */
            SelectObject(g_hMemDC, sPenGrid);
            for (int col = 0; col <= g_painting.gridCols; col++)
            {
                MoveToEx(g_hMemDC, col * g_painting.gridSize + g_painting.ptPos.x, g_painting.ptPos.y, NULL);
                LineTo(g_hMemDC, col * g_painting.gridSize + g_painting.ptPos.x, g_painting.ptSize.y + g_painting.ptPos.y);
            }
            for (int row = 0; row <= g_painting.gridRows; row++)
            {
                MoveToEx(g_hMemDC, g_painting.ptPos.x, row * g_painting.gridSize + g_painting.ptPos.y, NULL);
                LineTo(g_hMemDC, g_painting.ptSize.x + g_painting.ptPos.x, row * g_painting.gridSize + g_painting.ptPos.y);
            }

            /* 노드 정보 얻기 */
            // A* 노드 정보 얻기
            auto umapAStarNodeInfo = g_AStarSearch.GetNodeInfo();

            // jump point search 노드 정보 얻기
            auto mmapJPSOpenList = g_jumpPointSearch.GetOpenList();
            auto vecJPSCloseList = g_jumpPointSearch.GetCloseList();
            decltype(vecJPSCloseList) vecJPSNodeInfo;
            vecJPSNodeInfo.reserve(mmapJPSOpenList.size() + vecJPSCloseList.size());
            for (auto iter = mmapJPSOpenList.begin(); iter != mmapJPSOpenList.end(); ++iter)
            {
                vecJPSNodeInfo.push_back((*iter).second);
            }
            for (size_t i = 0; i < vecJPSCloseList.size(); i++)
            {
                vecJPSNodeInfo.push_back(vecJPSCloseList[i]);
            }



            /* 길찾기 결과 그리기 */
            // A* search 결과 그리기
            if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
            {
                // A* 노드 정보 얻기

                // open 노드, close 노드 색칠하기
                auto iterNodeInfo = umapAStarNodeInfo.begin();
                for (; iterNodeInfo != umapAStarNodeInfo.end(); ++iterNodeInfo)
                {
                    const AStarNode* pNode = iterNodeInfo->second;
                    int col = pNode->_x;
                    int row = pNode->_y;
                    if (col < 0 || col > g_painting.gridCols || row < 0 || row > g_painting.gridRows)
                    {
                        continue;;
                    }

                    if (pNode->_type == eAStarNodeType::OPEN)
                    {
                        SelectObject(g_hMemDC, sBrushOpenNode);
                    }
                    else if (pNode->_type == eAStarNodeType::CLOSE)
                    {
                        SelectObject(g_hMemDC, sBrushCloseNode);
                    }
                    Rectangle(g_hMemDC
                        , col * g_painting.gridSize + g_painting.ptPos.x
                        , row * g_painting.gridSize + g_painting.ptPos.y
                        , (col + 1) * g_painting.gridSize + g_painting.ptPos.x
                        , (row + 1) * g_painting.gridSize + g_painting.ptPos.y);
                }
            }
            // Jump Point Search 결과 그리기
            else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
            {
                // cell 색칠
                const unsigned char* const* arr2CellColorGroup = g_jumpPointSearch.GetCellColorGroup();
                for (int row = 0; row < g_jumpPointSearch.GetGridRows(); row++)
                {
                    for (int col = 0; col < g_jumpPointSearch.GetGridCols(); col++)
                    {
                        if (arr2CellColorGroup[row][col] != 0)
                        {
                            SelectObject(g_hMemDC, sBrushCellColor[(arr2CellColorGroup[row][col] - 1) % dfNUM_OF_CELL_COLOR_GROUP]);
                            Rectangle(g_hMemDC
                                , col * g_painting.gridSize + g_painting.ptPos.x
                                , row * g_painting.gridSize + g_painting.ptPos.y
                                , (col + 1) * g_painting.gridSize + g_painting.ptPos.x
                                , (row + 1) * g_painting.gridSize + g_painting.ptPos.y);
                        }
                    }
                }

                // open 노드, close 노드 색칠하기
                for (size_t i = 0; i < vecJPSNodeInfo.size(); i++)
                {
                    const JPSNode* pNode = vecJPSNodeInfo[i];
                    int col = pNode->_x;
                    int row = pNode->_y;
                    if (col < 0 || col > g_painting.gridCols || row < 0 || row > g_painting.gridRows)
                    {
                        continue;;
                    }

                    if (pNode->_type == eJPSNodeType::OPEN)
                    {
                        SelectObject(g_hMemDC, sBrushOpenNode);
                    }
                    else if (pNode->_type == eJPSNodeType::CLOSE)
                    {
                        SelectObject(g_hMemDC, sBrushCloseNode);
                    }
                    Rectangle(g_hMemDC
                        , col * g_painting.gridSize + g_painting.ptPos.x
                        , row * g_painting.gridSize + g_painting.ptPos.y
                        , (col + 1) * g_painting.gridSize + g_painting.ptPos.x
                        , (row + 1) * g_painting.gridSize + g_painting.ptPos.y);
                }
            }

            /* start point 색칠하기 */
            if (g_painting.startCol >= 0 || g_painting.startRow >= 0)
            {
                SelectObject(g_hMemDC, sBrushStartPoint);
                Rectangle(g_hMemDC
                    , g_painting.startCol * g_painting.gridSize + g_painting.ptPos.x
                    , g_painting.startRow * g_painting.gridSize + g_painting.ptPos.y
                    , (g_painting.startCol + 1) * g_painting.gridSize + g_painting.ptPos.x
                    , (g_painting.startRow + 1) * g_painting.gridSize + g_painting.ptPos.y);
            }

            /* end point 색칠하기 */
            if (g_painting.endCol >= 0 || g_painting.endRow >= 0)
            {
                SelectObject(g_hMemDC, sBrushEndPoint);
                Rectangle(g_hMemDC
                    , g_painting.endCol * g_painting.gridSize + g_painting.ptPos.x
                    , g_painting.endRow * g_painting.gridSize + g_painting.ptPos.y
                    , (g_painting.endCol + 1) * g_painting.gridSize + g_painting.ptPos.x
                    , (g_painting.endRow + 1) * g_painting.gridSize + g_painting.ptPos.y);
            }


            /* wall 색칠하기 */
            // 하나의 row 전체를 커버하는 wall bitmap을 만든다.
            if (g_painting.gridSize != sGridSize_old || g_painting.gridCols != sGridCols_old)
            {
                shWallBit = CreateCompatibleBitmap(g_hDC, g_painting.gridSize * g_painting.gridCols, g_painting.gridSize);
                HBITMAP hWallBit_old = (HBITMAP)SelectObject(shWallDC, shWallBit);
                DeleteObject(hWallBit_old);

                SelectObject(shWallDC, sPenGrid);
                SelectObject(shWallDC, sBrushWall);
                for (int col = 0; col < g_painting.gridCols; col++)
                {
                    Rectangle(shWallDC
                        , col * g_painting.gridSize
                        , 0
                        , (col + 1) * g_painting.gridSize
                        , g_painting.gridSize);
                }

                sGridSize_old = g_painting.gridSize;
                sGridCols_old = g_painting.gridCols;
            }

            // 연속된 wall을 wall bitmap에서 복사해 붙여넣는다.
            int wallStartCol = -1;
            for (int row = 0; row < g_painting.gridRows; row++)
            {
                for (int col = 0; col < g_painting.gridCols; col++)
                {
                    if (g_arr2Grid[row][col] == dfGRID_WALL)
                    {
                        // col이 처음이 아니고 col 이전이 WAY 인 경우,
                        // 또는 col이 처음인 경우 wall start
                        if ((col != 0 && g_arr2Grid[row][col - 1] == dfGRID_WAY)
                            || col == 0)
                        {
                            if(wallStartCol == -1)
                                wallStartCol = col;
                        }
                            
                        // col이 마지막이 아니고 col 다음이 WAY 인 경우
                        // 또는 col이 마지막인 경우 wall end, painting
                        if ((col != g_painting.gridCols - 1 && g_arr2Grid[row][col + 1] == dfGRID_WAY)
                            || col == g_painting.gridCols - 1)
                        {
                            BitBlt(g_hMemDC
                                , wallStartCol * g_painting.gridSize + g_painting.ptPos.x
                                , row * g_painting.gridSize + g_painting.ptPos.y
                                , (col + 1) * g_painting.gridSize + g_painting.ptPos.x
                                , (row + 1) * g_painting.gridSize + g_painting.ptPos.y
                                , shWallDC
                                , g_painting.gridSize* (g_painting.gridCols - (col + 1 - wallStartCol))
                                , 0
                                , SRCCOPY);

                            wallStartCol = -1;
                        }
                    }

                }
            }


            /* 길찾기 경로 그리기 */
            std::vector<POINT> vecPath;
            if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
            {
                vecPath = g_AStarSearch.GetPath();
            }
            else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
            {
                vecPath = g_jumpPointSearch.GetPath();
            }

            if (vecPath.size() > 0)
            {
                SelectObject(g_hMemDC, sPenPath);
                for (int i = 0; i < vecPath.size() - 1; i++)
                {
                    MoveToEx(g_hMemDC
                        , vecPath[i].x * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.x
                        , vecPath[i].y * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.y, NULL);
                    LineTo(g_hMemDC
                        , vecPath[i + 1].x * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.x
                        , vecPath[i + 1].y * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.y);
                }
            }


            /* Bresenham 알고리즘으로 보정된 경로 그리기 */
            std::vector<POINT> vecSmoothPath;
            if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
            {
                vecSmoothPath = g_jumpPointSearch.GetSmoothPath();
                if (vecSmoothPath.size() > 0)
                {
                    SelectObject(g_hMemDC, sPenSmoothPath);
                    for (int i = 0; i < vecSmoothPath.size() - 1; i++)
                    {
                        MoveToEx(g_hMemDC
                            , vecSmoothPath[i].x * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.x
                            , vecSmoothPath[i].y * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.y, NULL);
                        LineTo(g_hMemDC
                            , vecSmoothPath[i + 1].x * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.x
                            , vecSmoothPath[i + 1].y * g_painting.gridSize + g_painting.gridSize / 2 + g_painting.ptPos.y);
                    }
                }
            }


            /* F-value, 부모 방향 화살표 그리기 */
            if (g_tbBtnIsFValueChecked)
            {
                // 텍스트 배경색, 글자색 설정
                SetBkMode(g_hMemDC, TRANSPARENT);
                SetTextColor(g_hMemDC, RGB(0, 0, 0));

                // F-value font 생성
                int FValuefontSize = g_painting.gridSize / 4;
                HFONT hFValueFont = CreateFont(FValuefontSize, 0, 0, 0, 0
                    , false, false, false
                    , ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY
                    , DEFAULT_PITCH | FF_DONTCARE
                    , NULL);

                // 화살표 font 생성
                int arrowFontSize = g_painting.gridSize / 2;
                HFONT hArrowFont = CreateFont(arrowFontSize, 0, 0, 0, FW_BOLD
                    , false, false, false
                    , ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY
                    , DEFAULT_PITCH | FF_DONTCARE
                    , NULL);

                HFONT hFontOld = (HFONT)SelectObject(g_hMemDC, hFValueFont);

                // A* F-value, 부모 방향 화살표 그리기
                if (g_tbBtnAlgorithm == IDM_TOOLBAR_ASTAR_SEARCH)
                {
                    auto iterNodeInfo = umapAStarNodeInfo.begin();
                    for (; iterNodeInfo != umapAStarNodeInfo.end(); ++iterNodeInfo)
                    {
                        const AStarNode* pNode = iterNodeInfo->second;
                        int col = pNode->_x;
                        int row = pNode->_y;

                        SelectObject(g_hMemDC, hFValueFont);

                        // G-value 그리기
                        WCHAR szGValue[10];
                        int lenSzGValue = swprintf_s(szGValue, 10, L"G %.1f", pNode->_valG);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + g_painting.ptPos.y
                            , szGValue, lenSzGValue);

                        // H-value 그리기
                        WCHAR szHValue[10];
                        int lenSzHValue = swprintf_s(szHValue, 10, L"H %.1f", pNode->_valH);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + FValuefontSize + g_painting.ptPos.y
                            , szHValue, lenSzHValue);

                        // F-value 그리기
                        WCHAR szFValue[10];
                        int lenSzFValue = swprintf_s(szFValue, 10, L"F %.1f", pNode->_valF);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + FValuefontSize * 2 + g_painting.ptPos.y
                            , szFValue, lenSzFValue);

                        // 부모 방향 그리기
                        SelectObject(g_hMemDC, hArrowFont);
                        if (pNode->_pParent != nullptr)
                        {
                            WCHAR chArrow;
                            int diffX = pNode->_pParent->_x - pNode->_x;
                            int diffY = pNode->_pParent->_y - pNode->_y;
                            if (diffX > 0)
                            {
                                if (diffY > 0)
                                    chArrow = L'↘';
                                else if (diffY == 0)
                                    chArrow = L'→';
                                else
                                    chArrow = L'↗';
                            }
                            else if (diffX == 0)
                            {
                                if (diffY > 0)
                                    chArrow = L'↓';
                                else if (diffY == 0)
                                    chArrow = L'?';
                                else
                                    chArrow = L'↑';
                            }
                            else
                            {
                                if (diffY > 0)
                                    chArrow = L'↙';
                                else if (diffY == 0)
                                    chArrow = L'←';
                                else
                                    chArrow = L'↖';
                            }

                            TextOut(g_hMemDC
                                , (col + 1) * g_painting.gridSize - arrowFontSize + g_painting.ptPos.x
                                , (row + 1) * g_painting.gridSize - arrowFontSize + g_painting.ptPos.y
                                , (LPCWSTR)&chArrow, 1);
                        }
                    }
                }

                // Jump Point Search F-value, 부모 방향 화살표 그리기
                else if (g_tbBtnAlgorithm == IDM_TOOLBAR_JUMP_POINT_SEARCH)
                {
                    for (size_t i = 0; i < vecJPSNodeInfo.size(); i++)
                    {
                        const JPSNode* pNode = vecJPSNodeInfo[i];
                        int col = pNode->_x;
                        int row = pNode->_y;

                        SelectObject(g_hMemDC, hFValueFont);

                        // G-value 그리기
                        WCHAR szGValue[10];
                        int lenSzGValue = swprintf_s(szGValue, 10, L"G %.1f", pNode->_valG);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + g_painting.ptPos.y
                            , szGValue, lenSzGValue);

                        // H-value 그리기
                        WCHAR szHValue[10];
                        int lenSzHValue = swprintf_s(szHValue, 10, L"H %.1f", pNode->_valH);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + FValuefontSize + g_painting.ptPos.y
                            , szHValue, lenSzHValue);

                        // F-value 그리기
                        WCHAR szFValue[10];
                        int lenSzFValue = swprintf_s(szFValue, 10, L"F %.1f", pNode->_valF);
                        TextOut(g_hMemDC
                            , col * g_painting.gridSize + g_painting.ptPos.x + 1
                            , row * g_painting.gridSize + FValuefontSize * 2 + g_painting.ptPos.y
                            , szFValue, lenSzFValue);

                        // 부모 방향 그리기
                        SelectObject(g_hMemDC, hArrowFont);
                        if (pNode->_pParent != nullptr)
                        {
                            WCHAR chArrow;
                            int diffX = pNode->_pParent->_x - pNode->_x;
                            int diffY = pNode->_pParent->_y - pNode->_y;
                            if (diffX > 0)
                            {
                                if (diffY > 0)
                                    chArrow = L'↘';
                                else if (diffY == 0)
                                    chArrow = L'→';
                                else
                                    chArrow = L'↗';
                            }
                            else if (diffX == 0)
                            {
                                if (diffY > 0)
                                    chArrow = L'↓';
                                else if (diffY == 0)
                                    chArrow = L'?';
                                else
                                    chArrow = L'↑';
                            }
                            else
                            {
                                if (diffY > 0)
                                    chArrow = L'↙';
                                else if (diffY == 0)
                                    chArrow = L'←';
                                else
                                    chArrow = L'↖';
                            }

                            TextOut(g_hMemDC
                                , (col + 1) * g_painting.gridSize - arrowFontSize + g_painting.ptPos.x
                                , (row + 1) * g_painting.gridSize - arrowFontSize + g_painting.ptPos.y
                                , (LPCWSTR)&chArrow, 1);
                        }
                    }
                }

                SelectObject(g_hMemDC, hFontOld);
                DeleteObject(hFValueFont);
                DeleteObject(hArrowFont);
            }  // end of F-value, 부모 방향 화살표 그리기


            /* Bresenham Line 그리기 */
            // Bresenham Line 버튼이 눌려져 있고, 현재 마우스를 드래그 중일 경우 선을 그림
            if (g_tbBtnTool == IDM_TOOLBAR_BRESENHAM_LINE
                && g_clickPainting)
            {
                SelectObject(g_hMemDC, sPenGrid);
                SelectObject(g_hMemDC, sBrushBresenhamLine);

                int startRow = mcMOUSE_Y_TO_GRID_ROW(g_mouseClickY);
                int startCol = mcMOUSE_X_TO_GRID_COL(g_mouseClickX);
                int endRow = mcMOUSE_Y_TO_GRID_ROW(g_mouseY);
                int endCol = mcMOUSE_X_TO_GRID_COL(g_mouseX);
                Rectangle(g_hMemDC
                    , startCol * g_painting.gridSize + g_painting.ptPos.x
                    , startRow * g_painting.gridSize + g_painting.ptPos.y
                    , (startCol + 1) * g_painting.gridSize + g_painting.ptPos.x
                    , (startRow + 1) * g_painting.gridSize + g_painting.ptPos.y);

                CBresenhamPath BPath;
                BPath.SetParam(POINT{ startCol, startRow }, POINT{ endCol, endRow });
                while (BPath.Next())
                {
                    Rectangle(g_hMemDC
                        , BPath.GetX() * g_painting.gridSize + g_painting.ptPos.x
                        , BPath.GetY() * g_painting.gridSize + g_painting.ptPos.y
                        , (BPath.GetX() + 1) * g_painting.gridSize + g_painting.ptPos.x
                        , (BPath.GetY() + 1) * g_painting.gridSize + g_painting.ptPos.y);
                }
            }





            /* 그리기영역을 제외한 memDC 영역 초기화 */
            // memDC 초기화를 이곳에서 하는 이유는 그리기영역 밖으로 삐져나와 그려진 것들을 없애기 위해서임.
            SelectObject(g_hMemDC, sBrushBackground);
            PatBlt(g_hMemDC, 0, 0, g_ptMemBitSize.x, g_painting.ptPos.y, PATCOPY);
            PatBlt(g_hMemDC, 0, 0, g_painting.ptPos.x, g_ptMemBitSize.y, PATCOPY);
            PatBlt(g_hMemDC, g_painting.ptPos.x + g_painting.ptSize.x, 0, g_ptMemBitSize.x, g_ptMemBitSize.y, PATCOPY);
            PatBlt(g_hMemDC, 0, g_painting.ptPos.y + g_painting.ptSize.y, g_ptMemBitSize.x, g_ptMemBitSize.y, PATCOPY);


            /* Mem DC에 그라데이션 그림자 그리기 */
            for (int i = 0; i < dfSHADOW_STEP; i++)
            {
                SelectObject(g_hMemDC, arrSPenShadow[i]);

                MoveToEx(g_hMemDC, g_painting.ptPos.x + g_painting.ptSize.x + i, g_painting.ptPos.y + 10, NULL);
                LineTo(g_hMemDC, g_painting.ptPos.x + g_painting.ptSize.x + i, g_painting.ptPos.y + g_painting.ptSize.y + dfSHADOW_STEP);
                MoveToEx(g_hMemDC, g_painting.ptPos.x + 10, g_painting.ptPos.y + g_painting.ptSize.y + i, NULL);
                LineTo(g_hMemDC, g_painting.ptPos.x + g_painting.ptSize.x + dfSHADOW_STEP, g_painting.ptPos.y + g_painting.ptSize.y + i);
            }


            /* 그리기영역 크기조정 object 그리기 */
            {
                SelectObject(g_hMemDC, sPenResizeObject);
                SelectObject(g_hMemDC, sBrushResizeObject);
                int left[3] = { g_painting.ptPos.x + g_painting.ptSize.x / 2 - 2, g_painting.ptPos.x + g_painting.ptSize.x, g_painting.ptPos.x + g_painting.ptSize.x };
                int top[3] = { g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y, g_painting.ptPos.y + g_painting.ptSize.y / 2 - 2 };
                for (int i = 0; i < 3; i++)
                {
                    Rectangle(g_hMemDC, left[i], top[i], left[i] + 5, top[i] + 5);
                }
            }


            /* 그리기영역 크기조정 object를 클릭했을 때 크기조정 테두리 그리기 */
            if (g_clickResizeVertical || g_clickResizeDiagonal || g_clickResizeHorizontal)
            {
                SelectObject(g_hMemDC, sPenResizeBorder);
                SelectObject(g_hMemDC, sBrushResizeBorder);

                // 수직 그리기영역 크기조정 object를 클릭했을 때 크기조정 테두리 그리기
                if (g_clickResizeVertical)
                {
                    Rectangle(g_hMemDC
                        , g_painting.ptPos.x
                        , g_painting.ptPos.y
                        , g_painting.ptPos.x + g_painting.ptSize.x
                        , max(g_mouseY, g_painting.ptPos.y + dfPAINT_SIZE_MIN_Y));
                }
                // 대각 그리기영역 크기조정 object를 클릭했을 때 크기조정 테두리 그리기
                else if (g_clickResizeDiagonal)
                {
                    Rectangle(g_hMemDC
                        , g_painting.ptPos.x
                        , g_painting.ptPos.y
                        , max(g_mouseX, g_painting.ptPos.x + dfPAINT_SIZE_MIN_X)
                        , max(g_mouseY, g_painting.ptPos.y + dfPAINT_SIZE_MIN_Y));
                }
                // 수평 그리기영역 크기조정 object를 클릭했을 때 크기조정 테두리 그리기
                else if (g_clickResizeHorizontal)
                {
                    Rectangle(g_hMemDC
                        , g_painting.ptPos.x
                        , g_painting.ptPos.y
                        , max(g_mouseX, g_painting.ptPos.x + dfPAINT_SIZE_MIN_X)
                        , g_painting.ptPos.y + g_painting.ptSize.y);
                }
            }


            /* 그리드 row, col 수 표시문자 출력 */
            SelectObject(g_hMemDC, sFontGridInfo);
            SetBkMode(g_hMemDC, TRANSPARENT);
            SetTextColor(g_hMemDC, RGB(0, 0, 0));

            WCHAR szGridInfo[30];
            int lenSzGridInfo = swprintf_s(szGridInfo, 30, L"%d x %d cells", g_painting.gridCols, g_painting.gridRows);
            TextOut(g_hMemDC
                , g_painting.ptPos.x
                , g_painting.ptPos.y - 15
                , szGridInfo, lenSzGridInfo);


            /* 테스트 수행중일 때 관련 텍스트 출력 */
            WCHAR szTestInfo[150];
            int lenSzTestInfo;
            if (g_isTestRunning || g_isPrintTestInfo)
            {
                lenSzTestInfo = swprintf_s(szTestInfo, 150, L"Test is running...  seed: %u.  Press 'Q' on the console window to quit the test or 'R' to stop rendering.\n", g_testRandomSeed);
                TextOut(g_hMemDC
                    , g_painting.ptPos.x + 100
                    , g_painting.ptPos.y - 15
                    , szTestInfo, lenSzTestInfo);

                if (g_isTestRunning == false && g_isPrintTestInfo == true)
                    g_isPrintTestInfo = false;
            }


            // begin paint
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            /* MemDC에서 MainDC로 bit block transfer */
            BitBlt(g_hDC
                , 0, g_tbHeight
                , g_ptMemBitSize.x, g_ptMemBitSize.y
                , g_hMemDC, g_hScrollPos, g_tbHeight + g_vScrollPos, SRCCOPY);
            EndPaint(hWnd, &ps);

        } // end of if(!g_isRenderFromScrolling)


        // 스크롤링에 의한 렌더링일 경우 BitBlt만 다시 한다.
        else
        {
            g_isRenderFromScrolling = false;

            // begin paint
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            /* MemDC에서 MainDC로 bit block transfer */
            BitBlt(g_hDC
                , 0, g_tbHeight
                , g_ptMemBitSize.x, g_ptMemBitSize.y
                , g_hMemDC, g_hScrollPos, g_tbHeight + g_vScrollPos, SRCCOPY);

            EndPaint(hWnd, &ps);
        }


    }
    break;
    case WM_DESTROY:
        // KillTimer
        // select old dc
        // release dc
        // delete dc
        KillTimer(g_hWnd, dfTIMER_ID);

        SelectObject(g_hMemDC, sBrushOld);
        SelectObject(g_hMemDC, sPenOld);
        DeleteObject(sBrushBackground);
        DeleteObject(sPenGrid);
        DeleteObject(sBrushOpenNode);
        DeleteObject(sBrushCloseNode);
        DeleteObject(sBrushStartPoint);
        DeleteObject(sBrushEndPoint);
        DeleteObject(sBrushWall);
        DeleteObject(sPenPath);
        for (int i = 0; i < dfSHADOW_STEP; i++)
            DeleteObject(arrSPenShadow[i]);
        DeleteObject(sPenResizeObject);
        DeleteObject(sBrushResizeObject);
        DeleteObject(sPenResizeBorder);
        DeleteObject(sBrushResizeBorder);

        PostQuitMessage(0);     // 이 작업을 하지 않으면 윈도우만 닫히고 프로세스는 종료되지 않게 된다.
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}




/* Toolbar :: toolbar 만들기 */
HWND CreateToolbar(HINSTANCE hInstance, HWND hWnd)
{
    // Declare and initialize local constants
    HIMAGELIST hImageList = NULL;
    const int imageListID = 0;
    const int numButtons = 21;
    const int numCustonButtons = 16;
    const int bitmapSize = 16;
    const DWORD buttonStyles = BTNS_AUTOSIZE;

    // Create the toolbar.
    HWND hWndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL
        , WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | CCS_TOP | BTNS_AUTOSIZE
        , 0, 0, 0, 0
        , hWnd
        , NULL //(HMENU)IDC_MAIN_TOOL
        , GetModuleHandle(NULL), NULL);
    SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(hWndToolbar, TB_SETEXTENDEDSTYLE, 0, (LPARAM)TBSTYLE_EX_HIDECLIPPEDBUTTONS);

    // set button size
    //SendMessage(hWndToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(32, 32));

    TBADDBITMAP tbab;
    tbab.hInst = HINST_COMMCTRL;
    tbab.nID = IDB_STD_SMALL_COLOR;
    SendMessage(hWndToolbar, TB_ADDBITMAP, 0, (LPARAM)&tbab);


    // Create the image list.
    hImageList = ImageList_Create(bitmapSize, bitmapSize   // Dimensions of individual bitmaps.
        , ILC_COLOR16 | ILC_MASK  // Ensures transparent background.
        , numCustonButtons, 0);
    HBITMAP hBitSizeAdj = (HBITMAP)LoadImage(nullptr, L"toolbar_sizeAdj.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitWall = (HBITMAP)LoadImage(nullptr, L"toolbar_wall.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitStartpoint = (HBITMAP)LoadImage(nullptr, L"toolbar_startpoint.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitEndpoint = (HBITMAP)LoadImage(nullptr, L"toolbar_endpoint.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitBresenhamLine = (HBITMAP)LoadImage(nullptr, L"toolbar_bresenhamLine.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitAStarSearch = (HBITMAP)LoadImage(nullptr, L"toolbar_AStar.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitJumpPointSearch = (HBITMAP)LoadImage(nullptr, L"toolbar_JumpPoint.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitSearch = (HBITMAP)LoadImage(nullptr, L"toolbar_search.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitFValue = (HBITMAP)LoadImage(nullptr, L"toolbar_FValue.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitSpeedUp = (HBITMAP)LoadImage(nullptr, L"toolbar_speedUp.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitSpeedDown = (HBITMAP)LoadImage(nullptr, L"toolbar_speedDown.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitGoToEnd = (HBITMAP)LoadImage(nullptr, L"toolbar_goToEnd.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitMaze = (HBITMAP)LoadImage(nullptr, L"toolbar_maze.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitCave = (HBITMAP)LoadImage(nullptr, L"toolbar_cave.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitRandom = (HBITMAP)LoadImage(nullptr, L"toolbar_random.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    HBITMAP hBitTest = (HBITMAP)LoadImage(nullptr, L"toolbar_test.bmp", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    ImageList_Add(hImageList, hBitSizeAdj, NULL);
    ImageList_Add(hImageList, hBitWall, NULL);
    ImageList_Add(hImageList, hBitStartpoint, NULL);
    ImageList_Add(hImageList, hBitEndpoint, NULL);
    ImageList_Add(hImageList, hBitBresenhamLine, NULL);
    ImageList_Add(hImageList, hBitAStarSearch, NULL);
    ImageList_Add(hImageList, hBitJumpPointSearch, NULL);
    ImageList_Add(hImageList, hBitSearch, NULL);
    ImageList_Add(hImageList, hBitFValue, NULL);
    ImageList_Add(hImageList, hBitSpeedUp, NULL);
    ImageList_Add(hImageList, hBitSpeedDown, NULL);
    ImageList_Add(hImageList, hBitGoToEnd, NULL);
    ImageList_Add(hImageList, hBitMaze, NULL);
    ImageList_Add(hImageList, hBitCave, NULL);
    ImageList_Add(hImageList, hBitRandom, NULL);
    ImageList_Add(hImageList, hBitTest, NULL);
    DeleteObject(hBitSizeAdj);
    DeleteObject(hBitWall);
    DeleteObject(hBitStartpoint);
    DeleteObject(hBitEndpoint);
    DeleteObject(hBitBresenhamLine);
    DeleteObject(hBitAStarSearch);
    DeleteObject(hBitJumpPointSearch);
    DeleteObject(hBitSearch);
    DeleteObject(hBitFValue);
    DeleteObject(hBitSpeedUp);
    DeleteObject(hBitSpeedDown);
    DeleteObject(hBitGoToEnd);
    DeleteObject(hBitMaze);
    DeleteObject(hBitCave);
    DeleteObject(hBitRandom);
    DeleteObject(hBitTest);

    // Set the image list.
    SendMessage(hWndToolbar, TB_SETIMAGELIST, (WPARAM)imageListID, (LPARAM)hImageList);
    // Load the button images.
    SendMessage(hWndToolbar, TB_LOADIMAGES, (WPARAM)IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    // for tooltip
    SendMessage(hWndToolbar, TB_SETMAXTEXTROWS, (WPARAM)0, (LPARAM)0);
    // Initialize button info.
    TBBUTTON tbButtons[numButtons] =
    {
        { MAKELONG(STD_FILENEW + numCustonButtons, imageListID),  IDM_TOOLBAR_NEW,  TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"New"},
        { MAKELONG(0, imageListID), IDM_TOOLBAR_SIZE_ADJ,   TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Resize"},
        { 10, 0, 0, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(1, imageListID), IDM_TOOLBAR_WALL,       TBSTATE_ENABLED | TBSTATE_CHECKED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"Wall"},
        { MAKELONG(2, imageListID), IDM_TOOLBAR_STARTPOINT, TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"Start Point"},
        { MAKELONG(3, imageListID), IDM_TOOLBAR_ENDPOINT,   TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"End Point"},
        { MAKELONG(4, imageListID), IDM_TOOLBAR_BRESENHAM_LINE,   TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"Bresenham Line"},
        { 10, 0, 0, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(5, imageListID), IDM_TOOLBAR_ASTAR_SEARCH, TBSTATE_ENABLED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"A* Search"},
        { MAKELONG(6, imageListID), IDM_TOOLBAR_JUMP_POINT_SEARCH,   TBSTATE_ENABLED | TBSTATE_CHECKED, BTNS_AUTOSIZE | BTNS_CHECKGROUP, {0}, 0, (INT_PTR)L"Jump Point Search"},
        { 10, 0, 0, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(7, imageListID), IDM_TOOLBAR_SEARCH,     TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Search"},
        { MAKELONG(8, imageListID), IDM_TOOLBAR_FVALUE,     TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_CHECK, {0}, 0, (INT_PTR)L"Show F-value"},
        { MAKELONG(9, imageListID), IDM_TOOLBAR_SPEEDUP,    TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Speed Up"},
        { MAKELONG(10, imageListID), IDM_TOOLBAR_SPEEDDOWN,  TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Speed Down"},
        { MAKELONG(11, imageListID), IDM_TOOLBAR_GOTOEND,   TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Go to End"},
        { 10, 0, 0, BTNS_SEP, {0}, 0, 0},
        { MAKELONG(12, imageListID), IDM_TOOLBAR_MAKE_MAZE, TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Make Maze"},
        { MAKELONG(13, imageListID), IDM_TOOLBAR_MAKE_CAVE, TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Make Cave"},
        { MAKELONG(14, imageListID), IDM_TOOLBAR_MAKE_RANDOM, TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Make Random"},
        { MAKELONG(15, imageListID), IDM_TOOLBAR_RUN_TEST,  TBSTATE_ENABLED, BTNS_AUTOSIZE | TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"Run Test"},
    };
    // Add buttons.
    SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(hWndToolbar, TB_ADDBUTTONS, (WPARAM)numButtons, (LPARAM)&tbButtons);
    // show toolbar
    SendMessage(hWndToolbar, TB_AUTOSIZE, 0, 0);

    return hWndToolbar;
}





// 크기 조정 다이얼로그
INT_PTR CALLBACK WndProcDialogSizeAdj(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDlgItemInt(hDlg, IDC_SIZE_ADJ_SIZE, g_painting.gridSize, TRUE);
        SetDlgItemInt(hDlg, IDC_SIZE_ADJ_WIDTH, g_painting.ptSize.x, TRUE);
        SetDlgItemInt(hDlg, IDC_SIZE_ADJ_HEIGHT, g_painting.ptSize.y, TRUE);
    }
    return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch LOWORD(wParam)
        {
        case IDOK:
        {
            // OK를 눌렀을 경우 g_painting 에 크기 값을 저장한다.
            BOOL translated;
            int gridSize = (int)GetDlgItemInt(hDlg, IDC_SIZE_ADJ_SIZE, &translated, TRUE);
            if (!translated)
                break;

            int width = (int)GetDlgItemInt(hDlg, IDC_SIZE_ADJ_WIDTH, &translated, TRUE);
            if (!translated)
                break;

            int height = (int)GetDlgItemInt(hDlg, IDC_SIZE_ADJ_HEIGHT, &translated, TRUE);
            if (!translated)
                break;

            g_painting.SetSize(gridSize, width, height);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;

        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
















// Utils :: memory bitmap 크기를 조정한다.
void ResizeWindow()
{
    // 그리기영역 전체를 포함할 수 있는 크기로 만든다.
    GetClientRect(g_hWnd, &g_rtClientSize);
    int newBitSizeX = max(g_rtClientSize.right, g_painting.ptSize.x + g_painting.ptPos.x + 50);
    int newBitSizeY = max(g_rtClientSize.bottom, g_painting.ptSize.y + g_painting.ptPos.y + g_tbHeight + 50);

    // 만약 새로운 크기가 이전 크기와 같다면 종료
    if (newBitSizeX == g_ptMemBitSize.x && newBitSizeY == g_ptMemBitSize.y)
        return;

    // 비트맵 재생성
    g_ptMemBitSize.x = newBitSizeX;
    g_ptMemBitSize.y = newBitSizeY;
    HBITMAP g_hNewBit = CreateCompatibleBitmap(g_hMemDC, g_ptMemBitSize.x, g_ptMemBitSize.y);
    SelectObject(g_hMemDC, g_hNewBit);
    DeleteObject(g_hMemBit);
    g_hMemBit = g_hNewBit;

}

// Utils :: grid를 초기화한다.
void InitGrid()
{
    if (g_arr2Grid)
    {
        delete[] g_arr2Grid[0];
        delete[] g_arr2Grid;
    }

    g_arr2Grid = new char* [g_painting.gridRows];
    g_arr2Grid[0] = new char[g_painting.gridRows * g_painting.gridCols];
    memset(g_arr2Grid[0], 0, g_painting.gridRows * g_painting.gridCols);
    for (int i = 1; i < g_painting.gridRows; i++)
    {
        g_arr2Grid[i] = g_arr2Grid[0] + (i * g_painting.gridCols);
    }

    g_painting.startCol = -1;
    g_painting.startRow = -1;
    g_painting.endCol = -1;
    g_painting.endRow = -1;
}

// Utils :: grid 내용을 모두 지운다.
void ClearGrid()
{
    memset(g_arr2Grid[0], 0, g_painting.gridRows * g_painting.gridCols);

    g_painting.startCol = -1;
    g_painting.startRow = -1;
    g_painting.endCol = -1;
    g_painting.endRow = -1;
}

// Utils :: grid를 다시 만들고 이전 내용을 복사한다.
void ResizeGrid()
{
    // new grid 만들기
    char** gridNew = new char* [g_painting.gridRows];
    gridNew[0] = new char[g_painting.gridRows * g_painting.gridCols];
    memset(gridNew[0], 0, g_painting.gridRows * g_painting.gridCols);
    for (int i = 1; i < g_painting.gridRows; i++)
    {
        gridNew[i] = gridNew[0] + (i * g_painting.gridCols);
    }

    // 이전 값 복사
    for (int row = 0; row < g_painting.gridRows; row++)
    {
        if (row >= g_painting.gridRows_old)
            break;

        for (int col = 0; col < g_painting.gridCols; col++)
        {
            if (col >= g_painting.gridCols_old)
                break;
            gridNew[row][col] = g_arr2Grid[row][col];
        }
    }

    delete[] g_arr2Grid[0];
    delete[] g_arr2Grid;
    g_arr2Grid = gridNew;
}



// Utils :: 스크롤을 재조정한다.
void ReadjustScroll()
{
    GetClientRect(g_hWnd, &g_rtClientSize);
    // 수평 스크롤링 범위는 (비트맵너비 - 클라이언트너비) 로 정해진다.
    // 현재 수평 스크롤 위치는 수평 스크롤링 범위 내에 유지된다.
    g_hScrollMax = max(g_ptMemBitSize.x - g_rtClientSize.right, 0);
    g_hScrollPos = min(g_hScrollPos, g_hScrollMax);
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = g_ptMemBitSize.x;
    si.nPage = g_rtClientSize.right;
    si.nPos = g_hScrollPos;
    SetScrollInfo(g_hWnd, SB_HORZ, &si, TRUE);

    // 수직 스크롤링 범위는 (비트맵높이 - 클라이언트높이) 로 정해진다.
    // 현재 수직 스크롤 위치는 수직 스크롤링 범위 내에 유지된다.
    g_vScrollMax = max(g_ptMemBitSize.y - g_rtClientSize.bottom, 0);
    g_vScrollPos = min(g_vScrollPos, g_vScrollMax);
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = g_ptMemBitSize.y;
    si.nPage = g_rtClientSize.bottom;
    si.nPos = g_vScrollPos;
    SetScrollInfo(g_hWnd, SB_VERT, &si, TRUE);
}







// Utils :: 테스트 중단 키보드 입력 감지 스레드
unsigned WINAPI ThreadProcQuitRunTest(PVOID vParam)
{
    while (1)
    {
        if (g_isTestRunning == false)
            return 0;

        int ch = _getch();
        if (ch == 'q')
        {
            g_isTestRunning = false;
            return 0;
        }
        else if (ch == 'r')
        {
            g_isTestDoRendering = g_isTestDoRendering ? false : true;
        }
        Sleep(50);
    }
}