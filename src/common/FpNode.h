#ifndef _FP_NODE_H
#define _FP_NODE_H

#include <map>
#include <mpfr.h>
#include <stdint.h>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace std;

class FloatInstructionInfo;

class FpNode {
 public:
  enum FpType { kUnknown = 0, kFloat, kDouble };
  enum OpType { kNone = 0, kAdd, kSub, kMul, kDiv };

 private:
  FpType fpTypeID;
  static unordered_map<double, FpNode> const_double_node_map;
  static unordered_map<float, FpNode> const_float_node_map;

 public:
  FpType getType() { return fpTypeID; }
  bool isFloatTy() { return fpTypeID == kFloat; }
  bool isDoubleTy() { return fpTypeID == kDouble; }
  bool isValid() { return fpTypeID != kUnknown; }

  int32_t line;
  int32_t valid_bits;

  double d_value;
  float f_value;
  mpfr_t shadow_value;
  int32_t depth;
  int32_t bits_canceled_by_this_instruction;
  int32_t cancelled_badness_bits;
  int32_t max_cancelled_badness_bits;
  double sum_relative_error;
  double real_error_to_shadow;
  double relative_error_to_shadow;

  FpNode* max_cancelled_badness_node;
  FpNode* first_arg;
  FpNode* second_arg;
  FpNode* relative_bigger_arg;
  FloatInstructionInfo* fp_info;

  

  static FpNode* createConstantDouble(double val);

  static FpNode* createConstantFloat(float val);

  FpNode(FpType fpty);

  FpNode(const FpNode& f);

  ~FpNode() { mpfr_clear(shadow_value); }
};

#endif // _FP_NODE_H
