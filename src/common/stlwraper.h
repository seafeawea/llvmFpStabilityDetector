#ifndef _STLWRAPER_H
#define _STLWRAPER_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void initNewFunction(const char* name, const int32_t func_id);
void addErrorCount(const int32_t func_id, const int32_t line);
void printFuncErrorInfo(const int32_t func_id);
void clearDoubleNodeMap(int32_t func_id);
double _fp_debug_doubleadd(double a, double b, int32_t func_id, int32_t line, char* a_name, char* b_name, char* result_name);

#if defined(__cplusplus)
}
#endif

#endif
