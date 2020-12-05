#include <stdio.h>
#include <iostream> 
#include <math.h>
#include <assert.h>
#include <petscmat.h>
#include <vector>
#include "util.hpp"
#pragma once


class Gate {
  protected:
    Mat Va, Vb;        /* Input: Real and imaginary part of V_target, non-vectorized */
    // Mat PxP;           /* Vectorized projection matrix P\kron P to map between essential levels and full system */

    std::vector<int> nessential;
    std::vector<int> nlevels;
    int mpirank_petsc;

    int dim_ess;   /* Dimension of target Gate matrix (non-vectorized), essential levels only */
    int dim_rho;   /* Dimension of system matrix rho (non-vectorized), all levels */

  private:
    Mat ReG, ImG;           /* Real and imaginary part of \bar V \kron V */
    // Vec ufinal_e, vfinal_e; /* auxiliary, holding final state projected onto essential levels */
    // Vec u0_e, v0_e;         /* auxiliary, holding final state projected onto essential levels */
    Vec x_full;          /* auxiliary vecs */
    // Vec x_e;        /* auxiliary vecs */

  public:
    Gate();
    Gate(std::vector<int> nlevels_, std::vector<int> nessential_);
    virtual ~Gate();

    /* Assemble ReG = Re(\bar V \kron V) and ImG = Im(\bar V \kron V) */
    void assembleGate();
    
    /* compare the final state to gate-transformed initialcondition in Frobenius norm 1/2 * || q(T) - V\kronV q(0)||^2 */
    void compare_frobenius(const Vec finalstate, const Vec rho0, double& obj);
    void compare_frobenius_diff(const Vec finalstate, const Vec rho0, Vec rho0bar, const double delta_bar);

    /* compare the final state to gate-transformed initialcondition using trace distance overlap 1 - Tr(V\rho(0)V^d \rho(T)) */
    void compare_trace(const Vec finalstate, const Vec rho0, double& obj);
    void compare_trace_diff(const Vec finalstate, const Vec rho0, Vec rho0bar, const double delta_bar);
};

/* X Gate, spanning one qubit. 
 * V = 0 1
 *     1 0
 */
class XGate : public Gate {
  public:
    XGate(std::vector<int> nlevels_, std::vector<int> nessential_);
    ~XGate();
};

/* Y Gate, spanning one qubit. 
 * V = 0 -i
 *     i  0
 */
class YGate : public Gate {
  public:
    YGate(std::vector<int> nlevels_, std::vector<int> nessential_);
    ~YGate();
};

/* Z Gate, spanning one qubit. 
 * V = 1   0
 *     0  -1
 */
class ZGate : public Gate {
  public:
    ZGate(std::vector<int> nlevels_, std::vector<int> nessential_);
    ~ZGate();
};

/* Hadamard Gate 
 * V = 1/sqrt(2) * | 1   1 |
 *                 | 1  -1 |
 */
class HadamardGate : public Gate {
  public:
    HadamardGate(std::vector<int> nlevels_, std::vector<int> nessential_);
    ~HadamardGate();
};

/* CNOT Gate, spanning two qubits. 
 * V = 1 0 0 0
 *     0 1 0 0 
 *     0 0 0 1
 *     0 0 1 0 
 */
class CNOT : public Gate {
    public:
    CNOT(std::vector<int> nlevels_, std::vector<int> nessential_);
    ~CNOT();
};

