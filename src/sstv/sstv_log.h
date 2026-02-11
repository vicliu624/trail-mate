#pragma once

#include <Arduino.h>

#ifndef SSTV_DEBUG
#define SSTV_DEBUG 1
#endif

#if SSTV_DEBUG
#define SSTV_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define SSTV_LOG(...) ((void)0)
#endif

#ifndef SSTV_VERBOSE
#define SSTV_VERBOSE 0
#endif

#if SSTV_VERBOSE
#define SSTV_LOG_V(...) SSTV_LOG(__VA_ARGS__)
#else
#define SSTV_LOG_V(...) ((void)0)
#endif

#ifndef SSTV_VIS_DIAG
#define SSTV_VIS_DIAG 1
#endif

#if SSTV_VIS_DIAG
#define SSTV_LOG_VIS(...) SSTV_LOG(__VA_ARGS__)
#else
#define SSTV_LOG_VIS(...) ((void)0)
#endif

#ifndef SSTV_TONE_TEST
#define SSTV_TONE_TEST 0
#endif

#if SSTV_TONE_TEST
#define SSTV_LOG_TONE(...) SSTV_LOG(__VA_ARGS__)
#else
#define SSTV_LOG_TONE(...) ((void)0)
#endif
