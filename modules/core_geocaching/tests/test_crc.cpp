#include "sys/crc32.h"

int main()
{
    const char vector[] = "123456789";
    if (sys::crc32(vector, 9) != 0xcbf43926U || sys::crc32(nullptr, 0) != 0) return 1;
    for (unsigned split = 0; split <= 9; ++split)
        if (sys::crc32(vector + split, 9 - split, sys::crc32(vector, split)) != 0xcbf43926U) return 2;
    return 0;
}
