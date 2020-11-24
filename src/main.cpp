#ifdef WITH_BRAID
#include "braid_wrapper.hpp"
#endif
#include "timestepper.hpp"
#include "bspline.hpp"
#include "oscillator.hpp" 
#include "mastereq.hpp"
#include "config.hpp"
#include <stdlib.h>
#include <sys/resource.h>
#include "optimproblem.hpp"
#include "output.hpp"

#define TEST_FD 0    // Finite Differences gradient test
#define EPS 1e-4     // Epsilon for Finite Differences

int main(int argc,char **argv)
{
  char filename[255];
  PetscErrorCode ierr;

  /* Initialize MPI */
  MPI_Init(&argc, &argv);
  int mpisize_world, mpirank_world;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
  MPI_Comm_size(MPI_COMM_WORLD, &mpisize_world);
  if (mpirank_world == 0) printf("Running on %d cores.\n", mpisize_world);

  /* Read config file */
  if (argc != 2) {
    if (mpirank_world == 0) {
      printf("\nUSAGE: ./main </path/to/configfile> \n");
    }
    MPI_Finalize();
    return 0;
  }
  std::stringstream log;
  MapParam config(MPI_COMM_WORLD, log);
  config.ReadFile(argv[1]);

  /* --- Get some options from the config file --- */
  std::vector<int> nlevels;
  config.GetVecIntParam("nlevels", nlevels, 0);
  int ntime = config.GetIntParam("ntime", 1000);
  double dt    = config.GetDoubleParam("dt", 0.01);
  int nspline = config.GetIntParam("nspline", 10);
  PetscBool monitor = (PetscBool) config.GetBoolParam("monitor", false);
  RunType runtype;
  std::string runtypestr = config.GetStrParam("runtype", "primal");
  if      (runtypestr.compare("primal")      == 0) runtype = primal;
  else if (runtypestr.compare("adjoint")     == 0) runtype = adjoint;
  else if (runtypestr.compare("optimization")== 0) runtype = optimization;
  else {
    printf("\n\n WARNING: Unknown runtype: %s.\n\n", runtypestr.c_str());
    runtype = none;
  }

  /* Get type and the total number of initial conditions */
  int ninit = 1;
  std::vector<std::string> initcondstr;
  config.GetVecStrParam("initialcondition", initcondstr, "basis");
  assert (initcondstr.size() >= 1);
  if      (initcondstr[0].compare("file") == 0 ) ninit = 1;
  else if (initcondstr[0].compare("pure") == 0 ) ninit = 1;
  else if ( initcondstr[0].compare("diagonal") == 0 ||
            initcondstr[0].compare("basis")    == 0  ) {
    /* Compute ninit = dim(subsystem defined by list of oscil IDs) */
    ninit = 1;
    if (initcondstr.size() < 2) {
      printf("ERROR for initial condition option: If choosing 'basis' or 'diagonal', specify the list oscillator IDs!\n");
      exit(1);
    }
    for (int i = 1; i<initcondstr.size(); i++){
      int oscilID = atoi(initcondstr[i].c_str());
      ninit *= nlevels[oscilID];
    }
    if (initcondstr[0].compare("basis") == 0  ) ninit = (int) pow(ninit,2.0);
  }
  else {
    printf("\n\n ERROR: Wrong setting for initial condition.\n");
    exit(1);
  }

  /* --- Split communicators for distributed initial conditions, distributed linear algebra, time-parallel braid (and parallel optimizer, if HiOp) --- */
  int mpirank_init, mpisize_init;
  int mpirank_braid, mpisize_braid;
  int mpirank_petsc, mpisize_petsc;
  MPI_Comm comm_braid, comm_init, comm_petsc, comm_hiop;

  /* Split aside communicator for hiop. Size 1 for now */  
  MPI_Comm_split(MPI_COMM_WORLD, mpirank_world, mpirank_world, &comm_hiop);

  /* Get the size of communicators  */
#ifdef WITH_BRAID
  int np_braid = config.GetIntParam("np_braid", 1);
  np_braid = min(np_braid, mpisize_world); 
#else 
  int np_braid = 1; 
#endif
  int np_init  = min(ninit, config.GetIntParam("np_init", 1)); 
  np_init  = min(np_init,  mpisize_world); 
  int np_petsc = mpisize_world / (np_init * np_braid);

  /* Sanity check for communicator sizes */ 
  if (ninit % np_init != 0){
    printf("ERROR: Wrong processor distribution! \n Size of communicator for distributing initial conditions (%d) must be integer divisor of the total number of initial conditions (%d)!!\n", np_init, ninit);
    exit(1);
  }
  if (mpisize_world % (np_init * np_braid) != 0) {
    printf("ERROR: Wrong number of threads! \n Total number of threads (%d) must be integer multiple of the product of communicator sizes for initial conditions and braid (%d * %d)!\n", mpisize_world, np_init, np_braid);
    exit(1);
  }

  /* Split communicators */
  // Distributed initial conditions 
  int color_init = mpirank_world % (np_petsc * np_braid);
  MPI_Comm_split(MPI_COMM_WORLD, color_init, mpirank_world, &comm_init);
  MPI_Comm_rank(comm_init, &mpirank_init);
  MPI_Comm_size(comm_init, &mpisize_init);

#ifdef WITH_BRAID
  // Time-parallel Braid
  int color_braid = mpirank_world % np_petsc + mpirank_init * np_petsc;
  MPI_Comm_split(MPI_COMM_WORLD, color_braid, mpirank_world, &comm_braid);
  MPI_Comm_rank(comm_braid, &mpirank_braid);
  MPI_Comm_size(comm_braid, &mpisize_braid);
#else 
  mpirank_braid = 0;
  mpisize_braid = 1;
#endif

  // Distributed Linear algebra: Petsc
  int color_petsc = mpirank_world / np_petsc;
  MPI_Comm_split(MPI_COMM_WORLD, color_petsc, mpirank_world, &comm_petsc);
  MPI_Comm_rank(comm_petsc, &mpirank_petsc);
  MPI_Comm_size(comm_petsc, &mpisize_petsc);

  std::cout<< "Parallel distribution: " << "init " << mpirank_init << "/" << mpisize_init;
  std::cout<< ", petsc " << mpirank_petsc << "/" << mpisize_petsc;
#ifdef WITH_BRAID
  std::cout<< ", braid " << mpirank_braid << "/" << mpisize_braid;
#endif
  std::cout<< std::endl;

  /* Initialize Petsc using petsc's communicator */
  PETSC_COMM_WORLD = comm_petsc;
  ierr = PetscInitialize(&argc,&argv,(char*)0,NULL);if (ierr) return ierr;
  PetscViewerPushFormat(PETSC_VIEWER_STDOUT_WORLD, 	PETSC_VIEWER_ASCII_MATLAB );


  double total_time = ntime * dt;

  /* --- Initialize the Oscillators --- */
  Oscillator** oscil_vec = new Oscillator*[nlevels.size()];
  // Get fundamental and rotation frequencies from config file 
  std::vector<double> fundamental_freq, rot_freq;
  config.GetVecDoubleParam("transfreq", fundamental_freq, 1e20);
  if (fundamental_freq.size() < nlevels.size()) {
    printf("Error: Number of given fundamental frequencies (%lu) is smaller than the the number of oscillators (%lu)\n", fundamental_freq.size(), nlevels.size());
    exit(1);
  } 
  config.GetVecDoubleParam("rotfreq", rot_freq, 1e20);
  if (rot_freq.size() < nlevels.size()) {
    printf("Error: Number of given rotation frequencies (%lu) is smaller than the the number of oscillators (%lu)\n", rot_freq.size(), nlevels.size());
    exit(1);
  } 
  std::vector<double> detuning_freq(rot_freq.size());
  for(int i=0; i<rot_freq.size(); i++) {
    detuning_freq[i] = fundamental_freq[i] - rot_freq[i];
  }
  // Create the oscillators 
  for (int i = 0; i < nlevels.size(); i++){
    std::vector<double> carrier_freq;
    std::string key = "carrier_frequency" + std::to_string(i);
    config.GetVecDoubleParam(key, carrier_freq, 0.0);
    oscil_vec[i] = new Oscillator(i, nlevels, nspline, fundamental_freq[i], carrier_freq, total_time);
  }

  // Get pi-pulses, if any
  std::vector<std::string> pipulse_str;
  config.GetVecStrParam("apply_pipulse", pipulse_str, "none");
  if (pipulse_str[0].compare("none") != 0) { // There is at least one pipulse to be applied!
    // sanity check
    if (pipulse_str.size() % 4 != 0) {
      printf("Wrong pi-pulse configuration. Number of elements must be multiple of 4!\n");
      printf("apply_pipulse config option: <oscilID>, <tstart>, <tstop>, <amp>, <anotherOscilID>, <anotherTstart>, <anotherTstop>, <anotherAmp> ...\n");
      exit(1);
    }
    int k=0;
    while (k < pipulse_str.size()){
      // Set pipulse for this oscillator
      int pipulse_id = atoi(pipulse_str[k+0].c_str());
      oscil_vec[pipulse_id]->pipulse.tstart.push_back(atof(pipulse_str[k+1].c_str()));
      oscil_vec[pipulse_id]->pipulse.tstop.push_back(atof(pipulse_str[k+2].c_str()));
      oscil_vec[pipulse_id]->pipulse.amp.push_back(atof(pipulse_str[k+3].c_str()));
      if (mpirank_world==0) printf("Applying PiPulse to oscillator %d in [%f,%f]: |p+iq|=%f\n", pipulse_id, oscil_vec[pipulse_id]->pipulse.tstart.back(), oscil_vec[pipulse_id]->pipulse.tstop.back(), oscil_vec[pipulse_id]->pipulse.amp.back());
      // Set zero control for all other oscillators during this pipulse
      for (int i=0; i<nlevels.size(); i++){
        if (i != pipulse_id) {
          oscil_vec[i]->pipulse.tstart.push_back(atof(pipulse_str[k+1].c_str()));
          oscil_vec[i]->pipulse.tstop.push_back(atof(pipulse_str[k+2].c_str()));
          oscil_vec[i]->pipulse.amp.push_back(0.0);
        }
      }
      k+=4;
    }
  }

  /* --- Initialize the Master Equation  --- */
  std::vector<double> xi, t_collapse;
  config.GetVecDoubleParam("xi", xi, 2.0);
  config.GetVecDoubleParam("lindblad_collapsetime", t_collapse, 0.0);
  std::string lindblad = config.GetStrParam("lindblad_type", "none");
  LindbladType lindbladtype;
  if      (lindblad.compare("none")      == 0 ) lindbladtype = NONE;
  else if (lindblad.compare("decay")     == 0 ) lindbladtype = DECAY;
  else if (lindblad.compare("dephase")   == 0 ) lindbladtype = DEPHASE;
  else if (lindblad.compare("both")      == 0 ) lindbladtype = BOTH;
  else {
    printf("\n\n ERROR: Unnown lindblad type: %s.\n", lindblad.c_str());
    printf(" Choose either 'none', 'decay', 'dephase', or 'both'\n");
    exit(1);
  }
  bool usematfree;
  usematfree = config.GetBoolParam("usematfree", false);
  // Sanity check
  if (usematfree && nlevels.size() != 2 ){
    printf("ERROR: Matrix free solver is only implemented for systems with TWO oscillators!\n");
    exit(1);
  }
  if (usematfree && mpisize_petsc > 1) {
    printf("ERROR: No Petsc-parallel version for the matrix free solver available!");
    exit(1);
  }
  MasterEq* mastereq = new MasterEq(nlevels, oscil_vec, xi, detuning_freq, lindbladtype, t_collapse, usematfree);


  /* Output */
  Output* output = new Output(config, mpirank_petsc, mpirank_init, mpirank_braid);

  // Some screen output 
  if (mpirank_world == 0) {
    std::cout << "Time: [0:" << total_time << "], ";
    std::cout << "N="<< ntime << ", dt=" << dt << std::endl;
    std::cout<< "System: ";
    for (int i=0; i<nlevels.size(); i++) {
      std::cout<< nlevels[i];
      if (i < nlevels.size()-1) std::cout<< "x";
    }
    std::cout << "\nT1/T2 times: ";
    for (int i=0; i<t_collapse.size(); i++) {
      std::cout << t_collapse[i];
      if ((i+1)%2 == 0 && i < t_collapse.size()-1) std::cout<< ",";
      std::cout<< " ";
    }
    std::cout << std::endl;
  }

  /* --- Initialize the time-stepper --- */
  LinearSolverType linsolvetype;
  std::string linsolvestr = config.GetStrParam("linearsolver_type", "gmres");
  int linsolve_maxiter = config.GetIntParam("linearsolver_maxiter", 10);
  if      (linsolvestr.compare("gmres")   == 0) linsolvetype = GMRES;
  else if (linsolvestr.compare("neumann") == 0) linsolvetype = NEUMANN;
  else {
    printf("\n\n ERROR: Unknown linear solver type: %s.\n\n", linsolvestr.c_str());
    exit(1);
  }
  /* My time stepper */
  bool storeFWD = false;
  if (runtype == adjoint || runtype == optimization) storeFWD = true;
  TimeStepper *mytimestepper = new ImplMidpoint(mastereq, ntime, total_time, linsolvetype, linsolve_maxiter, output, storeFWD);
  // TimeStepper *mytimestepper = new ExplEuler(mastereq, ntime, total_time, output, storeFWD);

  /* Petsc's Time-stepper */
  TS ts;
  Vec x;
  TSCreate(PETSC_COMM_WORLD,&ts);CHKERRQ(ierr);
  MatCreateVecs(mastereq->getRHS(), &x, NULL);
  TSInit(ts, mastereq, ntime, dt, total_time, x, monitor);
   

#ifdef WITH_BRAID
  /* --- Create braid instances --- */
  myBraidApp* primalbraidapp = NULL;
  myAdjointBraidApp *adjointbraidapp = NULL;
  // Create primal app always, adjoint only if runtype is adjoint or optimization 
  primalbraidapp = new myBraidApp(comm_braid, total_time, ntime, ts, mytimestepper, mastereq, &config, output);
  if (runtype == adjoint || runtype == optimization) adjointbraidapp = new myAdjointBraidApp(comm_braid, total_time, ntime, ts, mytimestepper, mastereq, &config, primalbraidapp->getCore(), output);
  // Initialize the braid time-grids. Warning: initGrids for primal app depends on initialization of adjoint! Do not move this line up!
  primalbraidapp->InitGrids();
  if (runtype == adjoint || runtype == optimization) adjointbraidapp->InitGrids();
  
#endif

  /* --- Initialize optimization --- */
#ifdef WITH_BRAID
  OptimProblem* optimctx = new OptimProblem(config, mytimestepper, primalbraidapp, adjointbraidapp, comm_hiop, comm_init, ninit, output);
#else 
  OptimProblem* optimctx = new OptimProblem(config, mytimestepper, comm_hiop, comm_init, ninit, output);
#endif

  /* Set upt solution and gradient vector */
  Vec xinit;
  VecCreateSeq(PETSC_COMM_SELF, optimctx->ndesign, &xinit);
  VecSetFromOptions(xinit);
  Vec grad;
  VecCreateSeq(PETSC_COMM_SELF, optimctx->ndesign, &grad);
  VecSetUp(grad);
  VecZeroEntries(grad);
  Vec opt;

  /* Some output */
  if (mpirank_world == 0)
  {
    /* Print parameters to file */
    sprintf(filename, "%s/config_log.dat", output->datadir.c_str());
    ofstream logfile(filename);
    if (logfile.is_open()){
      logfile << log.str();
      logfile.close();
      printf("File written: %s\n", filename);
    }
    else std::cerr << "Unable to open " << filename;
  }

  /* Start timer */
  double StartTime = MPI_Wtime();

  double objective;
  /* --- Solve primal --- */
  if (runtype == primal) {
    optimctx->getStartingPoint(xinit);
    if (mpirank_world == 0) printf("\nStarting primal solver... \n");
    objective = optimctx->evalF(xinit);
    if (mpirank_world == 0) printf("\nTotal objective = %1.14e, \n", objective);
    optimctx->getSolution(&opt);
    optimctx->output->writeOptimFile(optimctx->objective, 0.0, 0.0, optimctx->obj_cost, optimctx->obj_regul, optimctx->obj_penal);
  } 
  
  /* --- Solve adjoint --- */
  if (runtype == adjoint) {
    double gnorm = 0.0;
    optimctx->getStartingPoint(xinit);
    if (mpirank_world == 0) printf("\nStarting adjoint solver...\n");
    optimctx->evalGradF(xinit, grad);
    VecNorm(grad, NORM_2, &gnorm);
    // VecView(grad, PETSC_VIEWER_STDOUT_WORLD);
    if (mpirank_world == 0) {
      printf("\nGradient norm: %1.14e\n", gnorm);
    }
    optimctx->output->writeOptimFile(optimctx->objective, optimctx->gnorm, 0.0, optimctx->obj_cost, optimctx->obj_regul, optimctx->obj_penal);
  }

  /* --- Solve the optimization  --- */
  if (runtype == optimization) {
    /* Set initial starting point */
    optimctx->getStartingPoint(xinit);
    if (mpirank_world == 0) printf("\nStarting Optimization solver ... \n");
    optimctx->solve(xinit);
    optimctx->getSolution(&opt);
  }


  /* --- Finalize --- */

  /* Get timings */
  double UsedTime = MPI_Wtime() - StartTime;
  /* Get memory usage */
  struct rusage r_usage;
  getrusage(RUSAGE_SELF, &r_usage);
  double myMB = (double)r_usage.ru_maxrss / 1024.0;
  double globalMB;
  MPI_Allreduce(&myMB, &globalMB, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  /* Print statistics */
  if (mpirank_world == 0) {
    printf("\n");
    printf(" Used Time:        %.2f seconds\n", UsedTime);
    printf(" Global Memory:    %.2f MB\n", globalMB);
    printf(" Processors used:  %d\n", mpisize_world);
    printf("\n");
  }
  // printf("Rank %d: %.2fMB\n", mpirank_world, myMB );

  /* Print timing to file */
  if (mpirank_world == 0) {
    sprintf(filename, "%s/timing.dat", output->datadir.c_str());
    FILE* timefile = fopen(filename, "w");
    fprintf(timefile, "%d  %1.8e\n", mpisize_world, UsedTime);
    fclose(timefile);
  }


#if TEST_FD
  if (mpirank_world == 0)  {
    printf("\n\n#########################\n");
    printf(" FD Testing... \n");
    printf("#########################\n\n");
  }

  double obj_org;
  double obj_pert1, obj_pert2;

  optimctx->getStartingPoint(xinit);

  /* --- Solve primal --- */
  if (mpirank_world == 0) printf("\nRunning optimizer eval_f... ");
  obj_org = optimctx->evalF(xinit);
  if (mpirank_world == 0) printf(" Obj_orig %1.14e\n", obj_org);

  /* --- Solve adjoint --- */
  if (mpirank_world == 0) printf("\nRunning optimizer eval_grad_f...\n");
  optimctx->evalGradF(xinit, grad);
  VecView(grad, PETSC_VIEWER_STDOUT_WORLD);
  

  /* --- Finite Differences --- */
  if (mpirank_world == 0) printf("\nFD...\n");
  for (int i=0; i<optimctx->ndesign; i++){
  // {int i=0;

    /* Evaluate f(p+eps)*/
    VecSetValue(xinit, i, EPS, ADD_VALUES);
    obj_pert1 = optimctx->evalF(xinit);

    /* Evaluate f(p-eps)*/
    VecSetValue(xinit, i, -2*EPS, ADD_VALUES);
    obj_pert2 = optimctx->evalF(xinit);

    /* Eval FD and error */
    double fd = (obj_pert1 - obj_pert2) / (2.*EPS);
    double err = 0.0;
    double gradi; 
    VecGetValues(grad, 1, &i, &gradi);
    if (fd != 0.0) err = (gradi - fd) / fd;
    if (mpirank_world == 0) printf(" %d: obj %1.14e, obj_pert1 %1.14e, obj_pert2 %1.14e, fd %1.14e, grad %1.14e, err %1.14e\n", i, obj_org, obj_pert1, obj_pert2, fd, gradi, err);

    /* Restore parameter */
    VecSetValue(xinit, i, EPS, ADD_VALUES);
  }
  
#endif


#ifdef SANITY_CHECK
  printf("\n\n Sanity checks have been performed. Check output for warnings and errors!\n\n");
#endif

  /* Clean up */
  for (int i=0; i<nlevels.size(); i++){
    delete oscil_vec[i];
  }
  delete [] oscil_vec;
  delete mastereq;
  delete mytimestepper;
#ifdef WITH_BRAID
  delete primalbraidapp;
  if (runtype == primal || runtype == adjoint) delete adjointbraidapp;
#endif
  delete optimctx;
  delete output;

  // TSDestroy(&ts);  /* TODO */

  /* Finallize Petsc */
  ierr = PetscFinalize();

  MPI_Finalize();
  return ierr;
}


