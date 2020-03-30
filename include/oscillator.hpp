#include <stdio.h>
#include "bspline.hpp"
#include <fstream>
#include <iomanip>
#include <petscmat.h>

#pragma once

using namespace std;

/*
 * Abstract base class for oscillators
 */
class Oscillator {
  protected:
    int nlevels;  // Number of levels for this the oscillator 
    int nparam;   // Number of control parameters for each real and imaginary part
    double* param_Re; // parameters of real part of the control
    double* param_Im; // parameters of imaginary part of the control

  public:
    Oscillator();
    Oscillator(int nlevels_, int nparam_);
    virtual ~Oscillator();

    /* Evaluates real and imaginary control function at time t */
    virtual int evalControl(double t, double* Re_ptr, double* Im_ptr) = 0;

    /* Return the constants */
    int getNParam() { return nparam; };
    int getNLevels() { return nlevels; };

    /* Return pointers to the control parameters */
    // virtual int getParams(double* paramsRe, double* paramsIm) = 0;

    /* Get real and imaginary part of parameters.  */
    double* getParamsRe();
    double* getParamsIm();


    /* Update control parameters x <- x + stepsize*direction */
    virtual int updateParams(double stepsize, double* directionRe, double* directionIm) = 0;

    /* Compute derivatives of the Re and Im control function wrt the parameters */
    virtual int evalDerivative(double t, double* dRedp, double* dImdp) = 0;

    /* Print the control functions for each t \in [0,ntime*dt] */
    virtual void flushControl(int ntime, double dt, const char* filename);

    /* Compute lowering operator a_k = I_n1 \kron ... \kron a^(nk) \kron ... \kron I_nQ */
    int createLoweringOP(int dim_prekron, int dim_postkron, Mat* loweringOP);

    /* Compute number operator N_k = a_k^T a_k */
    int createNumberOP(int dim_prekron, int dim_postcron, Mat* numberOP);

};


/* 
 * Implements oscillators that are discretized by spline basis functions
 */
class SplineOscillator : public Oscillator {
    double Tfinal;               // final time
    Bspline *basisfunctions;  // Bspline basis function for control discretization 

  public:
    SplineOscillator();
    SplineOscillator(int nlevels_, int nbasis_, double Tfinal_);
    ~SplineOscillator();

    /* Evaluates the real and imaginare spline functions at time t, using current spline parameters */
    virtual int evalControl(double t, double* Re_ptr, double* Im_ptr);

    /* Returns pointers to the real and imaginary control parameters */
    // virtual int getParams(double* paramsRe, double* paramsIm);

    /* Compute derivatives of the Re and Im control function wrt param_Re, param_Im */
    virtual int evalDerivative(double t, double* dRedp, double* dImdp);

    /* Update control parameters x <- x + stepsize*direction */
    virtual int updateParams(double stepsize, double* directionRe, double* directionIm);

    /* Print the control functions for each t \in [0,tfinal] */
    // virtual void flushControl(int ntime, double dt, const char* filename);

};

