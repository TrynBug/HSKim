#pragma once

// generate ggplot2 color slice
// https://stackoverflow.com/questions/20643291/replicating-r-ggplot2-colours-in-python
COLORREF hcl_to_rgb(double hue, double chroma, double luma, double alpha=1.0);
bool ggColorSlice(int numOfColors, COLORREF* arrRGB, double alpha=1.0);


// 미로 만들기
void MakeMaze(int width, int height, char** maze);




