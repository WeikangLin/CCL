#pragma once

#include "ccl_core.h"

double ccl_linear_matter_power(ccl_cosmology * cosmo, double k, double a,int * status);
double ccl_nonlin_matter_power(ccl_cosmology * cosmo, double k, double a,int * status);
//void ccl_linear_matter_powers(ccl_cosmology * cosmo, int n, double a[n], double k[n], double output[n]);         
// FIXME: Not implemented?                                                                                         

double ccl_sigmaR(ccl_cosmology *cosmo, double R, int * status);
double ccl_sigma8(ccl_cosmology *cosmo, int * status);
