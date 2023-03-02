#include <iostream>
#include <Windows.h>
#include <algorithm>
#include <unordered_set>

#include "utils.h"


COLORREF hcl_to_rgb(double hue, double chroma, double luma, double alpha)
{
    // hue, chroma, luma must be in the range (0, 1)

    // do the conversion
    double _h = hue - (double)(int)hue;
    _h = _h * 6.0;
    double x = chroma * (1.0 - abs(_h - (double)((int)(_h / 2.0) * 2) - 1.0));

    double c = chroma;
    double r, g, b;
    if (0.0 <= _h && _h < 1.0)
    {
        r = c;
        g = x;
        b = 0.0;
    }
    else if (1.0 <= _h && _h < 2.0)
    {
        r = x;
        g = c;
        b = 0.0;
    }
    else if (2.0 <= _h && _h < 3.0)
    {
        r = 0.0;
        g = c;
        b = x;
    }
    else if (3.0 <= _h && _h < 4.0)
    {
        r = 0.0;
        g = x;
        b = c;
    }
    else if (4.0 <= _h && _h < 5.0)
    {
        r = x;
        g = 0.0;
        b = c;
    }
    else if (5.0 <= _h && _h < 6.0)
    {
        r = c;
        g = 0.0;
        b = x;
    }
    else
    {
        r = 0.0;
        g = 0.0;
        b = 0.0;
    }

    double m = luma - (0.298839 * r + 0.586811 * g + 0.114350 * b);
    double z = 1.0;
    if (m < 0.0)
    {
        z = luma / (luma - m);
        m = 0.0;
    }
    else if (m + c > 1.0)
    {
        z = (1.0 - luma) / (m + c - luma);
        m = 1.0 - z * c;
    }
    r = z * r + m;
    g = z * g + m;
    b = z * b + m;

    // clipping
    r = max(min(r, 1.0), 0.0) * 255.0;
    g = max(min(g, 1.0), 0.0) * 255.0;
    b = max(min(b, 1.0), 0.0) * 255.0;

    // alpha
    double background = 255.0;
    r = ((1.0 - alpha) * background) + (alpha * r);
    g = ((1.0 - alpha) * background) + (alpha * g);
    b = ((1.0 - alpha) * background) + (alpha * b);

    return RGB((int)r, (int)g, (int)b);
}


bool ggColorSlice(int numOfColors, COLORREF* arrRGB, double alpha)
{
    // numOfColors must be in the range (1, 360)

    /*
    Assume:
    n: integer >= 1
    hue[from, to]: all floats - red = 0; green = 0.33333 (or -0.66667) ; blue = 0.66667 (or -0.33333)
    chroma[from, to]: floats all in range 0 .. 1
    luma[from, to]: floats all in range 0 .. 1
    Returns a list of #rgb colour strings:
    */

    double hue[2] = { 0.004, 1.00399 };
    double chroma[2] = { 0.8, 0.8 };
    double luma[2] = { 0.6, 0.6 };
    bool skipHue = true;


    if (numOfColors < 1 || numOfColors > 360)
        return false;
    if (chroma[0] < 0.0 || chroma[0] > 1.0 || chroma[1] < 0.0 || chroma[1] > 1.0
        || luma[0] < 0.0 || luma[0] > 1.0 || luma[1] < 0.0 || luma[1] > 1.0)
        return false;


    // generate a list of colour
    double x;
    double n = (double)numOfColors;
    if (numOfColors % 2 == 0)
        x = n;
    else
        x = n + 1.0;

    double lDiff = (luma[1] - luma[0]) / (n - 1.0);
    double cDiff = (chroma[1] - chroma[0]) / (n - 1.0);
    double hDiff;
    if (skipHue)
        hDiff = (hue[1] - hue[0]) / x;
    else
        hDiff = (hue[1] - hue[0]) / (x - 1.0);

    double c, l, h;
    for (int iCnt = 0; iCnt < numOfColors; iCnt++)
    {
        double i = (double)iCnt;
        c = chroma[0] + i * cDiff;
        l = luma[0] + i * lDiff;
        h = (hue[0] + i * hDiff) - (double)(int)(hue[0] + i * hDiff);
        h = h < 0.0 ? h + 1.0 : h;
        h = max(min(h, 0.99999999999), 0.0);
        c = max(min(c, 1.0), 0.0);
        l = max(min(l, 1.0), 0.0);

        arrRGB[iCnt] = hcl_to_rgb(h, c, l, alpha);
    }

    return true;
}








#define MAKE_KEY(high, low) (((__int64)(high) << 32) | (__int64)(low))
#define GET_X_VALUE(value) ((int)(value >> 32))
#define GET_Y_VALUE(value) ((int)(value))
#define MAZE_WALL 1
#define MAZE_WAY 0


void _visit(int x, int y, int width, int height, std::unordered_set<__int64>& usetVisited, char** maze);
void MakeMaze(int width, int height, char** maze)
{
    for (int h = 0; h < height; h++)
    {
        for (int w = 0; w < width; w++)
        {
            maze[h][w] = MAZE_WALL;
        }
    }

    int mazeWidth = (int)round((double)width / 4.0);
    int mazeHeight = (int)round((double)height / 4.0);
    std::unordered_set<__int64> usetVisited;
    _visit(0, 0, mazeWidth, mazeHeight, usetVisited, maze);
}

void _visit(int x, int y, int width, int height, std::unordered_set<__int64>& usetVisited, char** maze)
{
    maze[y * 4][x * 4] = MAZE_WAY;
    maze[y * 4 + 1][x * 4] = MAZE_WAY;
    maze[y * 4][x * 4 + 1] = MAZE_WAY;
    maze[y * 4 + 1][x * 4 + 1] = MAZE_WAY;

    usetVisited.insert(MAKE_KEY(x, y));

    POINT arrDir[4] = { {x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1} };
    std::random_shuffle(std::begin(arrDir), std::end(arrDir));
    for (int i = 0; i < 4; i++)
    {
        POINT dir = arrDir[i];
        if (dir.x < 0 || dir.x >= width || dir.y < 0 || dir.y >= height)
            continue;

        if (usetVisited.find(MAKE_KEY(dir.x, dir.y)) == usetVisited.end())
        {
            maze[(y + dir.y) * 2][(x + dir.x) * 2] = MAZE_WAY;
            maze[(y + dir.y) * 2 + 1][(x + dir.x) * 2] = MAZE_WAY;
            maze[(y + dir.y) * 2][(x + dir.x) * 2 + 1] = MAZE_WAY;
            maze[(y + dir.y) * 2 + 1][(x + dir.x) * 2 + 1] = MAZE_WAY;
            _visit(dir.x, dir.y, width, height, usetVisited, maze);
        }
    }

}


