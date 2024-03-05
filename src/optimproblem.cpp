#include "optimproblem.hpp"

OptimProblem::OptimProblem(MapParam config, TimeStepper* timestepper_, MPI_Comm comm_init_, MPI_Comm comm_optim_, int ninit_, Output* output_, bool quietmode_){

  timestepper = timestepper_;
  ninit = ninit_;
  output = output_;
  quietmode = quietmode_;
  /* Reset */
  objective = 0.0;

  /* Store communicators */
  comm_init = comm_init_;
  comm_optim = comm_optim_;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
  MPI_Comm_size(MPI_COMM_WORLD, &mpisize_world);
  MPI_Comm_rank(PETSC_COMM_WORLD, &mpirank_space);
  MPI_Comm_size(PETSC_COMM_WORLD, &mpisize_space);
  MPI_Comm_rank(comm_init, &mpirank_init);
  MPI_Comm_size(comm_init, &mpisize_init);
  MPI_Comm_rank(comm_optim, &mpirank_optim);
  MPI_Comm_size(comm_optim, &mpisize_optim);

  /* Store number of initial conditions per init-processor group */
  ninit_local = ninit / mpisize_init; 

  /*  If Schroedingers solver, allocate storage for the final states at time T for each initial condition. Schroedinger's solver does not store the time-trajectories during forward ODE solve, but instead recomputes the primal states during the adjoint solve. Therefore we need to store the terminal condition for the backwards primal solve. Be aware that the final states stored here will be overwritten during backwards computation!! */
  if (timestepper->mastereq->lindbladtype == LindbladType::NONE) {
    for (int i = 0; i < ninit_local; i++) {
      Vec state;
      VecCreate(PETSC_COMM_WORLD, &state);
      VecSetSizes(state, PETSC_DECIDE, 2*timestepper->mastereq->getDim());
      VecSetFromOptions(state);
      store_finalstates.push_back(state);
    }
  }

  /* Store number of design parameters */
  int n = 0;
  for (int ioscil = 0; ioscil < timestepper->mastereq->getNOscillators(); ioscil++) {
      n += timestepper->mastereq->getOscillator(ioscil)->getNParams(); 
  }
  ndesign = n;
  if (mpirank_world == 0 && !quietmode) std::cout<< "ndesign = " << ndesign << std::endl;

  /* Allocate the initial condition vector and adjoint terminal state */
  VecCreate(PETSC_COMM_WORLD, &rho_t0); 
  VecSetSizes(rho_t0,PETSC_DECIDE,2*timestepper->mastereq->getDim());
  VecSetFromOptions(rho_t0);
  VecZeroEntries(rho_t0);
  VecAssemblyBegin(rho_t0); VecAssemblyEnd(rho_t0);
  VecDuplicate(rho_t0, &rho_t0_bar);
  VecZeroEntries(rho_t0_bar);
  VecAssemblyBegin(rho_t0_bar); VecAssemblyEnd(rho_t0_bar);

  /* Initialize the optimization target, including setting of initial state rho_t0 if read from file or pure state or ensemble */
  std::vector<std::string> target_str;
  config.GetVecStrParam("optim_target", target_str, "pure");
  std::string objective_str = config.GetStrParam("optim_objective", "Jfrobenius");
  std::vector<double> read_gate_rot;
  config.GetVecDoubleParam("gate_rot_freq", read_gate_rot, 1e20); 
  std::vector<std::string> initcond_str;
  config.GetVecStrParam("initialcondition", initcond_str, "none", false);
  optim_target = new OptimTarget(target_str, objective_str, initcond_str, timestepper->mastereq, timestepper->total_time, read_gate_rot, rho_t0, quietmode);

  /* Get weights for the objective function (weighting the different initial conditions */
  config.GetVecDoubleParam("optim_weights", obj_weights, 1.0);
  int nfill = 0;
  if (obj_weights.size() < ninit) nfill = ninit - obj_weights.size();
  double val = obj_weights[obj_weights.size()-1];
  if (obj_weights.size() < ninit){
    for (int i = 0; i < nfill; i++) 
      obj_weights.push_back(val);
  }
  assert(obj_weights.size() >= ninit);
  // Scale the weights such that they sum up to one: beta_i <- beta_i / (\sum_i beta_i)
  double scaleweights = 0.0;
  for (int i=0; i<ninit; i++) scaleweights += obj_weights[i];
  for (int i=0; i<ninit; i++) obj_weights[i] = obj_weights[i] / scaleweights;
  // Distribute over mpi_init processes 
  double sendbuf[obj_weights.size()];
  double recvbuf[obj_weights.size()];
  for (int i = 0; i < obj_weights.size(); i++) sendbuf[i] = obj_weights[i];
  for (int i = 0; i < obj_weights.size(); i++) recvbuf[i] = obj_weights[i];
  int nscatter = ninit_local;
  MPI_Scatter(sendbuf, nscatter, MPI_DOUBLE, recvbuf, nscatter,  MPI_DOUBLE, 0, comm_init);
  for (int i = 0; i < nscatter; i++) obj_weights[i] = recvbuf[i];
  for (int i=nscatter; i < obj_weights.size(); i++) obj_weights[i] = 0.0;

  /* Store other optimization parameters */
  gamma_tik = config.GetDoubleParam("optim_regul", 1e-4);
  gatol = config.GetDoubleParam("optim_atol", 1e-8);
  fatol = config.GetDoubleParam("optim_ftol", 1e-8);
  inftol = config.GetDoubleParam("optim_inftol", 1e-5);
  grtol = config.GetDoubleParam("optim_rtol", 1e-4);
  maxiter = config.GetIntParam("optim_maxiter", 200);
  gamma_penalty = config.GetDoubleParam("optim_penalty", 1e-4);
  penalty_param = config.GetDoubleParam("optim_penalty_param", 0.5);

  /* Pass information on objective function to the time stepper needed for penalty objective function */
  timestepper->penalty_param = penalty_param;
  timestepper->gamma_penalty = gamma_penalty;
  timestepper->optim_target = optim_target;

  /* Store optimization bounds */
  VecCreateSeq(PETSC_COMM_SELF, ndesign, &xlower);
  VecSetFromOptions(xlower);
  VecDuplicate(xlower, &xupper);
  int col = 0;
  for (int iosc = 0; iosc < timestepper->mastereq->getNOscillators(); iosc++){
    std::vector<std::string> bound_str;
    config.GetVecStrParam("control_bounds" + std::to_string(iosc), bound_str, "10000.0");
    for (int iseg = 0; iseg < timestepper->mastereq->getOscillator(iosc)->getNSegments(); iseg++){
      double boundval = 0.0;
      if (bound_str.size() <= iseg) boundval =  atof(bound_str[bound_str.size()-1].c_str());
      else boundval = atof(bound_str[iseg].c_str());
      // If spline controls: Scale bounds by 1/sqrt(2) * (number of carrier waves) */
      if (timestepper->mastereq->getOscillator(iosc)->getNParams()>1)
        boundval = boundval / ( sqrt(2) * timestepper->mastereq->getOscillator(iosc)->getNCarrierfrequencies()) ;
      for (int i=0; i<timestepper->mastereq->getOscillator(iosc)->getNSegParams(iseg); i++){
        VecSetValue(xupper, col, boundval, INSERT_VALUES);
        VecSetValue(xlower, col, -1. * boundval, INSERT_VALUES);
        col++;
      }
    }
  }
  VecAssemblyBegin(xlower); VecAssemblyEnd(xlower);
  VecAssemblyBegin(xupper); VecAssemblyEnd(xupper);

  /* Store the initial guess if read from file */
  std::vector<std::string> controlinit_str;
  config.GetVecStrParam("control_initialization0", controlinit_str, "constant, 0.0");
  if ( controlinit_str.size() > 0 && controlinit_str[0].compare("file") == 0 ) {
    assert(controlinit_str.size() >=2);
    for (int i=0; i<ndesign; i++) initguess_fromfile.push_back(0.0);
    if (mpirank_world == 0) read_vector(controlinit_str[1].c_str(), initguess_fromfile.data(), ndesign, quietmode);
    MPI_Bcast(initguess_fromfile.data(), ndesign, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  }
 
  /* Create Petsc's optimization solver */
  TaoCreate(PETSC_COMM_WORLD, &tao);
  /* Set optimization type and parameters */
  TaoSetType(tao,TAOBQNLS);         // Optim type: taoblmvm vs BQNLS ??
  TaoSetMaximumIterations(tao, maxiter);
  TaoSetTolerances(tao, gatol, PETSC_DEFAULT, grtol);
  TaoSetMonitor(tao, TaoMonitor, (void*)this, NULL);
  TaoSetVariableBounds(tao, xlower, xupper);
  TaoSetFromOptions(tao);
  /* Set user-defined objective and gradient evaluation routines */
  TaoSetObjective(tao, TaoEvalObjective, (void *)this);
  TaoSetGradient(tao, NULL, TaoEvalGradient,(void *)this);
  TaoSetObjectiveAndGradient(tao, NULL, TaoEvalObjectiveAndGradient, (void*) this);

  /* Allocate auxiliary vector */
  mygrad = new double[ndesign];
}


OptimProblem::~OptimProblem() {
  delete [] mygrad;
  delete optim_target;
  VecDestroy(&rho_t0);
  VecDestroy(&rho_t0_bar);

  VecDestroy(&xlower);
  VecDestroy(&xupper);

  for (int i = 0; i < store_finalstates.size(); i++) {
    VecDestroy(&(store_finalstates[i]));
  }

  TaoDestroy(&tao);
}



double OptimProblem::evalF(const Vec x) {

  MasterEq* mastereq = timestepper->mastereq;

  if (mpirank_world == 0 && !quietmode) printf("EVAL F... \n");
  Vec finalstate = NULL;

  /* Pass design vector x to oscillators */
  mastereq->setControlAmplitudes(x); 

  /*  Iterate over initial condition */
  obj_cost  = 0.0;
  obj_regul = 0.0;
  obj_penal = 0.0;
  fidelity = 0.0;
  double obj_cost_re = 0.0;
  double obj_cost_im = 0.0;
  double fidelity_re = 0.0;
  double fidelity_im = 0.0;
  for (int iinit = 0; iinit < ninit_local; iinit++) {
      
    /* Prepare the initial condition in [rank * ninit_local, ... , (rank+1) * ninit_local - 1] */
    int iinit_global = mpirank_init * ninit_local + iinit;
    int initid = optim_target->prepareInitialState(iinit_global, ninit, timestepper->mastereq->nlevels, timestepper->mastereq->nessential, rho_t0);
    if (mpirank_optim == 0 && !quietmode) printf("%d: Initial condition id=%d ...\n", mpirank_init, initid);

    /* If gate optimiztion, compute the target state rho^target = Vrho(0)V^dagger */
    optim_target->prepareTargetState(rho_t0);

    /* Run forward with initial condition initid */
    finalstate = timestepper->solveODE(initid, rho_t0);

    /* Add to integral penalty term */
    obj_penal += obj_weights[iinit] * gamma_penalty * timestepper->penalty_integral;

    /* Evaluate J(finalstate) and add to final-time cost */
    double obj_iinit_re = 0.0;
    double obj_iinit_im = 0.0;
    optim_target->evalJ(finalstate,  &obj_iinit_re, &obj_iinit_im);
    obj_cost_re += obj_weights[iinit] * obj_iinit_re;
    obj_cost_im += obj_weights[iinit] * obj_iinit_im;

    /* Add to final-time fidelity */
    double fidelity_iinit_re = 0.0;
    double fidelity_iinit_im = 0.0;
    optim_target->HilbertSchmidtOverlap(finalstate, false, &fidelity_iinit_re, &fidelity_iinit_im);
    fidelity_re += 1./ ninit * fidelity_iinit_re;
    fidelity_im += 1./ ninit * fidelity_iinit_im;

    // printf("%d, %d: iinit obj_iinit: %f * (%1.14e + i %1.14e, Overlap=%1.14e + i %1.14e\n", mpirank_world, mpirank_init, obj_weights[iinit], obj_iinit_re, obj_iinit_im, fidelity_iinit_re, fidelity_iinit_im);
  }

  /* Sum up from initial conditions processors */
  double mypen = obj_penal;
  double mycost_re = obj_cost_re;
  double mycost_im = obj_cost_im;
  double myfidelity_re = fidelity_re;
  double myfidelity_im = fidelity_im;
  MPI_Allreduce(&mypen, &obj_penal, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&mycost_re, &obj_cost_re, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&mycost_im, &obj_cost_im, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&myfidelity_re, &fidelity_re, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&myfidelity_im, &fidelity_im, 1, MPI_DOUBLE, MPI_SUM, comm_init);

  /* Set the fidelity: If Schroedinger, need to compute the absolute value: Fid= |\sum_i \phi^\dagger \phi_target|^2 */
  if (timestepper->mastereq->lindbladtype == LindbladType::NONE) {
    fidelity = pow(fidelity_re, 2.0) + pow(fidelity_im, 2.0);
  } else {
    fidelity = fidelity_re; 
  }
 
  /* Finalize the objective function */
  obj_cost = optim_target->finalizeJ(obj_cost_re, obj_cost_im);

  /* Evaluate regularization objective += gamma/2 * ||x||^2*/
  double xnorm;
  VecNorm(x, NORM_2, &xnorm);
  obj_regul = gamma_tik / 2. * pow(xnorm,2.0);

  /* Sum, store and return objective value */
  objective = obj_cost + obj_regul + obj_penal;

  /* Output */
  if (mpirank_world == 0) {
    std::cout<< "Objective = " << std::scientific<<std::setprecision(14) << obj_cost << " + " << obj_regul << " + " << obj_penal << std::endl;
    std::cout<< "Fidelity = " << fidelity  << std::endl;
  }

  return objective;
}



void OptimProblem::evalGradF(const Vec x, Vec G){

  MasterEq* mastereq = timestepper->mastereq;

  if (mpirank_world == 0 && !quietmode) std::cout<< "EVAL GRAD F... " << std::endl;
  Vec finalstate = NULL;

  /* Pass design vector x to oscillators */
  mastereq->setControlAmplitudes(x); 

  /* Reset Gradient */
  VecZeroEntries(G);

  /* Derivative of regulatization term gamma / 2 ||x||^2 (ADD ON ONE PROC ONLY!) */
  // if (mpirank_init == 0 && mpirank_optim == 0) { // TODO: Which one?? 
  if (mpirank_init == 0 ) {
    VecAXPY(G, gamma_tik, x);
  }

  /*  Iterate over initial condition */
  obj_cost = 0.0;
  obj_regul = 0.0;
  obj_penal = 0.0;
  fidelity = 0.0;
  double obj_cost_re = 0.0;
  double obj_cost_im = 0.0;
  double fidelity_re = 0.0;
  double fidelity_im = 0.0;
  for (int iinit = 0; iinit < ninit_local; iinit++) {

    /* Prepare the initial condition */
    int iinit_global = mpirank_init * ninit_local + iinit;
    int initid = optim_target->prepareInitialState(iinit_global, ninit, timestepper->mastereq->nlevels, timestepper->mastereq->nessential, rho_t0);

    /* If gate optimiztion, compute the target state rho^target = Vrho(0)V^dagger */
    optim_target->prepareTargetState(rho_t0);

    /* --- Solve primal --- */
    // if (mpirank_optim == 0) printf("%d: %d FWD. ", mpirank_init, initid);

    /* Run forward with initial condition rho_t0 */
    finalstate = timestepper->solveODE(initid, rho_t0);

    /* Store the final state for the Schroedinger solver */
    if (timestepper->mastereq->lindbladtype == LindbladType::NONE) VecCopy(finalstate, store_finalstates[iinit]);

    /* Add to integral penalty term */
    obj_penal += obj_weights[iinit] * gamma_penalty * timestepper->penalty_integral;

    /* Evaluate J(finalstate) and add to final-time cost */
    double obj_iinit_re = 0.0;
    double obj_iinit_im = 0.0;
    optim_target->evalJ(finalstate,  &obj_iinit_re, &obj_iinit_im);
    obj_cost_re += obj_weights[iinit] * obj_iinit_re;
    obj_cost_im += obj_weights[iinit] * obj_iinit_im;

    /* Add to final-time fidelity */
    double fidelity_iinit_re = 0.0;
    double fidelity_iinit_im = 0.0;
    optim_target->HilbertSchmidtOverlap(finalstate, false, &fidelity_iinit_re, &fidelity_iinit_im);
    fidelity_re += 1./ ninit * fidelity_iinit_re;
    fidelity_im += 1./ ninit * fidelity_iinit_im;

    /* If Lindblas solver, compute adjoint for this initial condition. Otherwise (Schroedinger solver), compute adjoint only after all initial conditions have been propagated through (separate loop below) */
    if (timestepper->mastereq->lindbladtype != LindbladType::NONE) {
      // if (mpirank_optim == 0) printf("%d: %d BWD.", mpirank_init, initid);

      /* Reset adjoint */
      VecZeroEntries(rho_t0_bar);

      /* Terminal condition for adjoint variable: Derivative of final time objective J */
      double obj_cost_re_bar, obj_cost_im_bar;
      optim_target->finalizeJ_diff(obj_cost_re, obj_cost_im, &obj_cost_re_bar, &obj_cost_im_bar);
      optim_target->evalJ_diff(finalstate, rho_t0_bar, obj_weights[iinit]*obj_cost_re_bar, obj_weights[iinit]*obj_cost_im_bar);

      /* Derivative of time-stepping */
      timestepper->solveAdjointODE(initid, rho_t0_bar, finalstate, obj_weights[iinit] * gamma_penalty);

      /* Add to optimizers's gradient */
      VecAXPY(G, 1.0, timestepper->redgrad);
    }
  }

  /* Sum up from initial conditions processors */
  double mypen = obj_penal;
  double mycost_re = obj_cost_re;
  double mycost_im = obj_cost_im;
  double myfidelity_re = fidelity_re;
  double myfidelity_im = fidelity_im;
  MPI_Allreduce(&mypen, &obj_penal, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&mycost_re, &obj_cost_re, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&mycost_im, &obj_cost_im, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&myfidelity_re, &fidelity_re, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  MPI_Allreduce(&myfidelity_im, &fidelity_im, 1, MPI_DOUBLE, MPI_SUM, comm_init);

  /* Set the fidelity: If Schroedinger, need to compute the absolute value: Fid= |\sum_i \phi^\dagger \phi_target|^2 */
  if (timestepper->mastereq->lindbladtype == LindbladType::NONE) {
    fidelity = pow(fidelity_re, 2.0) + pow(fidelity_im, 2.0);
  } else {
    fidelity = fidelity_re; 
  }
 
  /* Finalize the objective function Jtrace to get the infidelity. 
     If Schroedingers solver, need to take the absolute value */
  obj_cost = optim_target->finalizeJ(obj_cost_re, obj_cost_im);

  /* Evaluate regularization objective += gamma/2 * ||x||^2*/
  double xnorm;
  VecNorm(x, NORM_2, &xnorm);
  obj_regul = gamma_tik / 2. * pow(xnorm,2.0);

  /* Sum, store and return objective value */
  objective = obj_cost + obj_regul + obj_penal;

  /* For Schroedinger solver: Solve adjoint equations for all initial conditions here. */
  if (timestepper->mastereq->lindbladtype == LindbladType::NONE) {

    // Iterate over all initial conditions 
    for (int iinit = 0; iinit < ninit_local; iinit++) {
      int iinit_global = mpirank_init * ninit_local + iinit;

      /* Recompute the initial state and target */
      int initid = optim_target->prepareInitialState(iinit_global, ninit, timestepper->mastereq->nlevels, timestepper->mastereq->nessential, rho_t0);
      optim_target->prepareTargetState(rho_t0);
     
      /* Get the last time step (finalstate) */
      finalstate = store_finalstates[iinit];

      /* Reset adjoint */
      VecZeroEntries(rho_t0_bar);

      /* Terminal condition for adjoint variable: Derivative of final time objective J */
      double obj_cost_re_bar, obj_cost_im_bar;
      optim_target->finalizeJ_diff(obj_cost_re, obj_cost_im, &obj_cost_re_bar, &obj_cost_im_bar);
      optim_target->evalJ_diff(finalstate, rho_t0_bar, obj_weights[iinit]*obj_cost_re_bar, obj_weights[iinit]*obj_cost_im_bar);

      /* Derivative of time-stepping */
      timestepper->solveAdjointODE(initid, rho_t0_bar, finalstate, obj_weights[iinit] * gamma_penalty);

      /* Add to optimizers's gradient */
      VecAXPY(G, 1.0, timestepper->redgrad);
    } // end of initial condition loop 
  } // end of adjoint for Schroedinger

  /* Sum up the gradient from all initial condition processors */
  PetscScalar* grad; 
  VecGetArray(G, &grad);
  for (int i=0; i<ndesign; i++) {
    mygrad[i] = grad[i];
  }
  MPI_Allreduce(mygrad, grad, ndesign, MPI_DOUBLE, MPI_SUM, comm_init);
  VecRestoreArray(G, &grad);

  /* Compute and store gradient norm */
  VecNorm(G, NORM_2, &(gnorm));

  /* Output */
  if (mpirank_world == 0 && !quietmode) {
    std::cout<< "Objective = " << std::scientific<<std::setprecision(14) << obj_cost << " + " << obj_regul << " + " << obj_penal << std::endl;
    std::cout<< "Fidelity = " << fidelity << std::endl;
  }
}


void OptimProblem::solve(Vec xinit) {
  TaoSetSolution(tao, xinit);
  TaoSolve(tao);
}

void OptimProblem::getStartingPoint(Vec xinit){
  MasterEq* mastereq = timestepper->mastereq;

  if (initguess_fromfile.size() > 0) {
    /* Set the initial guess from file */
    for (int i=0; i<initguess_fromfile.size(); i++) {
      VecSetValue(xinit, i, initguess_fromfile[i], INSERT_VALUES);
    }

  } else { // copy from initialization in oscillators contructor
    PetscScalar* xptr;
    VecGetArray(xinit, &xptr);
    int shift = 0;
    for (int ioscil = 0; ioscil<mastereq->getNOscillators(); ioscil++){
      mastereq->getOscillator(ioscil)->getParams(xptr + shift);
      shift += mastereq->getOscillator(ioscil)->getNParams();
    }
    VecRestoreArray(xinit, &xptr);
  }

  /* Assemble initial guess */
  VecAssemblyBegin(xinit);
  VecAssemblyEnd(xinit);

  /* Pass to oscillator */
  timestepper->mastereq->setControlAmplitudes(xinit);
  
  /* Write initial control functions to file */
  output->writeControls(xinit, timestepper->mastereq, timestepper->ntime, timestepper->dt);

}


void OptimProblem::getSolution(Vec* param_ptr){
  
  /* Get ref to optimized parameters */
  Vec params;
  TaoGetSolution(tao, &params);
  *param_ptr = params;
}

PetscErrorCode TaoMonitor(Tao tao,void*ptr){
  OptimProblem* ctx = (OptimProblem*) ptr;

  /* Get information from Tao optimization */
  PetscInt iter;
  PetscScalar deltax;
  Vec params;
  TaoConvergedReason reason;
  PetscScalar f, gnorm;
  TaoGetSolutionStatus(tao, &iter, &f, &gnorm, NULL, &deltax, &reason);
  TaoGetSolution(tao, &params);

  /* Pass current iteration number to output manager */
  ctx->output->optim_iter = iter;

  /* Grab some output stuff */
  double obj_cost = ctx->getCostT();
  double obj_regul = ctx->getRegul();
  double obj_penal = ctx->getPenalty();
  double F_avg = ctx->getFidelity();

  /* Print to optimization file */
  ctx->output->writeOptimFile(f, gnorm, deltax, F_avg, obj_cost, obj_regul, obj_penal);

  /* Print parameters and controls to file */
  ctx->output->writeControls(params, ctx->timestepper->mastereq, ctx->timestepper->ntime, ctx->timestepper->dt);

  /* Screen output */
  if (ctx->getMPIrank_world() == 0) {
    std::cout<< iter <<  ": Objective = " << std::scientific<<std::setprecision(14) << obj_cost << " + " << obj_regul << " + " << obj_penal;
    std::cout<< "  Fidelity = " << F_avg;
    std::cout<< "  ||Grad|| = " << gnorm;
    std::cout<< std::endl;
  }

  if (1.0 - F_avg <= ctx->getInfTol()) {
    printf("Optimization finished with small infidelity.\n");
    TaoSetConvergedReason(tao, TAO_CONVERGED_USER);
  } else if (obj_cost <= ctx->getFaTol()) {
    printf("Optimization finished with small final time cost.\n");
    TaoSetConvergedReason(tao, TAO_CONVERGED_USER);
  }

  return 0;
}


PetscErrorCode TaoEvalObjectiveAndGradient(Tao tao, Vec x, PetscReal *f, Vec G, void*ptr){

  TaoEvalGradient(tao, x, G, ptr);
  OptimProblem* ctx = (OptimProblem*) ptr;
  *f = ctx->getObjective();

  return 0;
}

PetscErrorCode TaoEvalObjective(Tao tao, Vec x, PetscReal *f, void*ptr){

  OptimProblem* ctx = (OptimProblem*) ptr;
  *f = ctx->evalF(x);
  
  return 0;
}


PetscErrorCode TaoEvalGradient(Tao tao, Vec x, Vec G, void*ptr){

  OptimProblem* ctx = (OptimProblem*) ptr;
  ctx->evalGradF(x, G);
  
  return 0;
}
