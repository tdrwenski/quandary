#include <string>
#include <sstream>
#include <cstring>
#include <iostream>
#include <fstream>
#include "util.hpp"
#include "defs.hpp"
#include <assert.h>
#include <petscts.h>
#include <vector>
#include<random>
#pragma once

/* Generalized Gellmann matrices, diagonally shifted such tha G_00 = 0, optionally only upper part */
class GellmannBasis {
  protected:
    int dim_rho;   /* Dimension of the Hilbertspace (N)*/
    int dim;       /* N (if Schroedinger solver) or N^2 (if Lindblad) */
    int nbasis;    /* Total number of basis matrices */
    LindbladType lindbladtype;  // decides whether or not to vectorize the system matrices
    bool upper_only; // Optional: only get the upper diagonal part (including the diagonal itself)
    bool shifted_diag; // Optional: Turn of shifting of the diagonal elements

    std::vector<Mat> BasisMat_Re; /* All (purely) real basis matrices. Size = dim_rho = N */ 
    std::vector<Mat> BasisMat_Im; /* All (purely) imaginary basis matrices. Size = dim_rho = N */ 
    Mat Id;			  /* Identity matrix */

    std::vector<Mat> SystemMats_A;  // System matrix when applying the operator in Schroedinger's equation
    std::vector<Mat> SystemMats_B;  // System matrix when applying the operator in Schroedinger's equation

    int nparams;

    Vec aux;     // Auxiliary vector to perform matvecs on Re(x) or Im(x)

  public:
     GellmannBasis(int dim_rho_, bool upper_only_, bool shifted_diag_, LindbladType lindbladtype_);
     virtual ~GellmannBasis();

    int getNBasis(){return nbasis;};
    int getNBasis_Re(){return BasisMat_Re.size();};
    int getNBasis_Im(){return BasisMat_Im.size();};
    Mat getBasisMat_Re(int id) {return BasisMat_Re[id];};
    Mat getBasisMat_Im(int id) {return BasisMat_Im[id];};
    Mat getIdentity(){return Id;};
    int getNParams(){return nparams;};

    void showBasisMats();

    virtual void assembleSystemMats()=0;

    virtual void applySystem(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im) = 0;
    virtual void applySystem_diff(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im) = 0;
    virtual void dRHSdp(Vec grad, Vec u, Vec v, double alpha, Vec ubar, Vec vbar, std::vector<double>& learnparamsL_Re, int skipID=0) = 0;

    virtual void printOperator(std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im, std::string datadir) = 0;
};

/* The standard generalized Gellmann matrices */
class StdGellmannBasis : public GellmannBasis {
  public: 
    StdGellmannBasis(int dim_rho_);
    ~StdGellmannBasis();
    // Leave the below functions empty
    void assembleSystemMats(){};
    void applySystem(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im){};
    void applySystem_diff(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im){};
    void dRHSdp(Vec grad, Vec u, Vec v, double alpha, Vec ubar, Vec vbar, std::vector<double>& learnparamsL_Re, int skipID=0){};

    void printOperator(std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im, std::string datadir){};
};

/* Hamiltonian paramterization via generalized Gellman matrices, multiplied by (-i) and shifted s.t. G_00=0 */
class HamiltonianBasis : public GellmannBasis {
  Mat Operator_Re;  
  Mat Operator_Im;  

  public:
    HamiltonianBasis(int dim_rho_, bool shifted_diag_, LindbladType lindbladtype);
    ~HamiltonianBasis();
    
    void assembleSystemMats();

    void applySystem(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im);
    void applySystem_diff(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im);
    void dRHSdp(Vec grad, Vec u, Vec v, double alpha, Vec ubar, Vec vbar, std::vector<double>& learnparamsL_Re, int skipID=0);

    void printOperator(std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im, std::string datadir);
};

class LindbladBasis: public GellmannBasis {
  Mat Operator;  

  public:
    LindbladBasis(int dim_rho_, bool shifted_diag_);
    ~LindbladBasis();

    void assembleSystemMats();

    void applySystem(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im);
    void applySystem_diff(Vec u, Vec v, Vec uout, Vec vout, std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im);
    void dRHSdp(Vec grad, Vec u, Vec v, double alpha, Vec ubar, Vec vbar, std::vector<double>& learnparamsL_Re, int skipID=0);

    std::vector<Mat> getSystemMats_A() {return SystemMats_A;};
    std::vector<Mat> getSystemMats_B() {return SystemMats_B;};

    void printOperator(std::vector<double>& learnparams_Re, std::vector<double>& learnparams_Im, std::string datadir);

    void evalOperator(std::vector<double>& learnparams_Re);
    int mapID(int i, int j){return i*BasisMat_Re.size() - i*(i+1)/2 + j;}
};