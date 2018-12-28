#include "FpNode.h"

#include <assert.h>
#include <math.h>

mpfr_prec_t PREC = 120;

unordered_map<double, shared_ptr<FpNode> > FpNode::const_double_node_map;
unordered_map<float, shared_ptr<FpNode> > FpNode::const_float_node_map;

FpNode* FpNode::createConstantDouble(double val) {
  //tring value_str(to_string(val));
  FpNode* node;
  auto it = const_double_node_map.find(val);
  if (it == const_double_node_map.end()) {
    pair<unordered_map<double, shared_ptr<FpNode> >::iterator, bool> ret;
    ret = const_double_node_map.emplace(
        make_pair(val, make_shared<FpNode>(kDouble)));
    node = ret.first->second.get();
    node->d_value = val;
    mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    node->depth = 1;
    node->valid_bits = 52;  // suppose constant always accurate
    assert(node->isDoubleTy());
    return node;
  }
  node = it->second.get();
  assert(node->isDoubleTy());
  return node;
}

FpNode* FpNode::createConstantFloat(float val) {
  //string value_str(to_string(val));
  FpNode* node;
  auto it = const_float_node_map.find(val);
  if (it == const_float_node_map.end()) {
    pair<unordered_map<float, shared_ptr<FpNode> >::iterator, bool> ret;
    ret = const_float_node_map.emplace(
        make_pair(val, make_shared<FpNode>(kFloat)));
    node = ret.first->second.get();
    node->f_value = val;
    mpfr_set_d(node->shadow_value, val, MPFR_RNDN);
    node->depth = 1;
    node->valid_bits = 23;  // suppose constant always accurate
    assert(node->isFloatTy());
    return node;
  }
  node = it->second.get();
  assert(node->isFloatTy());
  return node;
}

FpNode::FpNode(FpType&& fpty) {
  fpTypeID = fpty;
  line = 0;
  
  if (fpTypeID == kDouble) {
    valid_bits = 52;
  } else if (fpTypeID == kFloat) {
    valid_bits = 23;
  } else {
    assert(0 && "FpNode should be a valid type");
  }
  d_value = 0.0;
  f_value = 0.0f;

  mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
  depth = 1;
  bits_canceled_by_this_instruction = 0;
  cancelled_badness_bits = 0;
  max_cancelled_badness_bits = 0;
  
  sum_relative_error = 0.0;
  real_error_to_shadow =0.0;
  relative_error_to_shadow = 0.0;

  max_cancelled_badness_node = this;
  first_arg = NULL;
  second_arg = NULL;
  relative_bigger_arg = NULL;
  fp_info = NULL;
}

FpNode::FpNode(const FpNode& f) {
  fpTypeID = f.fpTypeID;
  line = f.line;
  valid_bits = f.valid_bits;
  d_value = f.d_value;
  f_value = f.f_value;
  mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
  mpfr_copysign(shadow_value, f.shadow_value, f.shadow_value, MPFR_RNDN);

  assert(f.isValid());

  depth = f.depth;

  bits_canceled_by_this_instruction = f.bits_canceled_by_this_instruction;
  cancelled_badness_bits = f.cancelled_badness_bits;;
  max_cancelled_badness_bits = f.max_cancelled_badness_bits;
  
  sum_relative_error = f.sum_relative_error;
  real_error_to_shadow = f.real_error_to_shadow;
  relative_error_to_shadow = f.relative_error_to_shadow;

  max_cancelled_badness_node = this;
  first_arg = f.first_arg;
  second_arg = f.second_arg;
  relative_bigger_arg = f.relative_bigger_arg;
  fp_info = f.fp_info;
  //if (f.d_value == -163.5) {
  //  assert(0);
  //}
  //mpfr_get_d(f.shadow_value, MPFR_RNDN);
  
  
  //double first = mpfr_get_d(shadow_value, MPFR_RNDN);
  //double second = mpfr_get_d(f.shadow_value, MPFR_RNDN);
  
  
  
  
  
  
  //if (isnan(first) && isnan(second)) return;
  //if (first != second) {
    //cout << "first=" << first << " second=" << second << endl;
  //  assert(first == second && "mpfr_copy should work well");
  //}
}

FpNode::FpNode(FpNode&& f) {
  fpTypeID = f.fpTypeID;
  line = f.line;
  valid_bits = f.valid_bits;
  d_value = f.d_value;
  f_value = f.f_value;
  mpfr_inits2(PREC, shadow_value, (mpfr_ptr)0);
  mpfr_copysign(shadow_value, f.shadow_value, f.shadow_value, MPFR_RNDN);

  assert(f.isValid());

  depth = f.depth;

  bits_canceled_by_this_instruction = f.bits_canceled_by_this_instruction;
  cancelled_badness_bits = f.cancelled_badness_bits;;
  max_cancelled_badness_bits = f.max_cancelled_badness_bits;
  
  sum_relative_error = f.sum_relative_error;
  real_error_to_shadow = f.real_error_to_shadow;
  relative_error_to_shadow = f.relative_error_to_shadow;

  max_cancelled_badness_node = this;
  first_arg = f.first_arg;
  second_arg = f.second_arg;
  relative_bigger_arg = f.relative_bigger_arg;
  fp_info = f.fp_info;
}
