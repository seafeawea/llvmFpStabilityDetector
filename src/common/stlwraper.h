#ifndef _STLWRAPER_H
#define _STLWRAPER_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void initNewFunction(const char* name, const int32_t func_id);
void addErrorCount(const int32_t func_id, const int32_t line);
void printFuncErrorInfo(const int32_t func_id);

#if defined(__cplusplus)
}
#endif

#endif
