#include "ccl_core.h"
#include "ccl_neutrinos.h"
#include "ccl_utils.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gsl/gsl_errno.h"
#include "gsl/gsl_odeiv.h"
#include "gsl/gsl_spline.h"
#include "gsl/gsl_integration.h"

const ccl_configuration default_config = {ccl_boltzmann_class, ccl_halofit, ccl_tinker};

/* ------- ROUTINE: ccl_cosmology_create ------
INPUTS: ccl_parameters params
        ccl_configuration config
TASK: creates the ccl_cosmology struct and passes some values to it
DEFINITIONS:
chi: comoving distance [Mpc]
growth: growth function (density)
fgrowth: logarithmic derivative of the growth (density) (dlnD/da?)
E: E(a)=H(a)/H0 
accelerator: interpolation accelerator for functions of a
accelerator_achi: interpolation accelerator for functions of chi
growth0: growth at z=0, defined to be 1
sigma: ?
p_lin: linear matter power spectrum at z=0?
p_lnl: nonlinear matter power spectrum at z=0?
computed_distances, computed_growth, 
computed_power, computed_sigma: store status of the computations
*/


ccl_cosmology * ccl_cosmology_create(ccl_parameters params, ccl_configuration config)
{
  ccl_cosmology * cosmo = malloc(sizeof(ccl_cosmology));
  cosmo->params = params;
  cosmo->config = config;

  cosmo->data.chi = NULL;
  cosmo->data.growth = NULL;
  cosmo->data.fgrowth = NULL;
  cosmo->data.E = NULL;
  cosmo->data.accelerator=NULL;
  cosmo->data.accelerator_achi=NULL;
  cosmo->data.accelerator_m=NULL;
  cosmo->data.growth0 = 1.;
  cosmo->data.achi=NULL;

  cosmo->data.logsigma = NULL;
  cosmo->data.p_lin = NULL;
  cosmo->data.p_nl = NULL;
  cosmo->data.nu_pspace_int = NULL;
  cosmo->computed_distances = false;
  cosmo->computed_growth = false;
  cosmo->computed_power = false;
  cosmo->computed_sigma = false;
  cosmo->status = 0;
  
  return cosmo;
}

/* ------ ROUTINE: ccl_parameters_fill_initial -------
INPUT: ccl_parameters: params
TASK: fill parameters not set by ccl_parameters_create with some initial values
DEFINITIONS:
Omega_g = (Omega_g*h^2)/h^2 is the radiation parameter; "g" is for photons, as in CLASS
T_CMB: CMB temperature in Kelvin
Omega_l: Lambda 
A_s: amplitude of the primordial PS, enforced here to initially set to NaN
sigma_8: variance in 8 Mpc/h spheres for normalization of matter PS, enforced here to initially set to NaN
z_star: recombination redshift
 */

void ccl_parameters_fill_initial(ccl_parameters *params)
{
  // Fixed radiation parameters
  // Omega_g * h**2 is known from T_CMB
  params->T_CMB =  2.7255;
  params->Omega_g = M_PI*M_PI*pow((params->T_CMB/11604.5),4.)/(15*8.098E-11*params->h*params->h);

  // Neutrino parameters
  gsl_spline *temp; // Define a temp parameter to hold the phase space integral to get massive neutrinos
  temp = ccl_calculate_nu_phasespace_spline(); // Calculate the phase space integral
  
  params->Omega_n_rel = Omeganuh2(1.0, params->N_nu_rel, 0., params->T_CMB, temp) / ((params->h)*(params->h));
  params->Omega_n_mass = Omeganuh2(1.0, params->N_nu_mass, params->mnu, params->T_CMB, temp) / ((params->h)*(params->h));
  params->Omega_n = params->Omega_n_rel + params->Omega_n_mass;

  // Now get the total Omega matter, after computing massive neutrinos
  params->Omega_m = params->Omega_b + params-> Omega_c + params-> Omega_n_mass; 

  // Derived parameters
  params->Omega_l = 1.0 - params->Omega_m - params->Omega_g - params->Omega_n_rel - params->Omega_k;
  // Initially undetermined parameters - set to nan to trigger
  // problems if they are mistakenly used.
  if (isfinite(params->A_s)){params->sigma_8 = NAN;}
  if (isfinite(params->sigma_8)){params->A_s = NAN;}
  params->z_star = NAN;
}

/* ------ ROUTINE: ccl_parameters_create -------
INPUT: numbers for the basic cosmological parameters needed by CCL
TASK: fill params with some initial values provided by the user
DEFINITIONS:
Omega_c: cold dark matter
Omega_b: baryons
Omega_m: matter
Omega_n: neutrinos
Omega_k: curvature
little omega_x means Omega_x*h^2
w0: Dark energy eq of state parameter
wa: Dark energy eq of state parameter, time variation
H0: Hubble's constant in km/s/Mpc.
h: Hubble's constant divided by (100 km/s/Mpc).
A_s: amplitude of the primordial PS
n_s: index of the primordial PS
 */

ccl_parameters ccl_parameters_create(double Omega_c, double Omega_b, double Omega_k, double N_nu_rel, double N_nu_mass, double mnu, double w0, double wa, double h, double A_s, double n_s,int nz_mgrowth,double *zarr_mgrowth,double *dfarr_mgrowth){
  ccl_parameters params;
  params.sigma_8 = NAN;
  params.A_s = NAN;
  params.Omega_c = Omega_c;
  params.Omega_b = Omega_b;
  //params.Omega_m = Omega_b + Omega_c;  // Set this in fill_initial after computing massive neutrinos.
  //params.Omega_n = Omega_n;  // Set this in fill_initial.
  params.Omega_k = Omega_k;

  // Neutrinos
  params.N_nu_mass = N_nu_mass;
  params.N_nu_rel = N_nu_rel;
  params.mnu = mnu;

  // Dark Energy
  params.w0 = w0;
  params.wa = wa;

  // Hubble parameters
  params.h = h;
  params.H0 = h*100;

  // Primordial power spectra
  params.A_s = A_s;
  params.n_s = n_s;

  // Set remaining standard and easily derived parameters
  ccl_parameters_fill_initial(&params);

  //Trigger modified growth function if nz>0
  if(nz_mgrowth>0) {
    params.has_mgrowth=true;
    params.nz_mgrowth=nz_mgrowth;
    params.z_mgrowth=malloc(params.nz_mgrowth*sizeof(double));
    params.df_mgrowth=malloc(params.nz_mgrowth*sizeof(double));
    memcpy(params.z_mgrowth,zarr_mgrowth,params.nz_mgrowth*sizeof(double));
    memcpy(params.df_mgrowth,dfarr_mgrowth,params.nz_mgrowth*sizeof(double));
  }
  else {
    params.has_mgrowth=false;
    params.nz_mgrowth=0;
    params.z_mgrowth=NULL;
    params.df_mgrowth=NULL;
  }
  
  return params;  
}

/* ------- ROUTINE: ccl_parameters_create_flat_lcdm -------- 
INPUT: some cosmological parameters needed to create a flat LCDM model 
TASK: call ccl_parameters_create to produce an LCDM model
*/

ccl_parameters ccl_parameters_create_flat_lcdm(double Omega_c, double Omega_b, double h, double A_s, double n_s)
{
  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  double N_nu_mass = 0.0;
  double N_nu_rel = 0.0;
  double mnu = 0.0;
  double w0 = -1.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s, -1, NULL, NULL);
  return params;

}

/* ------- ROUTINE: ccl_parameters_create_flat_lcdm_nu -------- 
INPUT: some cosmological parameters needed to create a flat LCDM model with neutrinos 
TASK: call ccl_parameters_create to produce an LCDM model
*/

ccl_parameters ccl_parameters_create_flat_lcdm_nu(double Omega_c, double Omega_b, double h, double A_s, double n_s, double N_nu_rel, double N_nu_mass, double mnu)
{
  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  double w0 = -1.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s, -1, NULL, NULL);
  return params;

}



/* ------- ROUTINE: ccl_parameters_create_lcdm -------- 
INPUT: some cosmological parameters needed to create an LCDM model with curvature 
TASK: call ccl_parameters_create for this specific model
*/

ccl_parameters ccl_parameters_create_lcdm(double Omega_c, double Omega_b, double Omega_k, double h, double A_s, double n_s)
{
  //double Omega_n = 0.0;
  double N_nu_mass = 0.0;
  double N_nu_rel = 0.0;
  double mnu = 0.0;
  double w0 = -1.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;

}

/* ------- ROUTINE: ccl_parameters_create_lcdm_nu -------- 
INPUT: some cosmological parameters needed to create an LCDM model with curvature and neutrinos
TASK: call ccl_parameters_create for this specific model
*/

ccl_parameters ccl_parameters_create_lcdm_nu(double Omega_c, double Omega_b, double Omega_k, double h, double A_s, double n_s, double N_nu_rel, double N_nu_mass, double mnu)
{
  //double Omega_n = 0.0;
  double w0 = -1.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;

}


/* ------- ROUTINE: ccl_parameters_create_flat_wcdm -------- 
INPUT: some cosmological parameters needed to create an LCDM model with wa=0 but w0!=-1
TASK: call ccl_parameters_create for this specific model
*/


ccl_parameters ccl_parameters_create_flat_wcdm(double Omega_c, double Omega_b, double w0, double h, double A_s, double n_s)
{

  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  double N_nu_mass = 0.0;
  double N_nu_rel = 0.0;
  double mnu = 0.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;
}

/* ------- ROUTINE: ccl_parameters_create_wcdm_nu -------- 
INPUT: some cosmological parameters needed to create an LCDM model with neutrinos, and wa=0 but w0!=-1
TASK: call ccl_parameters_create for this specific model
*/


ccl_parameters ccl_parameters_create_flat_wcdm_nu(double Omega_c, double Omega_b, double w0, double h, double A_s, double n_s, double N_nu_rel, double N_nu_mass, double mnu)
{

  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  double wa = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k, N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;
}


/* ------- ROUTINE: ccl_parameters_create_wacdm -------- 
INPUT: some cosmological parameters needed to create an LCDM model with curvature wa!=0 and and w0!=-1
TASK: call ccl_parameters_create for this specific model
*/

ccl_parameters ccl_parameters_create_flat_wacdm(double Omega_c, double Omega_b, double w0, double wa, double h, double A_s, double n_s)
{

  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  double N_nu_mass = 0.0;
  double N_nu_rel = 0.0;
  double mnu = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k,N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;
}

/* ------- ROUTINE: ccl_parameters_create_wacdm_nu -------- 
INPUT: some cosmological parameters needed to create an LCDM model with neutrinoswith curvature wa!=0 and and w0!=-1
TASK: call ccl_parameters_create for this specific model
*/

ccl_parameters ccl_parameters_create_flat_wacdm_nu(double Omega_c, double Omega_b, double w0, double wa, double h, double A_s, double n_s, double N_nu_rel, double N_nu_mass, double mnu)
{

  double Omega_k = 0.0;
  //double Omega_n = 0.0;
  ccl_parameters params = ccl_parameters_create(Omega_c, Omega_b, Omega_k,N_nu_rel, N_nu_mass, mnu, w0, wa, h, A_s, n_s,-1,NULL,NULL);
  return params;
}



/* ------- ROUTINE: ccl_data_free -------- 
INPUT: ccl_data
TASK: free the input data
*/

void ccl_data_free(ccl_data * data)
{
  //We cannot assume that all of these have been allocated
  //TODO: it would actually make more sense to do this within ccl_cosmology_free,
  //where we could make use of the flags "computed_distances" etc. to figure out
  //what to free up
  if(data->chi!=NULL)
    gsl_spline_free(data->chi);
  if(data->growth!=NULL)
    gsl_spline_free(data->growth);
  if(data->fgrowth!=NULL)
    gsl_spline_free(data->fgrowth);
  if(data->accelerator!=NULL)
    gsl_interp_accel_free(data->accelerator);
  if(data->accelerator_achi!=NULL)
    gsl_interp_accel_free(data->accelerator_achi);
  if(data->E!=NULL)
    gsl_spline_free(data->E);
  if(data->achi!=NULL)
    gsl_spline_free(data->achi);
  if(data->logsigma!=NULL)
    gsl_spline_free(data->logsigma);
  if(data->p_lin!=NULL)
    gsl_spline_free(data->p_lin);
  if(data->p_nl!=NULL)
    gsl_spline2d_free(data->p_nl);
}


/* ------- ROUTINE: ccl_cosmology_free -------- 
INPUT: ccl_cosmology struct
TASK: free the input data and the cosmology struct
*/

void ccl_cosmology_free(ccl_cosmology * cosmo)
{
  ccl_data_free(&cosmo->data);
  free(cosmo);
}
