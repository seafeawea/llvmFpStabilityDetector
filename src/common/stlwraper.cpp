#include "FpNode.h"

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
#include <unordered_map>
#include <vector>

#define PRINT_FLOAT_ERROR_ONCE "PRINT_FLOAT_ERROR"
#define OPEN_PRINT_ENV "OPEN_PRINT_FLOAT_ERROR"
#define FILE_TO_PRINT_ERROR_ENV "FILE_TO_PRINT_ERROR"

static bool is_open_print_float_error = (getenv(OPEN_PRINT_ENV) != NULL);
static bool is_write_to_file = (getenv(FILE_TO_PRINT_ERROR_ENV) != NULL);

using namespace std;

const char* __const__name__ = "__const__value__";

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
    max_relative_error_ = sum_relative_error_ = 0.0;
    max_relative_error_from_fpinfo = NULL;
    execute_count_ = 0;
    sum_of_cancelled_badness_bits_ = 0;
    //avg_cancelled_badness_bits_ = 0;
    sum_valid_bits_ = 0;
    // avg_valid_bits_ = 0.0;
  }

  string getTypeString() {
    switch (fop_type_) {
      case kFloatAdd:
        return "FloatAdd";
        break;
      case kFloatSub:
        return "FloatSub";
        break;
      case kFloatMul:
        return "FloatMul";
        break;
      case kFloatDiv:
        return "FloatDiv";
        break;
      case kDoubleAdd:
        return "DoubleAdd";
        break;
      case kDoubleSub:
        return "DoubleSub";
        break;
      case kDoubleMul:
        return "DoubleMul";
        break;
      case kDoubleDiv:
        return "DoubleDiv";
        break;
      default:
        assert(0 && "Invalid Float Op Type");
    }
  }
  // should be private in future
  int32_t line_;
  string name_;
  FOPType fop_type_;
  double max_relative_error_;
  double sum_relative_error_;
  // FpNode* max_relative_error_from_FpNode;
  int64_t sum_of_cancelled_badness_bits_;
  int64_t sum_valid_bits_;

  const FloatInstructionInfo* max_relative_error_from_fpinfo;
  //double avg_cancelled_badness_bits_;
  int64_t execute_count_;

  double getAvgCancelledBadnessBits() {
    return (double)sum_of_cancelled_badness_bits_ / (double)execute_count_;
  }

  double getAvgRelativeError() { return sum_relative_error_ / execute_count_; }

  double getAvgValidBits() {
    return (double)(sum_valid_bits_) / (double)(execute_count_);
  }

 private:
  // double avg_valid_bits_;
};

template <typename FpType>
inline double computeRelativeError(FpType value, FpType shadow_back_value) {
  return fabs((value - shadow_back_value) /
              max(fabs(shadow_back_value), (FpType)0.00001));
}

class FunctionFloatErrorInfo {
 public:
  // FunctionFloatErrorInfo(){};  // should't be called
  FunctionFloatErrorInfo(const char* name) : func_name(name) {}

  string func_name;
  unordered_map<string, FloatInstructionInfo> fp_instruction_Info_map;

  static unordered_map<int32_t, FunctionFloatErrorInfo> all_function_info;
};

unordered_map<int32_t, FunctionFloatErrorInfo>
    FunctionFloatErrorInfo::all_function_info;

extern "C" void initNewFunction(const char* name, const int32_t func_id) {
  unordered_map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  assert(all_function_info.find(func_id) == all_function_info.end() &&
         "can not init same name function");
  cout << "Init function " << name << " for Instrumentation" << endl;
  FunctionFloatErrorInfo temp_info(name);
  all_function_info.insert(
      pair<int32_t, FunctionFloatErrorInfo>(func_id, temp_info));
}

bool cmpByLine(FloatInstructionInfo* n1, FloatInstructionInfo* n2) {
  return n1->line_ < n2->line_;
}

void writeErrorToStream(ostream& out) {
  unordered_map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  out << "all_function_info size = " << all_function_info.size() << endl;

  for (auto ait = all_function_info.begin(); ait != all_function_info.end();
       ait++) {
    if (ait->second.func_name == "") continue;

    out << "=========================" << endl;
    out << "function id = " << ait->first << endl;
    out << "function :" << ait->second.func_name << endl;
    out.flags(ios::right);

    out << "      op     |    Type   | line | avg relative error | avg "
           "cancelled "
           "badness bits "
           "| avg valid bits | "
           " count  | Possable cause from "
        << endl;
    unordered_map<string, FloatInstructionInfo>& func_info =
        ait->second.fp_instruction_Info_map;
    vector<FloatInstructionInfo*> reorder_vector;
    for (auto fit = func_info.begin(); fit != func_info.end(); fit++) {
      reorder_vector.push_back(&(fit->second));
    }
    sort(reorder_vector.begin(), reorder_vector.end(), cmpByLine);
    for (auto fit = reorder_vector.begin(); fit != reorder_vector.end();
         fit++) {
      out << setw(12) << (*fit)->name_ << " | " << setw(9)
          << (*fit)->getTypeString() << " | " << setw(4) << (*fit)->line_
          << " | " << setw(18) << (*fit)->getAvgRelativeError() << " | "
          << setw(26) << (*fit)->getAvgCancelledBadnessBits() << " | "
          << setw(14) << (*fit)->getAvgValidBits() << " | " << setw(7)
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
  return (void*)(new unordered_map<string, vector<FpNode> >());
}

extern "C" void deleteFunctionFloatErrorInfo(void* ptr_fp_node_map) {
  delete (unordered_map<string, vector<FpNode> >*)ptr_fp_node_map;
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

template <typename T>
inline int32_t getExponent(T value) {
  int exp;
  frexp(value, &exp);
  return exp;
}

FpNode* getDoubleArgNode(void* ptr_fp_node_map, int32_t func_id, int32_t line,
                         const char* arg_name, double value) {
  string arg_name_str(arg_name);
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  FpNode* arg_node;
  if (arg_name_str == "__const__value__") {
    assert(arg_name = __const__name__);
    arg_node = FpNode::createConstantDouble(value);
  } else if (fp_node_map->find(arg_name_str) == fp_node_map->end()) {
    // first process node with whis name
    fp_node_map->insert(
        pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
    (*fp_node_map)[arg_name_str].emplace_back(FpNode::kDouble);
    arg_node = &((*fp_node_map)[arg_name_str].back());
    arg_node->d_value = value;
    mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
    arg_node->depth = 1;
    arg_node->valid_bits = 52;
    arg_node->line = line;
  } else {
    arg_node =
        &((*fp_node_map)[arg_name_str].back());  // just get the lastest node
    if (arg_node->d_value != value) {
      // must be a unknown function which produce the value

      fp_node_map->insert(
          pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
      (*fp_node_map)[arg_name_str].emplace_back(FpNode::kDouble);
      arg_node = &((*fp_node_map)[arg_name_str].back());
      arg_node->d_value = value;
      mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
      arg_node->depth = 1;
      arg_node->valid_bits = 52;
      arg_node->line = line;
    }
  }

  assert(arg_node->isDoubleTy());
  return arg_node;
}

FpNode* getFloatArgNode(void* ptr_fp_node_map, int32_t func_id, int32_t line,
                        const char* arg_name, float value) {
  string arg_name_str(arg_name);
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  FpNode* arg_node;
  if (arg_name_str == "__const__value__") {
    // arg_node = createConstantFloatIfNotExists(value);
    assert(arg_name = __const__name__);
    arg_node = FpNode::createConstantFloat(value);
  } else if (fp_node_map->find(arg_name_str) == fp_node_map->end()) {
    // first process node with whis name
    fp_node_map->insert(
        pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
    //(*fp_node_map)[arg_name_str].emplace_back(FpNode::kFloat);
    (*fp_node_map)[arg_name_str].emplace_back(FpNode::kFloat);
    arg_node = &((*fp_node_map)[arg_name_str].back());
    arg_node->f_value = value;
    mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
    arg_node->depth = 1;
    arg_node->valid_bits = 23;
    arg_node->line = line;
  } else {
    arg_node =
        &((*fp_node_map)[arg_name_str].back());  // just get the lastest node
    if (arg_node->f_value != value) {
      // must be a unknown function which produce the value

      fp_node_map->insert(
          pair<string, vector<FpNode> >(arg_name_str, vector<FpNode>()));
      (*fp_node_map)[arg_name_str].emplace_back(FpNode::kFloat);
      arg_node = &((*fp_node_map)[arg_name_str].back());
      arg_node->f_value = value;
      mpfr_set_d(arg_node->shadow_value, value, MPFR_RNDN);
      arg_node->depth = 1;
      arg_node->valid_bits = 23;
      arg_node->line = line;
    }
  }

  assert(arg_node->isFloatTy());
  return arg_node;
}

extern "C" void __storeDouble(void* ptr_fp_node_map, const char* from,
                              const char* to, int32_t func_id, int32_t line,
                              double from_val) {
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  string from_name_str(from);
  string to_name_str(to);
  FpNode* from_node;
  from_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, from, from_val);

  FpNode* to_node;
  if (fp_node_map->find(to_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[to_name_str].emplace_back(FpNode::kDouble);
  to_node = &((*fp_node_map)[to_name_str].back());
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

  assert(from_node->isDoubleTy());
  assert(to_node->isDoubleTy());
}

extern "C" void __doubleStoreToDoubleArrayElement(
    void* ptr_fp_node_map, const char* from, const char* to, int32_t func_id,
    int32_t line, double from_val, int64_t index) {
  string to_name_str(to);
  // to_name_str.append("[" + to_string(index) + "]");
  // for better performance
  to_name_str.append("[" + to_string(index));

  __storeDouble(ptr_fp_node_map, from, to_name_str.c_str(), func_id, line,
                from_val);
}

extern "C" void __doubleArrayElementStoreToDouble(
    void* ptr_fp_node_map, const char* from, const char* to, int32_t func_id,
    int32_t line, double from_val, int64_t index) {
  string from_name_str(from);
  // from_name_str.append("[" + to_string(index) + "]");
  from_name_str.append("[" + to_string(index));

  __storeDouble(ptr_fp_node_map, from_name_str.c_str(), to, func_id, line,
                from_val);
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

  if (result_node->isDoubleTy()) {
    double result_shadow_back_value =
        mpfr_get_d(result_node->shadow_value, MPFR_RNDN);
    result_node->real_error_to_shadow =
        result_shadow_back_value - result_node->d_value;
    result_node->relative_error_to_shadow =
        computeRelativeError(result_node->d_value, result_shadow_back_value);
    result_node->valid_bits =
        getDoubleValidBits(result_node->d_value, result_shadow_back_value);
  } else {
    float result_shadow_back_value =
        mpfr_get_flt(result_node->shadow_value, MPFR_RNDN);
    result_node->real_error_to_shadow =
        result_shadow_back_value - result_node->f_value;
    result_node->relative_error_to_shadow =
        computeRelativeError(result_node->f_value, result_shadow_back_value);
    result_node->valid_bits =
        getFloatValidBits(result_node->f_value, result_shadow_back_value);
  }

  result_node->depth = max(a_node->depth, b_node->depth) + 1;
  /* result_node->bits_canceled_by_this_instruction =
        max(getExponent(a_node->value),
            getExponent(b_node->value)) -
        getExponent(result_node->value); */
  if (result_node->isDoubleTy() &&
      result_node->bits_canceled_by_this_instruction > 52) {
    result_node->bits_canceled_by_this_instruction = 52;
  } else if (result_node->isFloatTy() &&
             result_node->bits_canceled_by_this_instruction > 23) {
    result_node->bits_canceled_by_this_instruction = 23;
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
}

void updateFloatOpInfo(const string& op_name, FpNode* op_node,
                       const int32_t func_id, const int32_t line,
                       FloatInstructionInfo::FOPType fop_type) {
  unordered_map<int32_t, FunctionFloatErrorInfo>& all_function_info =
      FunctionFloatErrorInfo::all_function_info;
  unordered_map<string, FloatInstructionInfo>& fp_instruction_Info_map =
      all_function_info.find(func_id)->second.fp_instruction_Info_map;

  // result_name is instruction name, for llvm use SSA
  FloatInstructionInfo* cur_fop_info;
  if (fp_instruction_Info_map.find(op_name) == fp_instruction_Info_map.end()) {
    fp_instruction_Info_map.insert(pair<string, FloatInstructionInfo>(
        op_name, FloatInstructionInfo(line, fop_type)));
    cur_fop_info = &fp_instruction_Info_map[op_name];
    cur_fop_info->name_ = op_name;

  } else {
    cur_fop_info = &fp_instruction_Info_map[op_name];
  }
  op_node->fp_info = cur_fop_info;
  cur_fop_info->execute_count_++;
  cur_fop_info->sum_of_cancelled_badness_bits_ +=
      op_node->bits_canceled_by_this_instruction;
  cur_fop_info->sum_relative_error_ += op_node->relative_error_to_shadow;
  /* cur_fop_info->avg_cancelled_badness_bits_ =
      (double)(cur_fop_info->sum_of_cancelled_badness_bits_) /
      (double)(cur_fop_info->execute_count_); */
  cur_fop_info->sum_valid_bits_ += op_node->valid_bits;
  // cur_fop_info->avg_valid_bits_ = (double)(cur_fop_info->sum_valid_bits_) /
  //                                (double)(cur_fop_info->execute_count_);
  if (op_node->relative_error_to_shadow > cur_fop_info->max_relative_error_) {
    cur_fop_info->max_relative_error_ = op_node->relative_error_to_shadow;

    if (op_node->relative_bigger_arg != NULL &&
        op_node->relative_bigger_arg->fp_info != NULL) {
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
  // map<string, vector<FpNode> >* fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kDouble);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;

  mpfr_add(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  result_node->bits_canceled_by_this_instruction =
      max(getExponent(a_node->d_value), getExponent(b_node->d_value)) -
      getExponent(result_node->d_value);
  if (result_node->bits_canceled_by_this_instruction < 0) {
    result_node->bits_canceled_by_this_instruction = 0;
  }

  updataResultNode(result_node);

  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kDoubleAdd);

  return r;
}

extern "C" double _fp_debug_doublesub(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a - b;
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kDouble);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_sub(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  if (a == 0.0 || b == 0.0) {
    result_node->bits_canceled_by_this_instruction = 0;
  } else {
    result_node->bits_canceled_by_this_instruction =
        max(getExponent(a_node->d_value), getExponent(b_node->d_value)) -
        getExponent(result_node->d_value);
  }

  if (result_node->bits_canceled_by_this_instruction < 0) {
    result_node->bits_canceled_by_this_instruction = 0;
  }

  updataResultNode(result_node);

  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kDoubleSub);

  return r;
}

extern "C" double _fp_debug_doublemul(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a * b;
  // map<string, vector<FpNode> >* fp_node_map =
  //     all_function_info[func_id].fp_node_map;
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kDouble);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;

  mpfr_mul(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  // result_node->bits_canceled_by_this_instruction = 0;
  result_node->bits_canceled_by_this_instruction =
      abs(a_node->valid_bits - b_node->valid_bits);

  updataResultNode(result_node);

  unordered_map<string, FloatInstructionInfo>& fp_instruction_Info_map =
      FunctionFloatErrorInfo::all_function_info.find(func_id)
          ->second.fp_instruction_Info_map;
  // result_name is instruction name, for llvm use SSA
  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kDoubleMul);

  return r;
}

extern "C" double _fp_debug_doublediv(void* ptr_fp_node_map, double a, double b,
                                      int32_t func_id, int32_t line,
                                      char* a_name, char* b_name,
                                      char* result_name) {
  double r = a / b;

  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  //assert(a_name_str != "");
  //assert(b_name_str != "");
  assert(result_name_str != "");

  FpNode* a_node;
  FpNode* b_node;
  a_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getDoubleArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kDouble);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->d_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_div(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);
  // result_node->bits_canceled_by_this_instruction = 0;
  result_node->bits_canceled_by_this_instruction =
      abs(a_node->valid_bits - b_node->valid_bits);
  updataResultNode(result_node);

  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kDoubleDiv);

  return r;
}

extern "C" void __storeFloat(void* ptr_fp_node_map, const char* from,
                             const char* to, int32_t func_id, int32_t line,
                             float from_val) {
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  string from_name_str(from);
  string to_name_str(to);
  FpNode* from_node;
  from_node = getFloatArgNode(ptr_fp_node_map, func_id, line, from, from_val);

  FpNode* to_node;
  if (fp_node_map->find(to_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[to_name_str].emplace_back(FpNode::kFloat);
  to_node = &((*fp_node_map)[to_name_str].back());
  to_node->f_value = from_node->f_value;
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

  assert(from_node->isFloatTy());
  assert(to_node->isFloatTy());
}

extern "C" void __doubleToFloat(void* ptr_fp_node_map, const char* from_name,
                                const char* to_name, int32_t func_id,
                                int32_t line, double from_val) {
  string from_name_str(from_name);
  string to_name_str(to_name);
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  FpNode* from_node;
  FpNode* to_node;

  from_node =
      getDoubleArgNode(ptr_fp_node_map, func_id, line, from_name, from_val);
  if (fp_node_map->find(to_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[to_name_str].emplace_back(FpNode::kFloat);
  to_node = &((*fp_node_map)[to_name_str].back());
  to_node->f_value = (float)(from_node->d_value);
  mpfr_copysign(to_node->shadow_value, from_node->shadow_value,
                from_node->shadow_value, MPFR_RNDN);
  to_node->first_arg = from_node->first_arg;
  to_node->second_arg = from_node->second_arg;
  to_node->depth = from_node->depth;
  if (from_node->valid_bits > 23) {
    to_node->valid_bits = 23;
  } else {
    to_node->valid_bits = from_node->valid_bits;
  }
  to_node->line = line;
  to_node->max_cancelled_badness_node = from_node->max_cancelled_badness_node;
  to_node->max_cancelled_badness_bits = from_node->max_cancelled_badness_bits;
  to_node->bits_canceled_by_this_instruction =
      from_node->bits_canceled_by_this_instruction;
  float to_shadow_back_value = mpfr_get_flt(to_node->shadow_value, MPFR_RNDN);
  to_node->real_error_to_shadow = to_node->f_value - to_shadow_back_value;
  to_node->relative_error_to_shadow =
      computeRelativeError(to_node->f_value, to_shadow_back_value);
  to_node->fp_info = from_node->fp_info;

  assert(from_node->isDoubleTy());
  assert(to_node->isFloatTy());
}

extern "C" void __floatToDouble(void* ptr_fp_node_map, const char* from_name,
                                const char* to_name, int32_t func_id,
                                int32_t line, float from_val) {
  string from_name_str(from_name);
  string to_name_str(to_name);
  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);

  FpNode* from_node;
  FpNode* to_node;

  from_node =
      getFloatArgNode(ptr_fp_node_map, func_id, line, from_name, from_val);
  if (fp_node_map->find(to_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(to_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[to_name_str].emplace_back(FpNode::kDouble);
  to_node = &((*fp_node_map)[to_name_str].back());
  to_node->d_value = (float)(from_node->f_value);
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
  double to_shadow_back_value = mpfr_get_d(to_node->shadow_value, MPFR_RNDN);
  to_node->real_error_to_shadow = to_node->d_value - to_shadow_back_value;
  to_node->relative_error_to_shadow =
      computeRelativeError(to_node->d_value, to_shadow_back_value);

  to_node->fp_info = from_node->fp_info;

  assert(from_node->isFloatTy());
  assert(to_node->isDoubleTy());
}

extern "C" float _fp_debug_floatadd(void* ptr_fp_node_map, float a, float b,
                                    int32_t func_id, int32_t line, char* a_name,
                                    char* b_name, char* result_name) {
  float r = a + b;

  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getFloatArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getFloatArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kFloat);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->f_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_add(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  if (a == 0.0 || b == 0.0) {
    result_node->bits_canceled_by_this_instruction = 0;
  } else {
    result_node->bits_canceled_by_this_instruction =
        max(getExponent(a_node->f_value), getExponent(b_node->f_value)) -
        getExponent(result_node->f_value);
  }

  if (result_node->bits_canceled_by_this_instruction < 0) {
    result_node->bits_canceled_by_this_instruction = 0;
  }

  updataResultNode(result_node);
  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kFloatAdd);

  return r;
}

extern "C" float _fp_debug_floatsub(void* ptr_fp_node_map, float a, float b,
                                    int32_t func_id, int32_t line, char* a_name,
                                    char* b_name, char* result_name) {
  float r = a - b;

  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getFloatArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getFloatArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kFloat);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->f_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_sub(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  if (a == 0.0 || b == 0.0) {
    result_node->bits_canceled_by_this_instruction = 0;
  } else {
    result_node->bits_canceled_by_this_instruction =
        max(getExponent(a_node->f_value), getExponent(b_node->f_value)) -
        getExponent(result_node->f_value);
  }

  if (result_node->bits_canceled_by_this_instruction < 0) {
    result_node->bits_canceled_by_this_instruction = 0;
  }

  updataResultNode(result_node);
  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kFloatSub);

  return r;
}

extern "C" float _fp_debug_floatmul(void* ptr_fp_node_map, float a, float b,
                                    int32_t func_id, int32_t line, char* a_name,
                                    char* b_name, char* result_name) {
  float r = a * b;

  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getFloatArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getFloatArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kFloat);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->f_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_mul(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  // result_node->bits_canceled_by_this_instruction = 0;
  result_node->bits_canceled_by_this_instruction =
      abs(a_node->valid_bits - b_node->valid_bits);

  updataResultNode(result_node);
  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kFloatMul);

  return r;
}

extern "C" float _fp_debug_floatdiv(void* ptr_fp_node_map, float a, float b,
                                    int32_t func_id, int32_t line, char* a_name,
                                    char* b_name, char* result_name) {
  float r = a / b;

  unordered_map<string, vector<FpNode> >* fp_node_map =
      reinterpret_cast<unordered_map<string, vector<FpNode> >*>(
          ptr_fp_node_map);
  //string a_name_str(a_name);
  //string b_name_str(b_name);
  string result_name_str(result_name);

  FpNode* a_node;
  FpNode* b_node;
  a_node = getFloatArgNode(ptr_fp_node_map, func_id, line, a_name, a);
  b_node = getFloatArgNode(ptr_fp_node_map, func_id, line, b_name, b);

  if (fp_node_map->find(result_name_str) == fp_node_map->end()) {
    fp_node_map->insert(
        pair<string, vector<FpNode> >(result_name_str, vector<FpNode>()));
  }
  (*fp_node_map)[result_name_str].emplace_back(FpNode::kFloat);

  FpNode* result_node = &((*fp_node_map)[result_name_str].back());
  result_node->f_value = r;
  result_node->first_arg = a_node;
  result_node->second_arg = b_node;
  result_node->line = line;
  mpfr_mul(result_node->shadow_value, a_node->shadow_value,
           b_node->shadow_value, MPFR_RNDN);

  // result_node->bits_canceled_by_this_instruction = 0;
  result_node->bits_canceled_by_this_instruction =
      abs(a_node->valid_bits - b_node->valid_bits);

  updataResultNode(result_node);
  updateFloatOpInfo(result_name_str, result_node, func_id, line,
                    FloatInstructionInfo::kFloatDiv);

  return r;
}