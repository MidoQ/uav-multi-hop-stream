#include "utils.h"

void delay(size_t seconds)
{
    struct timeval tmp;
    tmp.tv_sec = seconds;
    tmp.tv_usec = 0;
    select(0, nullptr, nullptr, nullptr, &tmp);
    return;
}

void udelay(size_t us)
{
    struct timeval tmp;
    tmp.tv_sec = 0;
    tmp.tv_usec = us;
    select(0, nullptr, nullptr, nullptr, &tmp);
    return;
}

void mdelay(size_t ms)
{
    struct timeval tmp;
    tmp.tv_sec = 0;
    tmp.tv_usec = ms * 1000;
    select(0, nullptr, nullptr, nullptr, &tmp);
    return;
}
