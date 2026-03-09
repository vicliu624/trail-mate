/*
  memtools.h
  June 2019

  Tools for looking at memory on the stm32.  See also debug_alloc.h
*/

#include <stdlib.h>
#include <sys/types.h>
#include <math.h>
#include "memtools.h"

/* Required memory allocation wrapper for embedded platforms. For SM1000, we can just use stdlib's memory functions. */
void* codec2_malloc(size_t size)
{
    return malloc(size);
}

void* codec2_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void codec2_free(void* ptr)
{
    free(ptr);
}

