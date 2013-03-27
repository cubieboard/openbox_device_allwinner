#ifndef CAMERA_DEBUG_H
#define CAMERA_DEBUG_H

// #define LOG_NDEBUG 0
#include <cutils/log.h>

#define F_LOG LOGV("%s, line: %d", __FUNCTION__, __LINE__);

#ifdef __SUN4I__
#define USE_MP_CONVERT 1		// A10 can define 1, or must 0
#else
#define USE_MP_CONVERT 0		// A10 can define 1, or must 0
#endif


#endif // CAMERA_DEBUG_H

