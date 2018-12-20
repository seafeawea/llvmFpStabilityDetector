#ifndef _STLWRAPER_H
#define _STLWRAPER_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void initNewFunction(const char* name, const int32_t func_id);
void checkAndPrintInfo(int32_t func_id);
void* createFpNodeMap();
void deleteFunctionFloatErrorInfo(void* ptr_fp_node_map);
double _fp_debug_doubleadd(void* ptr_fp_node_map, double a, double b,
                           int32_t func_id, int32_t line, char* a_name,
                           char* b_name, char* result_name);
double _fp_debug_doublesub(void* ptr_fp_node_map, double a, double b,
                           int32_t func_id, int32_t line, char* a_name,
                           char* b_name, char* result_name);
double _fp_debug_doublemul(void* ptr_fp_node_map, double a, double b,
                           int32_t func_id, int32_t line, char* a_name,
                           char* b_name, char* result_name);
double _fp_debug_doublediv(void* ptr_fp_node_map, double a, double b,
                           int32_t func_id, int32_t line, char* a_name,
                           char* b_name, char* result_name);

#if defined(__cplusplus)
}
#endif

#endif
