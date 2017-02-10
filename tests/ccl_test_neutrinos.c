#include "ccl.h"
#include "ccl_neutrinos.h"
#include <stdio.h>
#include <math.h>

int main(int argc, char * argv[]){

	// Set up parameters for the case of three massive neutrinos, example below.
	ccl_parameters params = ccl_parameters_create_lcdm_nu(0.3, 0.05, 0.0,0.7, 2.215e-9, 0.9619, 0.00641, 3., 0.12);
	//ccl_cosmology * cosmo = ccl_cosmology_create(params, default_config);	
	int ai;
	double a, omnuh2_3massless, omnuh2_3massive; //, OmegaMcheck;
	FILE * output;
	gsl_spline *temp_spline;

 	// Get the spline of the phase-space integral
	temp_spline = ccl_calculate_nu_phasespace_spline();

	/// Print parameters to check against CLASS
	printf("OmegaL=%1.12f\n",params.Omega_l);
	printf("OmegaNuRel=%1.12f\n", params.Omega_n_rel);
	printf("OmegaNuMass=%1.12f\n", params.Omega_n_mass);
	printf("OmegaM=%1.12f\n", params.Omega_m);
	printf("Omegag=%1.12f\n", params.Omega_g);
	printf("Omegak=%1.12f\n", params.Omega_k);
	printf("Omegac=%1.12f\n", params.Omega_c);
	printf("Omegab=%1.12f\n", params.Omega_b);
	
	// To check Omega_m(z) against CLASS.
	//OmegaMcheck = ccl_omega_m_z(cosmo, (1./(1.+0.99925023933)));
	//printf("OmM= %1.12f\n", OmegaMcheck); 

	// Get Omeganuh^2 at several values of a
	output = fopen("./neutrinos_example.out", "w"); 
	for(ai=1; ai<=50; ai++){
		a= ai*0.02;
   		// Examples of calling Omeganuh2 for different neutrino configurations:
		// All neutrinos massless:	 
		omnuh2_3massless = Omeganuh2(a, 3.046, 0., params.T_CMB, temp_spline);
		// Three massive neutrinos of 0.04 eV each. Adding a small contribution from massless neutrinos as described in CLASS explanatory.ini to ensure N = 3.046 at early times.
		omnuh2_3massive = Omeganuh2(a, params.N_nu_mass, params.mnu, params.T_CMB, temp_spline)+ Omeganuh2(a, params.N_nu_rel, 0., params.T_CMB, temp_spline);
		fprintf(output, "%.16f %.16f %.16f \n",a, omnuh2_3massless, omnuh2_3massive); 
		
	}

	fclose(output);

	return 0;
}