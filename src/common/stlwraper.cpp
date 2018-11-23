#include <assert.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <math.h>
#include <mpfr.h>
#include <sstream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>


#define PRINT_FLOAT_ERROR_ONCE "PRINT_FLOAT_ERROR"
#define OPEN_PRINT_ENV "OPEN_PRINT_FLOAT_ERROR"
#define FILE_TO_PRINT_ERROR_ENV "FILE_TO_PRINT_ERROR"

static bool is_open_print_float_error = (getenv(OPEN_PRINT_ENV) != NULL);
static bool is_write_to_file = (getenv(FILE_TO_PRINT_ERROR_ENV) != NULL);

using namespace std;

string DoubleConvertToString(double value) {
    stringstream ss;
    ss << value;
    return ss.str();
}

mpfr_prec_t PREC = 120;


class FpNode {
public:
    double value;
    mpfr_t shadow_value;
    int32_t depth;
    float valid_bits_num;
    float sum_relative_error;
    float cur_relative_error;
    
    FpNode* max_relative_error_node;
    FpNode* first_arg;
    FpNode* second_arg;

    FpNode() {
        mpfr_inits2(PREC, shadow_value, (mpfr_ptr) 0);
    }

    FpNode(const FpNode & f) {
        value = f.value;
        mpfr_inits2(PREC, shadow_value, (mpfr_ptr) 0);
        mpfr_copysign(shadow_value, f.shadow_value, f.shadow_value, MPFR_RNDN);
        double first = mpfr_get_d(shadow_value, MPFR_RNDN);
        double second = mpfr_get_d(f.shadow_value, MPFR_RNDN);
        if(isnan(first) && isnan(second))
            return;
        if(first != second) {
            cout << "first=" << first << " second=" << second << endl;
            assert(first == second && "mpfr_copy should work well");
        }
    }

    ~FpNode() {
        mpfr_clear(shadow_value);
    }
};

FpNode const_arg1;
FpNode const_arg2;
FpNode t0;

__attribute__((constructor))
void fp_init()
{
    //mpfr_inits2(PREC, const_arg1.shadow_value, const_arg2.shadow_value, t0.shadow_value, (mpfr_ptr) 0);
    //mpfr_set_d(t0.shadow_value, 0.0001, MPFR_RNDN);
}
//mpfr_inits2(PREC, const_arg1.shadow_value, const_arg2.shadow_value, (mpfr_ptr) 0);

map<string, FpNode> double_const_node_map;


class FunctionFloatErrorInfo {
public:
    FunctionFloatErrorInfo() {}; // should't be called
    FunctionFloatErrorInfo(const char* name) 
        : func_name(name), line_and_count() {}
    string func_name;
    map<int32_t,int32_t> line_and_count; 

    map<string, vector<FpNode> > fp_node_map;
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

extern "C"
void clearDoubleNodeMap(int32_t func_id) {
    all_function_info[func_id].fp_node_map.clear();
}

static char* mpfrToString(char* str, mpfr_t* fp) {
	int sgn = mpfr_sgn(*fp);
	if (sgn >= 0) {
		str[0] = ' '; str[1] = '0'; str[2] = '\0';
	} else {
		str[0] = '-'; str[1] = '\0';
	}

	char mpfr_str[60]; /* digits + 1 */
	mpfr_exp_t exp;
	/* digits_base10 = log10 ( 2^(significant bits) ) */
	mpfr_get_str(mpfr_str, &exp, /* base */ 10, /* digits, float: 7, double: 15 */ 15, *fp, MPFR_RNDN);
	exp--;
	strcat(str, mpfr_str);

	if (sgn >= 0) {
		str[1] = str[2];
		str[2] = '.';
	} else {
		str[1] = str[2];
		str[2] = '.';
	}

	char exp_str[50];
	sprintf(exp_str, " * 10^%ld", exp);
	strcat(str, exp_str);

	mpfr_prec_t pre_min = mpfr_min_prec(*fp);
	mpfr_prec_t pre = mpfr_get_prec(*fp);
	char pre_str[50];
    sprintf(pre_str, ", %ld/%ld bit", pre_min, pre);
	strcat(str, pre_str);
	return str;
}

FpNode* createConstantDoubleFPNodeIfNotExists(double val) {
    string value_str(DoubleConvertToString(val));
    FpNode* node;
    if (double_const_node_map.find(value_str) == double_const_node_map.end()) {
        double_const_node_map.insert(pair<string, FpNode>(value_str, FpNode()));
        node = &(double_const_node_map[value_str]);
        node->value = val;
        mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    }
    return &(double_const_node_map[value_str]);
}

extern "C"
void __storeDouble(char* from, char* to, int32_t func_id, double from_val) {
	printf("store %s = %s, func_id=%d, from_val=%f\n", to, from, func_id, from_val);

    map<string, vector<FpNode> >& fp_node_map = all_function_info[0].fp_node_map;

    string from_name_str(from);
    string to_name_str(to);
    FpNode* from_node;
    if (from_name_str == "__const__value__") {
        from_node = createConstantDoubleFPNodeIfNotExists(from_val);
    } else if (fp_node_map.find(from_name_str) == fp_node_map.end()) {
        fp_node_map.insert(pair<string, vector<FpNode> >(from_name_str, vector<FpNode>()));
        fp_node_map[from_name_str].push_back(FpNode());
        from_node = &(fp_node_map[from_name_str].back());
        from_node->value = from_val;
        mpfr_set_d(from_node->shadow_value, from_val, MPFR_RNDN);
    } else {
        from_node = &(fp_node_map[from_name_str].back());
    }
    
    FpNode* to_node;
    if (fp_node_map.find(to_name_str) == fp_node_map.end()) {
        fp_node_map.insert(pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
    } 
    fp_node_map[to_name_str].push_back(FpNode());
    to_node = &(fp_node_map[to_name_str].back());
    to_node->value = from_node->value;
    mpfr_copysign(to_node->shadow_value, from_node->shadow_value, from_node->shadow_value, MPFR_RNDN);
    to_node->first_arg = from_node;
}

FpNode* getArgNode(int32_t func_id, char* arg_name, double value) {
    string arg_name_str(arg_name);
    map<string, vector<FpNode> >& fp_node_map = all_function_info[func_id].fp_node_map;

    FpNode* arg_node;
    if (arg_name_str == "__const__value__") {
        arg_node = createConstantDoubleFPNodeIfNotExists(value);
    } else if (fp_node_map.find(arg_name_str) == fp_node_map.end()) {
        fp_node_map.insert(pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
        fp_node_map[arg_name_str].push_back(FpNode());
        arg_node = &(fp_node_map[arg_name_str].back());
        arg_node->value = value;
        mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
    } else {
        arg_node = &(fp_node_map[arg_name_str].back());   // just get the lastest node
    }

    return arg_node;
}

extern "C"
double _fp_debug_doubleadd(double a, double b, int32_t func_id, int32_t line, char* a_name, char* b_name, char* result_name) {
    double r = a + b;
    map<string, vector<FpNode> >& fp_node_map = all_function_info[func_id].fp_node_map;
    string a_name_str(a_name);
    string b_name_str(b_name);
    string result_name_str(result_name);

    FpNode* a_node;
    FpNode* b_node;
    a_node = getArgNode(func_id, a_name, a);
    b_node = getArgNode(func_id, b_name, b);

    if (fp_node_map.find(result_name_str) == fp_node_map.end()) {
        fp_node_map.insert(pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
    } 
    fp_node_map[result_name_str].push_back(FpNode());
    
    FpNode* result_node = &(fp_node_map[result_name_str].back());
    result_node->value = r;
    result_node->first_arg = a_node;
    result_node->second_arg = b_node;
    //mpfr_inits2(PREC, result_node->shadow_value, (mpfr_ptr) 0);
    mpfr_add(result_node->shadow_value, a_node->shadow_value, b_node->shadow_value, MPFR_RNDN);

    char debug_str[50];
    mpfrToString(debug_str, &(result_node->shadow_value));
    cout << "fadd result:" << debug_str << endl;
    return r;
}

