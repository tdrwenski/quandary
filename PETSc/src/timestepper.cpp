#include "timestepper.hpp"


PetscErrorCode RHSJacobian(TS ts,PetscReal t,Vec u,Mat M,Mat P,void *ctx){

  /* Cast ctx to Hamiltonian pointer */
  Hamiltonian *hamiltonian = (Hamiltonian*) ctx;

  /* Assembling the Hamiltonian will set the matrix RHS from Re, Im */
  hamiltonian->assemble_RHS(t);

  /* Set the hamiltonian system matrix */
  M = hamiltonian->getRHS();

  return 0;
}

PetscErrorCode RHSJacobianP(TS ts, PetscReal t, Vec y, Mat A, void *ctx){

  /* Cast ctx to Hamiltonian pointer */
  Hamiltonian *hamiltonian = (Hamiltonian*) ctx;

  /* Assembling the derivative of RHS with respect to the control parameters */
  hamiltonian->assemble_dRHSdp(t, y);

  /* Set the derivative */
  A = hamiltonian->getdRHSdp();

  return 0;
}


PetscErrorCode TSInit(TS ts, Hamiltonian* hamiltonian, PetscInt NSteps, PetscReal Dt, PetscReal Tfinal, bool monitor){
  int ierr;

  ierr = TSSetProblemType(ts,TS_LINEAR);CHKERRQ(ierr);
  ierr = TSSetType(ts, TSTHETA); CHKERRQ(ierr);
  ierr = TSThetaSetTheta(ts, 0.5); CHKERRQ(ierr);   // midpoint rule
  ierr = TSSetRHSFunction(ts,NULL,TSComputeRHSFunctionLinear,hamiltonian);CHKERRQ(ierr);
  ierr = TSSetRHSJacobian(ts,hamiltonian->getRHS(),hamiltonian->getRHS(),RHSJacobian,hamiltonian);CHKERRQ(ierr);
  ierr = TSSetTimeStep(ts,Dt);CHKERRQ(ierr);
  ierr = TSSetMaxSteps(ts,NSteps);CHKERRQ(ierr);
  ierr = TSSetMaxTime(ts,Tfinal);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_STEPOVER);CHKERRQ(ierr);
  if (monitor) {
    ierr = TSMonitorSet(ts, Monitor, NULL, NULL); CHKERRQ(ierr);
  }
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);

  return ierr;
}



PetscErrorCode Monitor(TS ts,PetscInt step,PetscReal t,Vec x,void *ctx) {
  PetscErrorCode ierr;
  PetscFunctionBeginUser;

  const PetscScalar *x_ptr;
  double tprev;
  ierr = TSGetPrevTime(ts, &tprev); CHKERRQ(ierr);
  ierr = VecGetArrayRead(x, &x_ptr);
  printf("Step %d: %f -> %f, x[1]=%1.14e\n", step, tprev, t, x_ptr[1]);
  ierr = VecRestoreArrayRead(x, &x_ptr);

  PetscFunctionReturn(0);
}


PetscErrorCode TSPrepare(TS ts){
  int ierr; 

  ierr = TSSetUp(ts); CHKERRQ(ierr);
  ierr = TSTrajectorySetUp(ts->trajectory,ts);CHKERRQ(ierr);
  /* reset time step and iteration counters */
  if (!ts->steps) {
    ts->ksp_its           = 0;
    ts->snes_its          = 0;
    ts->num_snes_failures = 0;
    ts->reject            = 0;
    ts->steprestart       = PETSC_TRUE;
    ts->steprollback      = PETSC_FALSE;
  }
  ts->reason = TS_CONVERGED_ITERATING;
  if (ts->steps >= ts->max_steps) ts->reason = TS_CONVERGED_ITS;
  else if (ts->ptime >= ts->max_time) ts->reason = TS_CONVERGED_TIME;

  if (!ts->steps) {
    ierr = TSTrajectorySet(ts->trajectory,ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
  }

  return ierr;
}


PetscErrorCode TSStepMod(TS ts){
  int ierr; 

    ierr = TSMonitor(ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
    ierr = TSPreStep(ts);CHKERRQ(ierr);
    ierr = TSStep(ts);CHKERRQ(ierr);

    ierr = TSPostEvaluate(ts);CHKERRQ(ierr);

    ierr = TSTrajectorySet(ts->trajectory,ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
    ierr = TSPostStep(ts);CHKERRQ(ierr);
  return ierr;
}
