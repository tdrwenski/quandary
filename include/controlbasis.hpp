#include "defs.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include "util.hpp"
#pragma once


/* Abstract base class */
class ControlBasis {
    protected:
        int nbasis;            // number of basis functions
        double tstart;         // Interval [tstart,tstop] where this control basis is applied in
        double tstop;           
        int skip;              // Constant to skip to the starting location for this basis inside the (global) control vector. 

    public: 
        ControlBasis();
        ControlBasis(int nbasis_, double tstart, double tstop);
        virtual ~ControlBasis();

        int getNBasis() {return nbasis; };
        double getTstart() {return tstart; };
        double getTstop() {return tstop; };
        void setSkip(int skip_) {skip = skip_;};

        /* Evaluate the Basis(alpha, t) at time t using the coefficients coeff. */
        virtual void evaluate(const double t, const std::vector<double>& coeff, int carrier_freq_size, int carrier_freq_id, double* Blt1, double*Blt2) = 0;

        /* Evaluates the derivative at time t, multiplied with fbar. */
        virtual void derivative(const double t, double* coeff_diff, const double valbar1, const double valbar2, int carrier_freq_size, int carrier_freq_id)= 0;
};

/* 
 * Discretization of the Controls using quadratic Bsplines ala Anders Petersson
 * Bspline basis functions have local support with width = 3*dtknot, 
 * where dtknot = T/(nsplines -2) is the time knot vector spacing.
 */
class BSpline2nd : public ControlBasis {
    protected:
        double dtknot;                    // spacing of time knot vector    
        double *tcenter;                  // vector of basis function center positions
        double width;                     // support of each basis function (m*dtknot)

        /* Evaluate the bspline basis functions B_l(tau_l(t)) */
        double basisfunction(int id, double t);

    public:
        BSpline2nd(int NBasis, double tstart, double tstop);
        ~BSpline2nd();


        /* Evaluate the spline at time t using the coefficients coeff. */
        void evaluate(const double t, const std::vector<double>& coeff, int carrier_freq_size, int carrier_freq_id, double* Blt1_ptr, double* Blt2_ptr);

        /* Evaluates the derivative at time t, multiplied with fbar. */
        void derivative(const double t, double* coeff_diff, const double valbar1, const double valbar2, int carrier_freq_size, int carrier_freq_id);
};

/* 
 * Parameterization of the controls using step function with constant amplitude and variable width 
 */
class Step : public ControlBasis {
    protected:
        double step_amp_p;
        double step_amp_q;
        double tramp;

    public: 
        Step(double step_amp_p_, double step_amp_q_, double t0, double t1, double tramp);
        ~Step();

       /* Evaluate the spline at time t using the coefficients coeff. */
        void evaluate(const double t, const std::vector<double>& coeff, int carrier_freq_size, int carrier_freq_id, double* Blt1, double*Blt2);

        /* Evaluates the derivative at time t, multiplied with fbar. */
        void derivative(const double t, double* coeff_diff, const double valbar1, const double valbar2, int carrier_freq_size, int carrier_freq_id);
};
