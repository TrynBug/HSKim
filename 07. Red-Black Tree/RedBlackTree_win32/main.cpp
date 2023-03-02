#include "framework.h"
#include "resource.h"

#include <iostream>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include "CRedBlackTree.h"

#define MAX_LOADSTRING 100

// 전역 변수:
HINSTANCE g_hInst;
WCHAR szTitle[MAX_LOADSTRING]; 
WCHAR szWindowClass[MAX_LOADSTRING];

// handle
HWND g_hWnd;
HDC g_hDC;
HBITMAP g_hMemBit;
HDC g_hMemDC;

// GDI
HPEN g_hPenLine;           // 노드 연결 선
HPEN g_hPenRedNode;        // 레드노드 테두리
HPEN g_hPenBlackNode;      // 블랙노드 테두리
HBRUSH g_hBrushRedNode;    // 레드노드 색
HBRUSH g_hBrushBlackNode;  // 블랙노드 색
HBRUSH g_hBrushBackground; // 배경색
HPEN g_hPenOld;
HBRUSH g_hBrushOld;

// size
RECT g_rtClientSize;      // 클라이언트 영역 크기
POINT g_ptMemBitSize;     // 메모리 비트맵 크기
float g_nodeFullSize = 50.f;  // 노드 크기 + 노드 양옆 공간 크기

// tree
CRedBlackTree<int, int> g_rbTree;  // red-black tree
std::vector<int> g_vecValues;      // insert한 값들 모아놓는용도

// insert, delete
bool g_isInsert = false;
bool g_isDelete = false;
int g_value;

// point
struct Point
{
    float x;
    float y;
};

// 스크롤 관련
int g_hScrollPos = 0;  // 수평 스크롤 위치
int g_vScrollPos = 0;  // 수직 스크롤 위치
int g_hScrollMax = 0;  // 수평 스크롤 최대위치
int g_vScrollMax = 0;  // 수직 스크롤 최대위치
bool g_isRenderFromScrolling = false; // 렌더링이 스크롤링에 의한 것인지 여부. 스크롤링에 의한 것이면 object를 다시 그리지 않는다.



/* utils */
void ResizeWindow();  // memory bitmap 재생성
void ReadjustScroll(); // 스크롤을 재조정한다.

/* tree 그리기 */
void DrawRBTree(CRedBlackTree<int, int>& tree);  // tree 그리기
void _drawNode(const CRedBlackTree<int, int>::iterator& iter
    , std::map<int, Point>& mapPos, Point posParent
    , float nodeSize, float nodeSpace);  // 노드 그리기
int _getNumOfNil(const CRedBlackTree<int, int>::iterator& iter);  // 현재 노드 아래의 Nil 수 구하기
int _getNumOfLeaf(const CRedBlackTree<int, int>::iterator& iter); // 현재 노드 아래의 leaf(Nil아님) 수 구하기


ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 전역 문자열을 초기화합니다.
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MY0530REDBLACKTREEWIN32, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 애플리케이션 초기화를 수행합니다:
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MY0530REDBLACKTREEWIN32));

    MSG msg;

    // 기본 메시지 루프입니다:
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



//  함수: MyRegisterClass()
//  용도: 창 클래스를 등록합니다.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_MY0530REDBLACKTREEWIN32));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_MY0530REDBLACKTREEWIN32);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//   함수: InitInstance(HINSTANCE, int)
//   용도: 인스턴스 핸들을 저장하고 주 창을 만듭니다.
//   주석:
//        이 함수를 통해 인스턴스 핸들을 전역 변수에 저장하고
//        주 프로그램 창을 만든 다음 표시합니다.
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL, 
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);
    g_hWnd = hWnd;

    if (!hWnd)
    {
        return FALSE;
    }

    // 이중버퍼링 DC, 비트맵 생성
    g_hDC = GetDC(g_hWnd);
    GetClientRect(g_hWnd, &g_rtClientSize);
    g_ptMemBitSize.x = g_rtClientSize.right;
    g_ptMemBitSize.y = g_rtClientSize.bottom;
    g_hMemBit = CreateCompatibleBitmap(g_hDC, g_ptMemBitSize.x, g_ptMemBitSize.y);
    g_hMemDC = CreateCompatibleDC(g_hDC);
    HBITMAP hOldBit = (HBITMAP)SelectObject(g_hMemDC, g_hMemBit);
    DeleteObject(hOldBit);
    PatBlt(g_hMemDC, 0, 0, g_ptMemBitSize.x, g_ptMemBitSize.y, WHITENESS);  // memDC를 흰색으로 초기화
    ReadjustScroll();

    // GDI 생성
    g_hPenLine = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
    //g_hPenRedNode = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
    //g_hPenBlackNode = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    g_hPenRedNode = (HPEN)GetStockObject(NULL_PEN);
    g_hPenBlackNode = (HPEN)GetStockObject(NULL_PEN);
    g_hBrushRedNode = CreateSolidBrush(RGB(255, 0, 0));
    g_hBrushBlackNode = CreateSolidBrush(RGB(0, 0, 0));
    g_hBrushBackground = CreateSolidBrush(RGB(199, 209, 225));
    g_hPenOld = (HPEN)SelectObject(g_hMemDC, g_hPenRedNode);
    g_hBrushOld = (HBRUSH)SelectObject(g_hMemDC, g_hBrushRedNode);

    // random seed
    srand(1111);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // 메뉴 선택을 구문 분석합니다:
        switch (wmId)
        {
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    /* 키보드 입력 */
    case WM_KEYDOWN:
    {
        // tree에 insert
        if (wParam == 'I')
        {
            while (true)
            {
                if (g_rbTree.Size() == 2000)
                    break;

                int rInt = rand() % 10000;
                //static int rInt = 0;
                //rInt++;
                bool isInsert = g_rbTree.Insert(rInt, rInt);
                if (isInsert == true)
                {
                    g_vecValues.push_back(rInt);
                    g_isInsert = true;
                    g_isDelete = false;
                    g_value = rInt;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
            }
        }
        // tree에서 delete
        else if (wParam == 'D')
        {
            if (g_vecValues.size() == 0)
                break;

            auto rng = std::default_random_engine{};
            std::shuffle(std::begin(g_vecValues), std::end(g_vecValues), rng);
            int key = g_vecValues.back();
            g_vecValues.pop_back();

            auto iter = g_rbTree.Find(key);
            g_rbTree.Erase(iter);
            g_isInsert = false;
            g_isDelete = true;
            g_value = key;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
    }
    break;

    /* 마우스 휠을 굴릴 때 */
    case WM_MOUSEWHEEL:
    {
        int fwKeys = GET_KEYSTATE_WPARAM(wParam);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        // 현재 Ctrl 키가 눌려져 있다면 노드 크기조정
        if (fwKeys & MK_CONTROL)
        {
            // 마우스 휠을 위로 굴렸을 때 노드 크기 +
            if (zDelta > 0)
            {
                g_nodeFullSize += 2.f;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            // 마우스 휠을 아래로 굴렸을 때 노드 크기 -
            else if (zDelta < 0)
            {
                g_nodeFullSize -= 2.f;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    break;

    /* 창의 크기가 바뀔 때 */
    case WM_SIZE:
    {
        GetClientRect(g_hWnd, &g_rtClientSize);
        if (g_ptMemBitSize.x < g_rtClientSize.right
            || g_ptMemBitSize.y < g_rtClientSize.bottom)
        {
            g_ptMemBitSize.x = g_rtClientSize.right;
            g_ptMemBitSize.y = g_rtClientSize.bottom;
            ResizeWindow();
            ReadjustScroll();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else
        {
            ReadjustScroll();
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
            g_isRenderFromScrolling = true;  // 렌더링이 스크롤링에 의한 것임을 표시
            InvalidateRect(hWnd, NULL, FALSE);
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
            g_isRenderFromScrolling = true;  // 렌더링이 스크롤링에 의한 것임을 표시
            InvalidateRect(hWnd, NULL, FALSE);
        }
        // 현재 스크롤바 위치 갱신
        g_vScrollPos = si.nPos;
    }
    break;



    /* WM_PAINT */
    case WM_PAINT:
    {

        // memDC 배경색 초기화
        SelectObject(g_hMemDC, g_hBrushBackground);
        PatBlt(g_hMemDC, 0, 0, g_ptMemBitSize.x, g_ptMemBitSize.y, PATCOPY);

        // tree 그리기
        DrawRBTree(g_rbTree);


        // insert, delete 텍스트 그리기
        SetBkMode(g_hMemDC, TRANSPARENT);
        SetTextColor(g_hMemDC, RGB(0, 0, 0));

        RECT rtUI = { 10 + g_hScrollPos, 10 + g_vScrollPos, 100 + g_hScrollPos, 30 + g_vScrollPos };
        WCHAR szUI[] = L"Insert: I, Delete: D";
        DrawText(g_hMemDC, szUI, lstrlen(szUI), &rtUI, DT_LEFT | DT_NOCLIP);

        RECT rtInsert = { 10 + g_hScrollPos, 30 + g_vScrollPos, 100 + g_hScrollPos, 50 + g_vScrollPos };
        RECT rtDelete = { 10 + g_hScrollPos, 50 + g_vScrollPos, 100 + g_hScrollPos, 70 + g_vScrollPos };
        WCHAR szInsert[50];
        WCHAR szDelete[50];
        if (g_isInsert)
        {
            swprintf_s(szInsert, 50, L"Inserted: %d", g_value);
            swprintf_s(szDelete, 50, L"Deleted:");
        }
        else if (g_isDelete)
        {
            swprintf_s(szInsert, 50, L"Inserted:");
            swprintf_s(szDelete, 50, L"Deleted: %d", g_value);
        }
        else
        {
            swprintf_s(szInsert, 50, L"Inserted:");
            swprintf_s(szDelete, 50, L"Deleted:");
        }
        DrawText(g_hMemDC, szInsert, lstrlen(szInsert), &rtInsert, DT_LEFT | DT_NOCLIP);
        DrawText(g_hMemDC, szDelete, lstrlen(szDelete), &rtDelete, DT_LEFT | DT_NOCLIP);

        RECT rtNumNodes = { 10 + g_hScrollPos, 70 + g_vScrollPos, 100 + g_hScrollPos, 90 + g_vScrollPos };
        WCHAR szNumNodes[50];
        swprintf_s(szNumNodes, 50, L"number of nodes: %d", (int)g_rbTree.Size());
        DrawText(g_hMemDC, szNumNodes, lstrlen(szNumNodes), &rtNumNodes, DT_LEFT | DT_NOCLIP);


        // BitBlt
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        BitBlt(g_hDC, 0, 0
            , g_ptMemBitSize.x, g_ptMemBitSize.y
            , g_hMemDC, g_hScrollPos, g_vScrollPos, SRCCOPY);

        EndPaint(hWnd, &ps);



    }
    break;
    case WM_DESTROY:
        ReleaseDC(g_hWnd, g_hDC);
        DeleteDC(g_hMemDC);
        DeleteObject(g_hMemBit);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}




/* utils::memory bitmap 크기를 조정한다. */
void ResizeWindow()
{
    HBITMAP hNewBit = CreateCompatibleBitmap(g_hMemDC, g_ptMemBitSize.x, g_ptMemBitSize.y);
    SelectObject(g_hMemDC, hNewBit);
    DeleteObject(g_hMemBit);
    g_hMemBit = hNewBit;
}


/* utils::스크롤을 재조정한다. */
void ReadjustScroll()
{
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


/* tree 그리기  :: tree 그리기 */
void DrawRBTree(CRedBlackTree<int, int>& tree)
{
    // root 노드 얻기
    auto iterRoot = tree.GetRoot();
    if (iterRoot.IsNil())
        return;

    // 전체 leaf 수 계산하기
    int numOfLeaf = _getNumOfLeaf(iterRoot);

    // 노드 그림 크기 계산하기
    float nodeSize = g_nodeFullSize * 0.8f;
    float nodeSpace = g_nodeFullSize * 0.2f;

    // bitmap 크기를 tree 너비에 맞추기
    int minTreeWidth = (int)(g_nodeFullSize * (float)numOfLeaf);
    int maxTreeWidth = (int)(g_nodeFullSize * (float)numOfLeaf * 1.2f);
    int minTreeHeight = (int)(g_nodeFullSize * 2.0 * log(tree.Size() + 1));
    int maxTreeHeight = (int)(g_nodeFullSize * 2.0 * log(tree.Size() + 1) * 1.2);
    if (g_ptMemBitSize.x < minTreeWidth || g_ptMemBitSize.x > maxTreeWidth
        || g_ptMemBitSize.y < minTreeHeight || g_ptMemBitSize.y > maxTreeHeight)
    {
        if (g_ptMemBitSize.x != max(g_rtClientSize.right, maxTreeWidth)
            || g_ptMemBitSize.y != max(g_rtClientSize.bottom, maxTreeHeight))
        {
            g_ptMemBitSize.x = max(g_rtClientSize.right, maxTreeWidth);
            g_ptMemBitSize.y = max(g_rtClientSize.bottom, maxTreeHeight);

            ResizeWindow();
            SelectObject(g_hMemDC, g_hBrushBackground);
            PatBlt(g_hMemDC, 0, 0, g_ptMemBitSize.x, g_ptMemBitSize.y, PATCOPY);
            ReadjustScroll();
        }
    }


    // 노드 그리기
    std::map<int, Point> mapPos;
    Point pos = { (float)g_ptMemBitSize.x / 2.f, 0.f };
    _drawNode(iterRoot, mapPos, pos, nodeSize, nodeSpace);
}



/* tree 그리기 :: 노드 그리기 
@parameters
iter: 그리기 대상 노드, mapPos: 지금까지 그린 노드들의 좌표를 저장해두는 map,
posParent: 부모 좌표, nodeSize: 노드 그림 크기, nodeSpace: 노드 그림 사이의 간격 */
void _drawNode(const CRedBlackTree<int, int>::iterator& iter
    , std::map<int, Point>& mapPos, Point posParent
    , float nodeSize, float nodeSpace)
{
    /* Nil 일 경우 return */
    if (iter.IsNil())
        return;

    /* 자신 왼쪽 subtree에서 Nil 수 구하기 */
    float leftNumOfNil = (float)_getNumOfNil(iter.GetLeftChild());
    leftNumOfNil = max(1.f, leftNumOfNil);
    /* 자신 오른쪽 subtree에서 Nil 수 구하기 */
    float rightNumOfNil = (float)_getNumOfNil(iter.GetRightChild());
    rightNumOfNil = max(1.f, rightNumOfNil);


    /* 자신의 왼쪽 경계 x좌표 계산하기 */
    float leftBoundaryX;
    // root 라면 화면 왼쪽 끝이 경계
    if (iter.IsRoot())
    {
        leftBoundaryX = 0.f;
    }
    // 왼쪽 자식이라면 조상 중 오른쪽 자식인 것의 부모의 x 좌표가 왼쪽 경계임
    // 조상 중 오른쪽 자식을 찾지 못하면 화면 왼쪽 끝이 경계
    else if (iter.IsLeftChild())
    {
        auto iterParent = iter.GetParent();
        while (true)
        {
            if (iterParent.IsRoot())
            {
                leftBoundaryX = 0.f;
                break;
            }
            if (iterParent.IsRightChild())
            {
                iterParent = iterParent.GetParent();
                auto iterPos = mapPos.find((*iterParent).first);
                leftBoundaryX = (*iterPos).second.x;
                break;
            }
            iterParent = iterParent.GetParent();
        }
    }
    // 오른쪽 자식이라면 부모의 x좌표가 왼쪽 경계임
    else
    {
        leftBoundaryX = posParent.x;
    }
    // 왼쪽 경계의 최소값은 왼쪽 subtree의 Nil 수에 따라 조정됨
    leftBoundaryX = max(leftBoundaryX, posParent.x - leftNumOfNil * (nodeSize + nodeSpace * 2));
    

    /* 자신의 오른쪽 경계 x좌표 계산하기 */
    float rightBoundaryX;
    // root 라면 화면 오른쪽 끝이 경계
    if (iter.IsRoot())
    {
        rightBoundaryX = (float)g_ptMemBitSize.x;
    }
    // 오른쪽 자식이라면 조상 중 왼쪽 자식인 것의 부모의 x 좌표가 오른쪽 경계임
    // 조상 중 왼쪽 자식을 찾지 못하면 화면 오른쪽 끝이 경계
    else if (iter.IsRightChild())
    {
        auto iterParent = iter.GetParent();
        while (true)
        {
            if (iterParent.IsRoot())
            {
                rightBoundaryX = (float)g_ptMemBitSize.x;
                break;
            }
            if (iterParent.IsLeftChild())
            {
                iterParent = iterParent.GetParent();
                auto iterPos = mapPos.find((*iterParent).first);
                rightBoundaryX = (*iterPos).second.x;
                break;
            }
            iterParent = iterParent.GetParent();
        }
    }
    // 왼쪽 자식이라면 부모의 x좌표가 오른쪽 경계임
    else
    {
        rightBoundaryX = posParent.x;
    }
    // 오른쪽 경계의 최대값은 오른쪽 subtree의 Nil 수에 따라 조정됨
    rightBoundaryX = min(rightBoundaryX, posParent.x + rightNumOfNil * (nodeSize + nodeSpace * 2));




    /* 자신의 위치 계산하기. x 좌표는 왼쪽경계와 오른쪽경계 사이에서 계산됨 */
    Point pos;
    pos.x = leftBoundaryX + (rightBoundaryX - leftBoundaryX) * (leftNumOfNil / (leftNumOfNil + rightNumOfNil));
    pos.y = posParent.y + nodeSize + nodeSpace * 2.f;
    // 위치를 map에 저장해둠
    mapPos.insert(std::make_pair((*iter).first, pos));


    /* 노드 색에 따른 GDI 변경 */
    if (iter.IsBlack())
    {
        SelectObject(g_hMemDC, g_hPenBlackNode);
        SelectObject(g_hMemDC, g_hBrushBlackNode);
    }
    else
    {
        SelectObject(g_hMemDC, g_hPenRedNode);
        SelectObject(g_hMemDC, g_hBrushRedNode);
    }


    /* 그리기 */
    // 노드 그리기
    Ellipse(g_hMemDC
        , (int)(pos.x - nodeSize / 2.f)
        , (int)(pos.y - nodeSize / 2.f)
        , (int)(pos.x + nodeSize / 2.f)
        , (int)(pos.y + nodeSize / 2.f));

    // 값 텍스트 그리기
    RECT rtText = { (int)(pos.x - nodeSize / 2.f)
        , (int)(pos.y - nodeSize / 2.f)
        , (int)(pos.x + nodeSize / 2.f)
        , (int)(pos.y + nodeSize / 2.f) };
    SetBkMode(g_hMemDC, TRANSPARENT);
    SetTextColor(g_hMemDC, RGB(255, 255, 255));
    WCHAR szValue[10];
    swprintf_s(szValue, 10, L"%d", (*iter).first);
    DrawText(g_hMemDC, szValue, lstrlen(szValue), &rtText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // 선 그리기
    if (!iter.IsRoot())
    {
        SelectObject(g_hMemDC, g_hPenLine);
        MoveToEx(g_hMemDC, (int)posParent.x, (int)(posParent.y + nodeSize / 2.f), NULL);
        LineTo(g_hMemDC, (int)pos.x, (int)(pos.y - nodeSize / 2.f));
    }

    // 다음 노드 그리기 
    _drawNode(iter.GetLeftChild(), mapPos, pos , nodeSize, nodeSpace);
    _drawNode(iter.GetRightChild(), mapPos, pos , nodeSize, nodeSpace);
}


/* tree 그리기 :: 현재 노드 아래의 Nil 수 구하기 */
int _getNumOfNil(const CRedBlackTree<int, int>::iterator& iter)
{
    int numOfNil = 0;

    if (iter.IsNil())
    {
        return 1;
    }
    else
    {
        numOfNil += _getNumOfNil(iter.GetLeftChild());
        numOfNil += _getNumOfNil(iter.GetRightChild());
    }

    return numOfNil;
}


/* tree 그리기 :: 현재 노드 아래의 leaf(Nil아님) 수 구하기 */
int _getNumOfLeaf(const CRedBlackTree<int, int>::iterator& iter)
{
    int numOfLeaf = 0;

    if (iter.IsLeaf())
    {
        return 1;
    }
    else if (iter.IsNil())
    {
        return 0;
    }
    else
    {
        numOfLeaf += _getNumOfLeaf(iter.GetLeftChild());
        numOfLeaf += _getNumOfLeaf(iter.GetRightChild());
    }

    return numOfLeaf;
}

