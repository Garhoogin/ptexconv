#pragma once

#include "color.h"

COLOR32 getAverageColor(COLOR32 *colors, int nColors);

void getPrincipalComponent(COLOR32 *colors, int nColors, float *vec);

void getColorEndPoints(COLOR32 *colors, int nColors, COLOR32 *points);