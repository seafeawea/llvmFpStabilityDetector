#include <iostream>
#include <map>
#include <string>
#include <vector>


using namespace std;

class FunctionFloatErrorInfo {
public:
    FunctionFloatErrorInfo() {}; // should't be called
    FunctionFloatErrorInfo(const char* name) 
        : func_name(name), line_and_count() {}
    string func_name;
    map<int,int> line_and_count; 
};

static map<int, FunctionFloatErrorInfo> all_function_info;

extern "C"
void initNewFunction(const char* name, int func_id) {
    cout << "Init Instrumentation function" <<name << endl;
    FunctionFloatErrorInfo temp_info(name);
    all_function_info.insert(pair<int, FunctionFloatErrorInfo>(func_id, temp_info));
}

extern "C"
void addErrorCount(const int func_id, const int line) {
    all_function_info[func_id].line_and_count[line]++;
}

extern "C"
void printFuncErrorInfo(const int func_id) {
    cout << "=========================" << endl;
    cout << all_function_info[func_id].func_name << endl;
    cout << "  Line  | ErrorCount" << endl;
    cout << "-------------------------" << endl;
    for (map<int, int>::iterator it = all_function_info[func_id].line_and_count.begin(); it != all_function_info[func_id].line_and_count.end(); it++) {
        cout << " " << it->first << "  | " << it->second << endl; 
    }
    cout << "=========================" << endl;
}
