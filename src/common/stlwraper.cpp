#include <assert.h>
#include <math.h>
#include <mpfr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define PRINT_FLOAT_ERROR_ONCE "PRINT_FLOAT_ERROR"
#define OPEN_PRINT_ENV "OPEN_PRINT_FLOAT_ERROR"
#define FILE_TO_PRINT_ERROR_ENV "FILE_TO_PRINT_ERROR"

static bool is_open_print_float_error = (getenv(OPEN_PRINT_ENV) != NULL);
static bool is_write_to_file = (getenv(FILE_TO_PRINT_ERROR_ENV) != NULL);

using namespace std;

class FloatInstructionInfo;

template <typename T>
inline string ConvertToString(T value) {
  stringstream ss;
  ss << value;
  return ss.str();
}

mpfr_prec_t PREC = 120;

class FpNode {
 public:
  enum FpType { kUnknown = 0, kFloat, kDouble };

  FpType ID;
  bool isFloatTy() { return ID == kFloat; }
  bool isDoubleTy() { return ID == kDouble; }

  double d_value;
  float f_value;
  mpfr_t shadow_value;
  int32_t depth;
  int32_t valid_bits;
  int32_t bits_canceled_by_this_instruction;
  int32_t cancelled_badness_bits;
  int32_t max_cancelled_badness_bits;
  int32_t line;
  double sum_relative_error;
  // double cur_relative_error;
  double real_error_to_shadow;
  double relative_error_to_shadow;

  FpNode* max_cancelled_badness_node;
  FpNode* first_arg;
  FpNode* second_arg;
  FpNode* relative_bigger_arg;
  FloatInstructionInfo* fp_info;

  static map<string, FpNode> const_node_map;

  FpNode(FpType ty) {
    mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
    ID = ty;
    d_value = 0.0;
    f_value = 0.0f;
    sum_relative_error = real_error_to_shadow = relative_error_to_shadow = 0.0;
    first_arg = second_arg = NULL;
    max_cancelled_badness_node = this;
    relative_bigger_arg = NULL;
    fp_info = NULL;
    depth = 1;
    valid_bits = 52;
    bits_canceled_by_this_instruction = 0;
    cancelled_badness_bits = 0;
    max_cancelled_badness_bits = 0;
    line = 0;
  }

  FpNode(const FpNode& f) {
    d_value = f.d_value;
    f_value = f.f_value;
    ID = f.ID;
    mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
    mpfr_copysign(shadow_value, f.shadow_value, f.shadow_value, MPFR_RNDN);
    valid_bits = f.valid_bits;
    double first = mpfr_get_d(shadow_value, MPFR_RNDN);
    double second = mpfr_get_d(f.shadow_value, MPFR_RNDN);
    max_cancelled_badness_node = this;
    fp_info = f.fp_info;
    first_arg = f.first_arg;
    second_arg = f.second_arg;
    relative_bigger_arg = f.relative_bigger_arg;
    bits_canceled_by_this_instruction = f.bits_canceled_by_this_instruction;
    if (isnan(first) && isnan(second)) return;
    if (first != second) {
      cout << "first=" << first << " second=" << second << endl;
      assert(first == second && "mpfr_copy should work well");
    }
  }

  ~FpNode() { mpfr_clear(shadow_value); }
};

map<string, FpNode> FpNode::const_node_map;

class FloatInstructionInfo {
 public:
  enum FOPType {
    kFloatAdd = 0,
    kFloatSub,
    kFloatMul,
    kFloatDiv,
    kDoubleAdd,
    kDoubleSub,
    kDoubleMul,
    kDoubleDiv
  };

  FloatInstructionInfo() {}

  FloatInstructionInfo(int32_t line, FOPType fop_type)
      : line_(line), fop_type_(fop_type) {
    avg_relative_error_ = max_relative_error_ = sum_relative_error_ = 0.0;
    max_relative_error_from_fpinfo = NULL;
    execute_count_ = 0;
    sum_of_cancelled_badness_bits_ = 0;
    avg_cancelled_badness_bits_ = 0;
    sum_valid_bits_ = 0;
    avg_valid_bits_ = 0.0;
  }
  // private:
  int32_t line_;
  string name_;
  FOPType fop_type_;
  double avg_relative_error_;
  double max_relative_error_;
  double sum_relative_error_;
  // FpNode* max_relative_error_from_FpNode;
  int64_t sum_of_cancelled_badness_bits_;
  int64_t sum_valid_bits_;
  double avg_valid_bits_;
  const FloatInstructionInfo* max_relative_error_from_fpinfo;
  double avg_cancelled_badness_bits_;
  int64_t execute_count_;
};

template <typename FpType>
inline double computeRelativeError(FpType value, FpType shadow_back_value) {
  return fabs((value - shadow_back_value) /
              max(fabs(shadow_back_value), 0.00001));
}

class FunctionFloatErrorInfo {
 public:
  // FunctionFloatErrorInfo(){};  // should't be called
  FunctionFloatErrorInfo(const char* name) : func_name(name) {}

  string func_name;
  map<string, FloatInstructionInfo> fp_instruction_Info_map;

  static map<int32_t, FunctionFloatErrorInfo> all_function_info;
};

map<int32_t, FunctionFloatErrorInfo> FunctionFloatErrorInfo::all_function_info;

extern "C" void initNewFunction(const char* name, const int32_t func_id) {
  map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  assert(all_function_info.find(func_id) == all_function_info.end() &&
         "can not init same name function");
  cout << "Init function " << name << " for Instrumentation" << endl;
  FunctionFloatErrorInfo temp_info(name);
  all_function_info.insert(
      pair<int32_t, FunctionFloatErrorInfo>(func_id, temp_info));
}

bool cmp(FloatInstructionInfo* n1, FloatInstructionInfo* n2) {
  return n1->line_ < n2->line_;
}

void writeErrorToStream(ostream& out) {
  map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  out << "all_function_info size = " << all_function_info.size() << endl;

  for (map<int, FunctionFloatErrorInfo>::iterator ait =
           all_function_info.begin();
       ait != all_function_info.end(); ait++) {
    if (ait->second.func_name == "") continue;

    out << "=========================" << endl;
    out << "function id = " << ait->first << endl;
    out << "function :" << ait->second.func_name << endl;
    out.flags(ios::right);

    out << "    op    | line | avg relative error | avg cancelled badness bits "
           "| avg valid bits | "
           " count  | Possable cause from "
        << endl;
    map<string, FloatInstructionInfo>& func_info =
        ait->second.fp_instruction_Info_map;
    vector<FloatInstructionInfo*> reorder_vector;
    for (map<string, FloatInstructionInfo>::iterator fit = func_info.begin();
         fit != func_info.end(); fit++) {
      reorder_vector.push_back(&(fit->second));
    }
    sort(reorder_vector.begin(), reorder_vector.end(), cmp);
    for (vector<FloatInstructionInfo*>::iterator fit = reorder_vector.begin();
         fit != reorder_vector.end(); fit++) {
      out << setw(9) << (*fit)->name_ << " | " << setw(4) << (*fit)->line_
          << " | " << setw(18) << (*fit)->avg_relative_error_ << " | "
          << setw(26) << (*fit)->avg_cancelled_badness_bits_ << " | "
          << setw(14) << (*fit)->avg_valid_bits_ << " | " << setw(7)
          << (*fit)->execute_count_;
      out << " | ";
      if ((*fit)->max_relative_error_from_fpinfo != NULL) {
        out << setw(18) << (*fit)->max_relative_error_from_fpinfo->name_;
      } else {
        out << setw(18) << " ";
      }

      out << endl;
    }
    out << "=========================" << endl;
  }
}

extern "C" __attribute__((destructor)) void printFuncErrorInfoAll() {
  if (is_write_to_file) {
    ofstream myfile;
    myfile.open(getenv(FILE_TO_PRINT_ERROR_ENV));
    writeErrorToStream(myfile);
    myfile.close();
  } else {
    writeErrorToStream(cout);
  }
}

extern "C" void checkAndPrintInfo(int32_t func_id) {
  if (is_open_print_float_error) {
    if (__builtin_expect((remove(PRINT_FLOAT_ERROR_ONCE) == 0), 0)) {
      printFuncErrorInfoAll();
    }
  }
}

extern "C" void* createFpNodeMap() {
  return (void*)(new map<string, vector<FpNode> >());
}

extern "C" void deleteFunctionFloatErrorInfo(void* ptr_fp_node_map) {
  delete (map<string, vector<FpNode> >*)ptr_fp_node_map;
}

static char* mpfrToString(char* str, mpfr_t* fp) {
  int sgn = mpfr_sgn(*fp);
  if (sgn >= 0) {
    str[0] = ' ';
    str[1] = '0';
    str[2] = '\0';
  } else {
    str[0] = '-';
    str[1] = '\0';
  }

  char mpfr_str[60]; /* digits + 1 */
  mpfr_exp_t exp;
  /* digits_base10 = log10 ( 2^(significant bits) ) */
  mpfr_get_str(mpfr_str, &exp, /* base */ 10,
               /* digits, float: 7, double: 15 */ 15, *fp, MPFR_RNDN);
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

inline int32_t getDoubleValidBits(double a, double b) {
  int a_exp, b_exp;
  double a_significand, b_significand;
  a_significand = frexp(a, &a_exp);
  b_significand = frexp(b, &b_exp);
  if (a_exp != b_exp) {
    return 0;
  } else {
    if (a_significand == b_significand) {
      return 52;
    } else {
      int diff_exp;
      double diff_significand;
      diff_significand = frexp(a - b, &diff_exp);
      return min(52, abs(b_exp - diff_exp));
    }
  }
}

inline int32_t getFloatValidBits(float a, float b) {
  int a_exp, b_exp;
  float a_significand, b_significand;
  a_significand = frexp(a, &a_exp);
  b_significand = frexp(b, &b_exp);
  if (a_exp != b_exp) {
    return 0;
  } else {
    if (a_significand == b_significand) {
      return 23;
    } else {
      int diff_exp;
      float diff_significand;
      diff_significand = frexp(a - b, &diff_exp);
      return min(23, abs(b_exp - diff_exp));
    }
  }
}

inline int32_t getDoubleExponent(double value) {
  int exp;
  frexp(value, &exp);
  return exp;
}

inline int32_t getFloatExponent(float value) {
  int exp;
  frexp(value, &exp);
  return exp;
}

FpNode* createConstantDoubleFPNodeIfNotExists(double val) {
  string value_str(ConvertToString(val));
  FpNode* node;
  map<string, FpNode>::iterator it = FpNode::const_node_map.find(value_str);
  if (it == FpNode::const_node_map.end()) {
    pair<map<string, FpNode>::iterator, bool> ret;
    ret = FpNode::const_node_map.insert(
        pair<string, FpNode>(value_str, FpNode(FpNode::kDouble)));
    node = &(ret.first->second);
    node->d_value = val;
    mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    node->depth = 1;
    node->valid_bits = 52;  // suppose constant always accurate
    return node;
  }
  // return &(const_node_map[value_str]);
  return &(it->second);
}

extern "C" void __storeDouble(void* ptr_fp_node_map, const char* from,
                              const char* to, int32_t func_id, int32_t line,
                              double from_val) {
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;

  string from_name_str(from);
  string to_name_str(to);
  FpNode* from_node;
  if (from_name_str == "__const__value__") {
    from_node = createConstantDoubleFPNodeIfNotExists(from_val);
  } else if (fp_node_map.find(from_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(from_name_str, vector<FpNode>()));
    fp_node_map[from_name_str].push_back(FpNode(FpNode::kDouble));
    from_node = &(fp_node_map[from_name_str].back());
    from_node->d_value = from_val;
    mpfr_set_d(from_node->shadow_value, from_val, MPFR_RNDN);
    from_node->depth = 1;
    from_node->valid_bits = 52;
    from_node->line = line;
  } else {
    from_node = &(fp_node_map[from_name_str].back());
  }

  FpNode* to_node;
  if (fp_node_map.find(to_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
  }
  fp_node_map[to_name_str].push_back(FpNode(FpNode::kDouble));
  to_node = &(fp_node_map[to_name_str].back());
  to_node->d_value = from_node->d_value;
  mpfr_copysign(to_node->shadow_value, from_node->shadow_value,
                from_node->shadow_value, MPFR_RNDN);
  to_node->first_arg = from_node->first_arg;
  to_node->second_arg = from_node->second_arg;
  to_node->depth = from_node->depth;
  to_node->valid_bits = from_node->valid_bits;
  to_node->line = line;
  to_node->max_cancelled_badness_node = from_node->max_cancelled_badness_node;
  to_node->max_cancelled_badness_bits = from_node->max_cancelled_badness_bits;
  to_node->bits_canceled_by_this_instruction =
      from_node->bits_canceled_by_this_instruction;
  to_node->real_error_to_shadow = from_node->real_error_to_shadow;
  to_node->relative_error_to_shadow = from_node->relative_error_to_shadow;
  to_node->fp_info = from_node->fp_info;
}

extern "C" void __doubleStoreToDoubleArrayElement(
    void* ptr_fp_node_map, const char* from, const char* to, int32_t func_id,
    int32_t line, double from_val, int64_t index) {
  string to_name_str(to);
  to_name_str.append("[" + ConvertToString(index) + "]");

  __storeDouble(ptr_fp_node_map, from, to_name_str.c_str(), func_id, line,
                from_val);
}

extern "C" void __doubleArrayElementStoreToDouble(
    void* ptr_fp_node_map, const char* from, const char* to, int32_t func_id,
    int32_t line, double from_val, int64_t index) {
  string from_name_str(from);
  from_name_str.append("[" + ConvertToString(index) + "]");

  __storeDouble(ptr_fp_node_map, from_name_str.c_str(), to, func_id, line,
                from_val);
}

FpNode* getArgNode(void* ptr_fp_node_map, int32_t func_id, int32_t line,
                   char* arg_name, double value) {
  string arg_name_str(arg_name);
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;

  FpNode* arg_node;
  if (arg_name_str == "__const__value__") {
    arg_node = createConstantDoubleFPNodeIfNotExists(value);
  } else if (fp_node_map.find(arg_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
    fp_node_map[arg_name_str].push_back(FpNode(FpNode::kDouble));
    arg_node = &(fp_node_map[arg_name_str].back());
    arg_node->d_value = value;
    mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
    arg_node->depth = 1;
    arg_node->line = line;
  } else {
    arg_node =
        &(fp_node_map[arg_name_str].back());  // just get the lastest node
    if (arg_node->d_value != value) {
      // must be a unknown function which produce the value

      // cout << arg_node->value << " != " << value << endl;
      // assert(arg_node->value == value &&
      //        "get arg_node value shoule be equal to arg value");

      fp_node_map.insert(
          pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
      fp_node_map[arg_name_str].push_back(FpNode(FpNode::kDouble));
      arg_node = &(fp_node_map[arg_name_str].back());
      arg_node->d_value = value;
      mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
      arg_node->depth = 1;
      arg_node->line = 0;
    }
  }

  return arg_node;
}

void printResultNodePath(const FpNode& result_node) {
  int32_t cur_depth = result_node.depth;
  const FpNode* cur_node = &result_node;
  cout << "-------Cancellation Error Trace-------" << endl;
  while (cur_depth > 1) {
    cout << "line:" << cur_node->line
         << " | Valid Bit: " << cur_node->valid_bits << endl;
    if (cur_node->second_arg == NULL) {
      cur_node = cur_node->first_arg;
      cur_depth = cur_node->depth;
      continue;
    } else {
      if (cur_node->first_arg->valid_bits > cur_node->second_arg->valid_bits) {
        cur_node = cur_node->second_arg;
      } else {
        cur_node = cur_node->first_arg;
      }
      cur_depth = cur_node->depth;
    }
  }
  cout << "line:" << cur_node->line << " | Valid Bit: " << cur_node->valid_bits
       << endl;
  cout << "-------End-------" << endl;
}

void updataResultNode(FpNode* result_node) {
  FpNode* a_node = result_node->first_arg;
  FpNode* b_node = result_node->second_arg;
  double result_shadow_back_value =
      mpfr_get_d(result_node->shadow_value, MPFR_RNDN);
  result_node->real_error_to_shadow =
      result_shadow_back_value - result_node->d_value;
  result_node->relative_error_to_shadow =
      computeRelativeError(result_node->d_value, result_shadow_back_value);
  result_node->depth = max(a_node->depth, b_node->depth) + 1;

  // cout << result_node->value << " --- " << result_shadow_back_value << endl;
  result_node->valid_bits =
      getDoubleValidBits(result_node->d_value, result_shadow_back_value);

  /* result_node->bits_canceled_by_this_instruction =
      max(getDoubleExponent(a_node->value),
          getDoubleExponent(b_node->value)) -
      getDoubleExponent(result_node->value); */
  if (result_node->bits_canceled_by_this_instruction > 52) {
    /*     cout << result_node->value << " = " << a_node->value << " op "
             << b_node->value << " | bits canceled = "
             << result_node->bits_canceled_by_this_instruction << endl; */
    result_node->bits_canceled_by_this_instruction = 52;
  }
  result_node->cancelled_badness_bits =
      1 + result_node->bits_canceled_by_this_instruction -
      min(a_node->valid_bits, b_node->valid_bits);
  result_node->max_cancelled_badness_bits = result_node->cancelled_badness_bits;
  if (result_node->max_cancelled_badness_bits >
          a_node->max_cancelled_badness_bits &&
      result_node->max_cancelled_badness_bits >
          b_node->max_cancelled_badness_bits) {
    result_node->max_cancelled_badness_node = result_node;
  } else if (a_node->max_cancelled_badness_bits >
             b_node->max_cancelled_badness_bits) {
    result_node->max_cancelled_badness_bits = a_node->cancelled_badness_bits;
    result_node->max_cancelled_badness_node =
        a_node->max_cancelled_badness_node;
  } else {
    result_node->max_cancelled_badness_bits = b_node->cancelled_badness_bits;
    result_node->max_cancelled_badness_node =
        b_node->max_cancelled_badness_node;
  }

  if (a_node->real_error_to_shadow > b_node->real_error_to_shadow) {
    result_node->relative_bigger_arg = a_node;
  } else {
    result_node->relative_bigger_arg = b_node;
  }

  /* if (result_node->cancelled_badness_bits > 0) {
    cout << " Catastrophic Cancellation in fsub, line:" << result_node->line
         << " ,origin from line:"
         << result_node->max_cancelled_badness_node->line << endl;
    printResultNodePath(*result_node);
  } else if (result_node->cancelled_badness_bits > -15) {
    cout << " Slightly Cancellation in fsub, line:" << result_node->line
         << " ,origin from line:"
         << result_node->max_cancelled_badness_node->line << endl;
    printResultNodePath(*result_node);
  } */
}

void updateFloatOpInfo(const string& op_name, FpNode* op_node,
                       const int32_t func_id, const int32_t line) {
  map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  map<string, FloatInstructionInfo>& fp_instruction_Info_map =
      all_function_info.find(func_id)->second.fp_instruction_Info_map;
  // all_function_info[func_id].fp_instruction_Info_map;
  // result_name is instruction name, for llvm use SSA
  FloatInstructionInfo* cur_fop_info;
  if (fp_instruction_Info_map.find(op_name) == fp_instruction_Info_map.end()) {
    fp_instruction_Info_map.insert(pair<string, FloatInstructionInfo>(
        op_name, FloatInstructionInfo(line, FloatInstructionInfo::kDoubleAdd)));
    cur_fop_info = &fp_instruction_Info_map[op_name];
    cur_fop_info->name_ = op_name;
    // cout << cur_fop_info << endl;
  } else {
    cur_fop_info = &fp_instruction_Info_map[op_name];
  }
  op_node->fp_info = cur_fop_info;
  cur_fop_info->execute_count_++;
  cur_fop_info->sum_of_cancelled_badness_bits_ +=
      op_node->bits_canceled_by_this_instruction;
  cur_fop_info->sum_relative_error_ += op_node->relative_error_to_shadow;
  cur_fop_info->avg_relative_error_ =
      cur_fop_info->sum_relative_error_ / cur_fop_info->execute_count_;
  cur_fop_info->avg_cancelled_badness_bits_ =
      (double)(cur_fop_info->sum_of_cancelled_badness_bits_) /
      (double)(cur_fop_info->execute_count_);
  cur_fop_info->sum_valid_bits_ += op_node->valid_bits;
  cur_fop_info->avg_valid_bits_ = (double)(cur_fop_info->sum_valid_bits_) /
                                  (double)(cur_fop_info->execute_count_);
  if (op_node->relative_error_to_shadow > cur_fop_info->max_relative_error_) {
    cur_fop_info->max_relative_error_ = op_node->relative_error_to_shadow;
    // cur_fop_info->max_relative_error_from_ = result_node->
    // cur_fop_info->max_relative_error_from_FpNode = op_node;
    if (op_node->relative_bigger_arg != NULL &&
        op_node->relative_bigger_arg->fp_info != NULL) {
      // cout << "In line " << __LINE__ << " "
      //     << op_node->relative_bigger_arg->fp_info << endl;
      cur_fop_info->max_relative_error_from_fpinfo =
          op_node->relative_bigger_arg->fp_info;
    }
  }
}

extern "C" double _fp_debug_doubleadd(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a + b;
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;
  string a_name_str(a_name);
  string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map.find(result_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  fp_node_map[result_name_str].push_back(FpNode(FpNode::kDouble));

  FpNode* result_node = &(fp_node_map[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  // mpfr_inits2(PREC, result_node->shadow_value, (mpfr_ptr) 0);
  mpfr_add(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  result_node->bits_canceled_by_this_instruction =
      max(getDoubleExponent(a_node->d_value),
          getDoubleExponent(b_node->d_value)) -
      getDoubleExponent(result_node->d_value);

  updataResultNode(result_node);
  /*
      int result_value_exp, result_shadow_back_value_exp;
      double result_value_significand, result_shadow_back_value_significand;
      result_value_significand = frexp (result_node->value , &result_value_exp);
      result_shadow_back_value_exp = frexp (result_shadow_back_value ,
     &result_shadow_back_value_exp); if (result_value_exp !=
     result_shadow_back_value_exp) { result_node->valid_bits = 0; } else { if
     (result_value_significand == result_shadow_back_value_significand) {
              result_node->valid_bits = 52;
          } else {
              int diff_exp;
              double diff_significand;
              diff_significand = frexp (result_node->real_error_to_shadow ,
     &diff_exp); result_node->valid_bits = min(52,
     abs(result_shadow_back_value_exp - diff_exp));
          }
      }
  */

  char debug_str[50];
  /* mpfrToString(debug_str, &(result_node->shadow_value));
  cout << "fadd result:" << debug_str
       << " | cancel bits:" << result_node->bits_canceled_by_this_instruction
       << " | canceled badness bits:" << result_node->cancelled_badness_bits
       << endl; */

  updateFloatOpInfo(result_name_str, result_node, func_id, line);

  return r;
}

extern "C" double _fp_debug_doublesub(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a - b;
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;
  string a_name_str(a_name);
  string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map.find(result_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  fp_node_map[result_name_str].push_back(FpNode(FpNode::kDouble));

  FpNode* result_node = &(fp_node_map[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  // mpfr_inits2(PREC, result_node->shadow_value, (mpfr_ptr) 0);
  mpfr_sub(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  result_node->bits_canceled_by_this_instruction =
      max(getDoubleExponent(a_node->d_value),
          getDoubleExponent(b_node->d_value)) -
      getDoubleExponent(result_node->d_value);

  updataResultNode(result_node);

  /* char debug_str[50];
  mpfrToString(debug_str, &(result_node->shadow_value));
  cout << "fsub result:" << debug_str
       << " | cancel bits:" << result_node->bits_canceled_by_this_instruction
       << " | canceled badness bits:" << result_node->cancelled_badness_bits
       << endl; */
  updateFloatOpInfo(result_name_str, result_node, func_id, line);

  return r;
}

extern "C" double _fp_debug_doublemul(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a * b;
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;
  string a_name_str(a_name);
  string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map.find(result_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  fp_node_map[result_name_str].push_back(FpNode(FpNode::kDouble));

  FpNode* result_node = &(fp_node_map[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  // mpfr_inits2(PREC, result_node->shadow_value, (mpfr_ptr) 0);
  mpfr_mul(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  result_node->bits_canceled_by_this_instruction = 0;

  updataResultNode(result_node);

  /* char debug_str[50];
  mpfrToString(debug_str, &(result_node->shadow_value));
  cout << "fsub result:" << debug_str
       << " | cancel bits:" << result_node->bits_canceled_by_this_instruction
       << " | canceled badness bits:" << result_node->cancelled_badness_bits
       << endl; */

  map<string, FloatInstructionInfo>& fp_instruction_Info_map =
      FunctionFloatErrorInfo::all_function_info.find(func_id)
          ->second.fp_instruction_Info_map;
  // result_name is instruction name, for llvm use SSA
  updateFloatOpInfo(result_name_str, result_node, func_id, line);

  return r;
}

extern "C" double _fp_debug_doublediv(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a / b;
  // map<string, vector<FpNode> >& fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  map<string, vector<FpNode> >& fp_node_map =
      *(map<string, vector<FpNode> >*)ptr_fp_node_map;
  string a_name_str(a_name);
  string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map.find(result_name_str) == fp_node_map.end()) {
    fp_node_map.insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  fp_node_map[result_name_str].push_back(FpNode(FpNode::kDouble));

  FpNode* result_node = &(fp_node_map[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  // mpfr_inits2(PREC, result_node->shadow_value, (mpfr_ptr) 0);
  mpfr_div(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  result_node->bits_canceled_by_this_instruction = 0;

  updataResultNode(result_node);

  /* char debug_str[50];
  mpfrToString(debug_str, &(result_node->shadow_value));
  cout << "fsub result:" << debug_str
       << " | cancel bits:" << result_node->bits_canceled_by_this_instruction
       << " | canceled badness bits:" << result_node->cancelled_badness_bits
       << endl; */
  updateFloatOpInfo(result_name_str, result_node, func_id, line);

  return r;
}