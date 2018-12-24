#include "FpNode.h"

#include <assert.h>
#include <math.h>

mpfr_prec_t PREC = 120;

FpNode* FpNode::createConstantDouble(double val) {
  string value_str(ConvertToString(val));
  FpNode* node;
  unordered_map<string, FpNode>::iterator it = const_node_map.find(value_str);
  if (it == FpNode::const_node_map.end()) {
    pair<unordered_map<string, FpNode>::iterator, bool> ret;
    ret = FpNode::const_node_map.insert(
        pair<string, FpNode>(value_str, FpNode(kDouble)));
    node = &(ret.first->second);
    node->d_value = val;
    mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    node->depth = 1;
    node->valid_bits = 52;  // suppose constant always accurate
    return node;
  }
  node = &(it->second);
  assert(node->isDoubleTy());
  return node;
}

FpNode* FpNode::createConstantFloat(float val) {
  string value_str(ConvertToString(val));
  FpNode* node;
  unordered_map<string, FpNode>::iterator it = const_node_map.find(value_str);
  if (it == const_node_map.end()) {
    pair<unordered_map<string, FpNode>::iterator, bool> ret;
    ret = FpNode::const_node_map.insert(
        pair<string, FpNode>(value_str, FpNode(kFloat)));
    node = &(ret.first->second);
    node->f_value = val;
    mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    node->depth = 1;
    node->valid_bits = 23;  // suppose constant always accurate
    return node;
  }
  node = &(it->second);
  assert(node->isFloatTy());
  return node;
}

FpNode::FpNode(FpType fpty) {
  mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
  fpTypeID = fpty;
  d_value = 0.0;
  f_value = 0.0f;
  sum_relative_error = real_error_to_shadow = relative_error_to_shadow = 0.0;
  first_arg = second_arg = NULL;
  max_cancelled_badness_node = this;
  relative_bigger_arg = NULL;
  fp_info = NULL;
  depth = 1;
  if (fpTypeID == kDouble) {
    valid_bits = 52;
  } else if (fpTypeID == kFloat) {
    valid_bits = 23;
  } else {
    assert(0 && "FpNode should be a valid type");
  }

  bits_canceled_by_this_instruction = 0;
  cancelled_badness_bits = 0;
  max_cancelled_badness_bits = 0;
  line = 0;
}

FpNode::FpNode(const FpNode& f) {
  d_value = f.d_value;
  f_value = f.f_value;
  fpTypeID = f.fpTypeID;
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
    //cout << "first=" << first << " second=" << second << endl;
    assert(first == second && "mpfr_copy should work well");
  }
}
