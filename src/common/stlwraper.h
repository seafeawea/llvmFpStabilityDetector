#ifndef _STLWRAPER_H
#define _STLWRAPER_H

extern "C" void initNewFunction(const char* name, int func_id);
extern "C" void addErrorCount(const int func_id, const int line);
extern "C" void printFuncErrorInfo(const int func_id);

#endif
