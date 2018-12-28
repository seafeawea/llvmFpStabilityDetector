#ifndef _FP_NODE_H
#define _FP_NODE_H

#include <assert.h>
#include <mpfr.h>
#include <stdint.h>
#include <map>
#include <memory>
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
  static unordered_map<double, shared_ptr<FpNode> > const_double_node_map;
  static unordered_map<float, shared_ptr<FpNode> > const_float_node_map;

 public:
  FpType getType() { return fpTypeID; }
  bool isFloatTy() const { return fpTypeID == kFloat; }
  bool isDoubleTy() const { return fpTypeID == kDouble; }
  bool isValid() const { return fpTypeID != kUnknown; }

  string getTypeString() const {
    switch (fpTypeID) {
      case kFloat:
        return "Float";
        break;
      case kDouble:
        return "Double";
        break;
      default:
        assert(0 && "Invalid Float Op Type");
    }
  }

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

  uint64_t debug_identifier;

  static FpNode* createConstantDouble(double val);

  static FpNode* createConstantFloat(float val);

  FpNode() = delete;

  explicit FpNode(FpType&& fpty);

  // explicit FpNode(FpType fpty);

  FpNode(const FpNode& f);

  FpNode(FpNode&& f);

  ~FpNode() {
    mpfr_clear(shadow_value);
    fpTypeID = kUnknown;  // mark the memeory is invalid
  }
};

#endif  // _FP_NODE_H
