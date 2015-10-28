// The Monte Carlo Arihmetic Library - A tool for automated rounding error
// analysis of floating point software.
//
// Copyright (C) 2014 The Computer Engineering Laboratory, The
// University of Sydney. Maintained by Michael Frechtling:
// michael.frechtling@sydney.edu.au
//
// Copyright (C) 2015
//     Universite de Versailles St-Quentin-en-Yvelines                          *
//     CMLA, Ecole Normale Superieure de Cachan                                 *
//
// Changelog:
//
// 2015-05-20 replace random number generator with TinyMT64. This
// provides a reentrant, independent generator of better quality than
// the one provided in libc.
//
// This file is part of the Monte Carlo Arithmetic Library, (MCALIB). MCALIB is
// free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.

#include <math.h>
#include <mpfr.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "libmca-mpfr.h"
#include "../vfcwrapper/vfcwrapper.h"
#include "../common/tinymt64.h"


#define NEAREST_FLOAT(x)	((float) (x))
#define	NEAREST_DOUBLE(x)	((double) (x))

int 	MCALIB_OP_TYPE 		= MCAMODE_IEEE;
int 	MCALIB_T		    = 24;

#define MP_ADD &mpfr_add
#define MP_SUB &mpfr_sub
#define MP_MUL &mpfr_mul
#define MP_DIV &mpfr_div
#define MP_NEG &mpfr_neg

typedef int (*mpfr_bin)(mpfr_t, mpfr_t, mpfr_t, mpfr_rnd_t);
typedef int (*mpfr_unr)(mpfr_t, mpfr_t, mpfr_rnd_t);

static float _mca_sbin(float a, float b, mpfr_bin mpfr_op);
static float _mca_sunr(float a, mpfr_unr mpfr_op);
static int _mca_scmp(float a, float b);
static double _mca_dbin(double a, double b, mpfr_bin mpfr_op);
static double _mca_dunr(double a, mpfr_unr mpfr_op);
static int _mca_dcmp(double a, double b);

/******************** MCA CONTROL FUNCTIONS *******************
* The following functions are used to set virtual precision and
* MCA mode of operation.
***************************************************************/

static int _set_mca_mode(int mode){
	if (mode < 0 || mode > 3)
		return -1;

	MCALIB_OP_TYPE = mode;
	return 0;
}

static int _set_mca_precision(int precision){
	MCALIB_T = precision;
	return 0;
}


/******************** MCA RANDOM FUNCTIONS ********************
* The following functions are used to calculate the random
* perturbations used for MCA and apply these to MPFR format
* operands
***************************************************************/

/* random generator internal state */
tinymt64_t random_state;

static double _mca_rand(void) {
	/* Returns a random double in the (0,1) open interval */
	return tinymt64_generate_doubleOO(&random_state);
}

static int _mca_inexact(mpfr_ptr a, mpfr_rnd_t rnd_mode) {
	if (MCALIB_OP_TYPE == MCAMODE_IEEE) {
		return 0;
	}
	mpfr_exp_t e_a = mpfr_get_exp(a);
	mpfr_prec_t p_a = mpfr_get_prec(a);
	mpfr_t mpfr_rand, mpfr_offset, mpfr_zero;
	e_a = e_a - (MCALIB_T - 1);
	mpfr_inits2(p_a, mpfr_rand, mpfr_offset, mpfr_zero, (mpfr_ptr) 0);
	mpfr_set_d(mpfr_zero, 0., rnd_mode);
	int cmp = mpfr_cmp(a, mpfr_zero);
	if (cmp == 0) {
		mpfr_clear(mpfr_rand);
		mpfr_clear(mpfr_offset);
		mpfr_clear(mpfr_zero);
		return 0;
	}
	double d_rand = (_mca_rand() - 0.5);
	double d_offset = pow(2, e_a);
	mpfr_set_d(mpfr_rand, d_rand, rnd_mode);
	mpfr_set_d(mpfr_offset, d_offset, rnd_mode);
	mpfr_mul(mpfr_rand, mpfr_rand, mpfr_offset, rnd_mode);
	mpfr_add(a, a, mpfr_rand, rnd_mode);
	mpfr_clear(mpfr_rand);
	mpfr_clear(mpfr_offset);
	mpfr_clear(mpfr_zero);
}

static void _mca_seed(void) {
	const int key_length = 3;
	uint64_t init_key[key_length];
	struct timeval t1;
	gettimeofday(&t1, NULL);

	/* Hopefully the following seed is good enough for Montercarlo */
	init_key[0] = t1.tv_sec;
	init_key[1] = t1.tv_usec;
	init_key[2] = getpid();

	tinymt64_init_by_array(&random_state, init_key, key_length);
}

/******************** MCA ARITHMETIC FUNCTIONS ********************
* The following set of functions perform the MCA operation. Operands
* are first converted to MPFR format, inbound and outbound perturbations
* are applied using the _mca_inexact function, and the result converted
* to the original format for return
*******************************************************************/

static float _mca_sbin(float a, float b, mpfr_bin mpfr_op) {
	mpfr_t mpfr_a, mpfr_b, mpfr_r;
	mpfr_prec_t prec = 24 + MCALIB_T;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_b, mpfr_r, (mpfr_ptr) 0);
	mpfr_set_flt(mpfr_a, a, rnd);
	mpfr_set_flt(mpfr_b, b, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_RR) {
		_mca_inexact(mpfr_a, rnd);
		_mca_inexact(mpfr_b, rnd);
	}
	mpfr_op(mpfr_r, mpfr_a, mpfr_b, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_PB) {
		_mca_inexact(mpfr_r, rnd);
	}
	float ret = mpfr_get_flt(mpfr_r, rnd);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_b);
	mpfr_clear(mpfr_r);
	return NEAREST_FLOAT(ret);
}

static float _mca_sunr(float a, mpfr_unr mpfr_op) {
	mpfr_t mpfr_a, mpfr_r;
	mpfr_prec_t prec = 24 + MCALIB_T;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_r, (mpfr_ptr) 0);
	mpfr_set_flt(mpfr_a, a, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_RR) {
		_mca_inexact(mpfr_a, rnd);
	}
	mpfr_op(mpfr_r, mpfr_a, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_PB) {
		_mca_inexact(mpfr_r, rnd);
	}
	float ret = mpfr_get_flt(mpfr_r, rnd);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_r);
	return NEAREST_FLOAT(ret);
}

static double _mca_dbin(double a, double b, mpfr_bin mpfr_op) {
	mpfr_t mpfr_a, mpfr_b, mpfr_r;
	mpfr_prec_t prec = 53 + MCALIB_T;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_b, mpfr_r, (mpfr_ptr) 0);
	mpfr_set_d(mpfr_a, a, rnd);
	mpfr_set_d(mpfr_b, b, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_RR) {
		_mca_inexact(mpfr_a, rnd);
		_mca_inexact(mpfr_b, rnd);
	}
	mpfr_op(mpfr_r, mpfr_a, mpfr_b, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_PB) {
		_mca_inexact(mpfr_r, rnd);
	}
	double ret = mpfr_get_d(mpfr_r, rnd);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_b);
	mpfr_clear(mpfr_r);
	return NEAREST_DOUBLE(ret);
}

static double _mca_dunr(double a, mpfr_unr mpfr_op) {
	mpfr_t mpfr_a, mpfr_r;
	mpfr_prec_t prec = 53 + MCALIB_T;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_r, (mpfr_ptr) 0);
	mpfr_set_d(mpfr_a, a, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_RR) {
		_mca_inexact(mpfr_a, rnd);
	}
	mpfr_op(mpfr_r, mpfr_a, rnd);
	if (MCALIB_OP_TYPE != MCAMODE_PB) {
		_mca_inexact(mpfr_r, rnd);
	}
	double ret = mpfr_get_d(mpfr_r, rnd);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_r);
	return NEAREST_DOUBLE(ret);
}

/******************** MCA COMPARE FUNCTIONS ********************
* Compare operations do not require MCA and as such these functions
* only convert the operands to MPFR format and execute the mpfr_cmp
* function, the integer result is then passed back to the original
* fphook function.
****************************************************************/

static int _mca_scmp(float a, float b) {
	mpfr_t mpfr_a, mpfr_b;
	mpfr_prec_t prec = 24;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_b, (mpfr_ptr) 0);
	mpfr_set_flt(mpfr_a, a, rnd);
	mpfr_set_flt(mpfr_b, b, rnd);
	int cmp_r = mpfr_cmp(mpfr_a, mpfr_b);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_b);
	return cmp_r;
}

static int _mca_dcmp(double a, double b) {
	mpfr_t mpfr_a, mpfr_b;
	mpfr_prec_t prec = 53;
	mpfr_rnd_t rnd = MPFR_RNDN;
	mpfr_inits2(prec, mpfr_a, mpfr_b, (mpfr_ptr) 0);
	mpfr_set_d(mpfr_a, a, rnd);
	mpfr_set_d(mpfr_b, b, rnd);
	int cmp_r = mpfr_cmp(mpfr_a, mpfr_b);
	mpfr_clear(mpfr_a);
	mpfr_clear(mpfr_b);
	return cmp_r;
}

/************************* FPHOOKS FUNCTIONS *************************
* These functions correspond to those inserted into the source code
* during source to source compilation and are replacement to floating
* point operators
**********************************************************************/

static int _floateq(float a, float b) {
	//return a == b
	int res = _mca_scmp(a, b);
	return (res == 0 ? 1 : 0);
}

static int _floatne(float a, float b) {
	//return a != b
	int res = _mca_scmp(a, b);
	return (res == 0 ? 0 : 1);
}

static int _floatlt(float a, float b) {
	//return a < b
	int res = _mca_scmp(a, b);
	return (res < 0 ? 1 : 0);
}

static int _floatgt(float a, float b) {
	//return a > b
	int res = _mca_scmp(a, b);
	return (res > 0 ? 1 : 0);
}

static int _floatle(float a, float b) {
	//return a <= b
	int res = _mca_scmp(a, b);
	return (res <= 0 ? 1 : 0);
}

static int _floatge(float a, float b) {
	//return a >= b
	int res = _mca_scmp(a, b);
	return (res >= 0 ? 1 : 0);
}

static int _doubleeq(double a, double b) {
	//return a == b
	int res = _mca_dcmp(a, b);
	return (res == 0 ? 1 : 0);
}

static int _doublene(double a, double b) {
	//return a != b
	int res = _mca_dcmp(a, b);
	return (res == 0 ? 0 : 1);
}

static int _doublelt(double a, double b) {
	//return a < b
	int res = _mca_dcmp(a, b);
	return (res < 0 ? 1 : 0);
}

static int _doublegt(double a, double b) {
	//return a > b
	int res = _mca_dcmp(a, b);
	return (res > 0 ? 1 : 0);
}

static int _doublele(double a, double b) {
	//return a <= b
	int res = _mca_dcmp(a, b);
	return (res <= 0 ? 1 : 0);
}

static int _doublege(double a, double b) {
	//return a >= b
	int res = _mca_dcmp(a, b);
	return (res >= 0 ? 1 : 0);
}

static float _floatadd(float a, float b) {
	//return a + b
	return _mca_sbin(a, b,(mpfr_bin)MP_ADD);
}

static float _floatsub(float a, float b) {
	//return a - b
	return _mca_sbin(a, b, (mpfr_bin)MP_SUB);
}

static float _floatmul(float a, float b) {
	//return a * b
	return _mca_sbin(a, b, (mpfr_bin)MP_MUL);
}

static float _floatdiv(float a, float b) {
	//return a / b
	return _mca_sbin(a, b, (mpfr_bin)MP_DIV);
}

static float _floatneg(float a) {
	//return -a
	return _mca_sunr(a, (mpfr_unr)MP_NEG);
}

static double _doubleadd(double a, double b) {
	//return a + b
	return _mca_dbin(a, b, (mpfr_bin)MP_ADD);
}

static double _doublesub(double a, double b) {
	//return a - b
	return _mca_dbin(a, b, (mpfr_bin)MP_SUB);
}

static double _doublemul(double a, double b) {
	//return a * b
	return _mca_dbin(a, b, (mpfr_bin)MP_MUL);
}

static double _doublediv(double a, double b) {
	//return a / b
	return _mca_dbin(a, b, (mpfr_bin)MP_DIV);
}

static double _doubleneg(double a) {
	//return -a
	return _mca_dunr(a, (mpfr_unr)MP_NEG);
}

struct mca_interface_t mpfr_mca_interface = {
	_floateq,
	_floatne,
	_floatlt,
	_floatgt,
	_floatle,
	_floatge,
	_floatadd,
	_floatsub,
	_floatmul,
	_floatdiv,
	_doubleeq,
	_doublene,
	_doublelt,
	_doublegt,
	_doublele,
	_doublege,
	_doubleadd,
	_doublesub,
	_doublemul,
	_doublediv,
	_mca_seed,
	_set_mca_mode,
	_set_mca_precision
};