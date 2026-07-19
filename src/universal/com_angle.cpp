#include <universal/com_angle.h>

#include <cmath>

float KISAK_CDECL AngleDelta(float a1, float a2)
{
    float rounded;
    float offset;
    float difference;
    float scaled;

    difference = a1 - a2;
    scaled = static_cast<float>(difference * 0.002777777845039964);
    offset = static_cast<float>(scaled + 0.5);
    rounded = std::floor(offset);
    return static_cast<float>((scaled - rounded) * 360.0);
}

float KISAK_CDECL AngleSubtract(float a1, float a2)
{
    return AngleDelta(a1, a2);
}
