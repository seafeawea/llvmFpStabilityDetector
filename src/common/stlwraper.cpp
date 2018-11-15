#include <fstream>
#include <iostream>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>


#define PRINT_FLOAT_ERROR_ONCE "PRINT_FLOAT_ERROR"
#define OPEN_PRINT_ENV "OPEN_PRINT_FLOAT_ERROR"
#define FILE_TO_PRINT_ERROR_ENV "FILE_TO_PRINT_ERROR"

static bool is_open_print_float_error = (getenv(OPEN_PRINT_ENV) != NULL);
static bool is_write_to_file = (getenv(FILE_TO_PRINT_ERROR_ENV) != NULL);

using namespace std;

class FunctionFloatErrorInfo {
public:
    FunctionFloatErrorInfo() {}; // should't be called
    FunctionFloatErrorInfo(const char* name) 
        : func_name(name), line_and_count() {}
    string func_name;
    map<int32_t,int32_t> line_and_count; 
};

static map<int32_t, FunctionFloatErrorInfo> all_function_info;

extern "C"
void initNewFunction(const char* name, const int32_t func_id) {
    cout << "Init function " << name << " for Instrumentation"<< endl;
    FunctionFloatErrorInfo temp_info(name);
    all_function_info[func_id] = temp_info;
}

void writeErrorToStream(ostream& out) {
    out << "all_function_info size = " << all_function_info.size() << endl;
    for (map<int, FunctionFloatErrorInfo>::iterator ait = all_function_info.begin(); ait != all_function_info.end(); ait++) {
        if (ait->second.func_name == "") continue;
        out << "=========================" << endl;
        out << "function id = " << ait->first << endl;
        out << "function :" << ait->second.func_name << endl;
        out << "  Line  | ErrorCount" << endl;
        out << "-------------------------" << endl;
        if (ait->second.line_and_count.size() == 0) {
            out << "No error in here" << endl;
        }
        else {
            for (map<int, int>::iterator it = ait->second.line_and_count.begin(); it != ait->second.line_and_count.end(); it++) {
                out << " " << it->first << "  | " << it->second << endl; 
            }
        }
        out << "=========================" << endl;
    }
}

extern "C"
__attribute__((destructor))
void printFuncErrorInfoAll() {
    if (is_write_to_file) {
        ofstream myfile;
        myfile.open(getenv(FILE_TO_PRINT_ERROR_ENV));
        writeErrorToStream(myfile);
        myfile.close();
    } else {
        writeErrorToStream(cout);
    }
}

extern "C"
void addErrorCount(const int32_t func_id, const int32_t line) {
    if(is_open_print_float_error) {
        if (__builtin_expect((remove(PRINT_FLOAT_ERROR_ONCE) == 0), 0)) {
            printFuncErrorInfoAll();
        }
    }
    all_function_info[func_id].line_and_count[line]++;
}



extern "C"
void printFuncErrorInfo(const int32_t func_id)  {
    cout << "=========================" << endl;
    cout << all_function_info[func_id].func_name << endl;
    cout << "  Line  | ErrorCount" << endl;
    cout << "-------------------------" << endl;
    for (map<int, int>::iterator it = all_function_info[func_id].line_and_count.begin(); it != all_function_info[func_id].line_and_count.end(); it++) {
        cout << " " << it->first << "  | " << it->second << endl; 
    }
    cout << "=========================" << endl;
}

