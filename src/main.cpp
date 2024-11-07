#include "timestepper.hpp"
#include "defs.hpp"
#include <string>
#include "oscillator.hpp" 
#include "mastereq.hpp"
#include "config.hpp"
#include <stdlib.h>
#include <sys/resource.h>
#include <cassert>
#include "optimproblem.hpp"
#include "output.hpp"
#include "petsc.h"
#include <random>
#ifdef WITH_SLEPC
#include <slepceps.h>
#endif

#define TEST_FD_GRAD 0    // Run Finite Differences gradient test
#define TEST_FD_HESS 0    // Run Finite Differences Hessian test
#define HESSIAN_DECOMPOSITION 0 // Run eigenvalue analysis for Hessian
#define EPS 1e-7          // Epsilon for Finite Differences

int main(int argc,char **argv)
{
  char filename[255];
  PetscErrorCode ierr;

  /* Initialize MPI */
  int mpisize_world, mpirank_world;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
  MPI_Comm_size(MPI_COMM_WORLD, &mpisize_world);

  /* Parse argument line for "--quiet" to enable reduced output mode */
  bool quietmode = false;
  if (argc > 2){
    for (int i=2; i<argc; i++) {
      std::string quietstring = argv[i];
      if (quietstring.substr(2,5).compare("quiet") == 0) {
        quietmode = true;
        // printf("quietmode =  %d\n", quietmode);
      }
    }
  }

  if (mpirank_world == 0 && !quietmode) printf("Running on %d cores.\n", mpisize_world);

  /* Read config file */
  if (argc < 2) {
    if (mpirank_world == 0) {
      printf("\nUSAGE: ./main </path/to/configfile> \n");
    }
    MPI_Finalize();
    return 0;
  }
  std::stringstream log;
  MapParam config(MPI_COMM_WORLD, log, quietmode);
  config.ReadFile(argv[1]);

  /* Initialize random number generator: Check if rand_seed is provided from config file, otherwise set random. */
  int rand_seed = config.GetIntParam("rand_seed", -1, false, false);
  std::random_device rd;
  if (rand_seed < 0){
    rand_seed = rd();  // random non-reproducable seed
  }
  MPI_Bcast(&rand_seed, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD); // Broadcast from rank 0 to all.
  std::default_random_engine rand_engine{};
  rand_engine.seed(rand_seed);
  export_param(mpirank_world, *config.log, "rand_seed", rand_seed);

  /* --- Get some options from the config file --- */
  std::vector<int> nlevels;
  config.GetVecIntParam("nlevels", nlevels, 0);
  int ntime = config.GetIntParam("ntime", 1000);
  double dt    = config.GetDoubleParam("dt", 0.01);
  RunType runtype;
  std::string runtypestr = config.GetStrParam("runtype", "simulation");
  if      (runtypestr.find("simulation") != std::string::npos ) runtype = RunType::SIMULATION;
  else if (runtypestr.find("gradient")   != std::string::npos) runtype = RunType::GRADIENT;
  else if (runtypestr.find("optimization") != std::string::npos) runtype = RunType::OPTIMIZATION;
  else if (runtypestr.find("evalcontrols") != std::string::npos) runtype = RunType::EVALCONTROLS;
  else {
    printf("\n\n WARNING: Unknown runtype: %s.\n\n", runtypestr.c_str());
    runtype = RunType::NONE;
  }

  /* Get the number of essential levels per oscillator. 
   * Default: same as number of levels */  
  std::vector<int> nessential(nlevels.size());
  for (int iosc = 0; iosc<nlevels.size(); iosc++) nessential[iosc] = nlevels[iosc];
  /* Overwrite if config option is given */
  std::vector<int> read_nessential;
  config.GetVecIntParam("nessential", read_nessential, -1);
  if (read_nessential[0] > -1) {
    for (int iosc = 0; iosc<nlevels.size(); iosc++){
      if (iosc < read_nessential.size()) nessential[iosc] = read_nessential[iosc];
      else                               nessential[iosc] = read_nessential[read_nessential.size()-1];
      if (nessential[iosc] > nlevels[iosc]) nessential[iosc] = nlevels[iosc];
    }
  }


  /* Get type and the total number of initial conditions */
  int ninit = 1;
  std::vector<std::string> initcondstr;
  config.GetVecStrParam("initialcondition", initcondstr, "none");
  assert (initcondstr.size() >= 1);
  if      (initcondstr[0].compare("file") == 0 ) ninit = 1;
  else if (initcondstr[0].compare("pure") == 0 ) ninit = 1;
  else if (initcondstr[0].compare("performance") == 0 ) ninit = 1;
  else if (initcondstr[0].compare("ensemble") == 0 ) ninit = 1;
  else if (initcondstr[0].compare("3states") == 0 ) ninit = 3;
  else if (initcondstr[0].compare("Nplus1") == 0 )  {
    // compute system dimension N 
    ninit = 1;
    for (int i=0; i<nlevels.size(); i++){
      ninit *= nlevels[i];
    }
    ninit +=1;
  }
  else if ( initcondstr[0].compare("diagonal") == 0 ||
            initcondstr[0].compare("basis")    == 0  ) {
    /* Compute ninit = dim(subsystem defined by list of oscil IDs) */
    ninit = 1;
    if (initcondstr.size() < 2) {
      for (int j=0; j<nlevels.size(); j++)  initcondstr.push_back(std::to_string(j));
    }
    for (int i = 1; i<initcondstr.size(); i++){
      int oscilID = atoi(initcondstr[i].c_str());
      if (oscilID < nessential.size()) ninit *= nessential[oscilID];
    }
    if (initcondstr[0].compare("basis") == 0  ) {
      // if Schroedinger solver: ninit = N, do nothing.
      // else Lindblad solver: ninit = N^2
      std::string tmpstr = config.GetStrParam("collapse_type", "none", false);
      if (tmpstr.compare("none") != 0 ) ninit = (int) pow(ninit,2.0);
    }
  }
  else {
    printf("\n\n ERROR: Wrong setting for initial condition.\n");
    exit(1);
  }
  if (mpirank_world == 0 && !quietmode) {
    printf("Number of initial conditions: %d\n", ninit);
  }

  /* --- Split communicators for distributed initial conditions, distributed linear algebra, parallel optimization --- */
  int mpirank_init, mpisize_init;
  int mpirank_optim, mpisize_optim;
  int mpirank_petsc, mpisize_petsc;
  MPI_Comm comm_optim, comm_init, comm_petsc;

  /* Get the size of communicators  */
  // Number of cores for optimization. Under development, set to 1 for now. 
  // int np_optim= config.GetIntParam("np_optim", 1);
  // np_optim= min(np_optim, mpisize_world); 
  // int np_optim= 1;
  // Number of pulses for Petsc: hardcode 1
  int np_petsc = 1;
  // Number of cores for initial condition distribution. Since this gives perfect speedup, choose maximum.
  int np_init = std::min(ninit, mpisize_world); 
  // Number of cores for different pulses: All the remaining ones. 
  int np_optim = mpisize_world / (np_init * np_petsc);

  /* Sanity check for communicator sizes */ 
  if (mpisize_world % ninit != 0 && ninit % mpisize_world != 0) {
    if (mpirank_world == 0) printf("ERROR: Number of threads (%d) must be integer multiplier or divisor of the number of initial conditions (%d)!\n", mpisize_world, ninit);
    exit(1);
  }

  /* Split communicators */
  // Distributed initial conditions 
  int color_init = mpirank_world % (np_petsc * np_optim);
  MPI_Comm_split(MPI_COMM_WORLD, color_init, mpirank_world, &comm_init);
  MPI_Comm_rank(comm_init, &mpirank_init);
  MPI_Comm_size(comm_init, &mpisize_init);

  // Time-parallel Optimization
  int color_optim = mpirank_world % np_petsc + mpirank_init * np_petsc;
  MPI_Comm_split(MPI_COMM_WORLD, color_optim, mpirank_world, &comm_optim);
  MPI_Comm_rank(comm_optim, &mpirank_optim);
  MPI_Comm_size(comm_optim, &mpisize_optim);

  // Distributed Linear algebra: Petsc
  int color_petsc = mpirank_world / np_petsc;
  MPI_Comm_split(MPI_COMM_WORLD, color_petsc, mpirank_world, &comm_petsc);
  MPI_Comm_rank(comm_petsc, &mpirank_petsc);
  MPI_Comm_size(comm_petsc, &mpisize_petsc);

  /* Set Petsc using petsc's communicator */
  PETSC_COMM_WORLD = comm_petsc;

  if (mpirank_world == 0 && !quietmode)  std::cout<< "Parallel distribution: " << mpisize_init << " np_init  X  " << mpisize_petsc<< " np_petsc  X  " << mpisize_optim << " np_optim" << std::endl;

#ifdef WITH_SLEPC
  ierr = SlepcInitialize(&argc, &argv, (char*)0, NULL);if (ierr) return ierr;
#else
  ierr = PetscInitialize(&argc,&argv,(char*)0,NULL);if (ierr) return ierr;
#endif
  PetscViewerPushFormat(PETSC_VIEWER_STDOUT_WORLD, 	PETSC_VIEWER_ASCII_MATLAB );


  /* Create output class */
  Output* output = new Output(config, comm_petsc, comm_init, nlevels.size(), quietmode);

  /* --- Initialize the Oscillators --- */
  Oscillator** oscil_vec = new Oscillator*[nlevels.size()];
  // Get fundamental and rotation frequencies from config file 
  std::vector<double> trans_freq, rot_freq;
  config.GetVecDoubleParam("transfreq", trans_freq, 1e20);
  copyLast(trans_freq, nlevels.size());

  config.GetVecDoubleParam("rotfreq", rot_freq, 1e20);
  copyLast(rot_freq, nlevels.size());
  // Get self kerr coefficient
  std::vector<double> selfkerr;
  config.GetVecDoubleParam("selfkerr", selfkerr, 0.0);   // self ker \xi_k 
  copyLast(selfkerr, nlevels.size());
  // Get lindblad type and collapse times
  std::string lindblad = config.GetStrParam("collapse_type", "none");
  std::vector<double> decay_time, dephase_time;
  config.GetVecDoubleParam("decay_time", decay_time, 0.0, true, false);
  config.GetVecDoubleParam("dephase_time", dephase_time, 0.0, true, false);
  LindbladType lindbladtype;
  if      (lindblad.compare("none")      == 0 ) lindbladtype = LindbladType::NONE;
  else if (lindblad.compare("decay")     == 0 ) lindbladtype = LindbladType::DECAY;
  else if (lindblad.compare("dephase")   == 0 ) lindbladtype = LindbladType::DEPHASE;
  else if (lindblad.compare("both")      == 0 ) lindbladtype = LindbladType::BOTH;
  else {
    printf("\n\n ERROR: Unnown lindblad type: %s.\n", lindblad.c_str());
    printf(" Choose either 'none', 'decay', 'dephase', or 'both'\n");
    exit(1);
  }
  copyLast(decay_time, nlevels.size());
  copyLast(dephase_time, nlevels.size());

  /* Create Learning Model, or a dummy if not using UDEs */
  std::string UDEmodel_str = config.GetStrParam("UDEmodel", "none", true, false);
  UDEmodelType UDEmodel; 
  if      (UDEmodel_str.compare("none")        == 0 ) UDEmodel = UDEmodelType::NONE;
  else if (UDEmodel_str.compare("hamiltonian") == 0 ) UDEmodel = UDEmodelType::HAMILTONIAN;
  else if (UDEmodel_str.compare("lindblad")    == 0 ) UDEmodel = UDEmodelType::LINDBLAD;
  else if (UDEmodel_str.compare("both")        == 0 ) UDEmodel = UDEmodelType::BOTH;
  else {
    printf("\n\n ERROR: Unnown UDE model type: %s.\n", UDEmodel_str.c_str());
    printf(" Choose either 'none', 'hamiltonian', 'lindblad', or 'both'\n");
    exit(1);
  }

  /* Switch solver mode between learning/simulation a UDE model parameters vs optimizing/simulating controls. */
  bool x_is_control;
  if (runtypestr.find("UDE") != std::string::npos && UDEmodel != UDEmodelType::NONE) {
    x_is_control = false ; // optim wrt UDE model parameters
  } else {
    x_is_control = true; // optim wrt controls parameters
  }
  output->x_is_control = x_is_control;

  /* Create learning and data class*/
  Learning* learning;
  if (UDEmodel != UDEmodelType::NONE) {

    /* Load trajectory data only if operating on the Loss (if not x_is_control) */
    Data* data;
    if (!x_is_control){
      std::vector<std::string> data_name;
      config.GetVecStrParam("data_name", data_name, "data");
      std::string identifyer = data_name[0];
      data_name.erase(data_name.begin());
      if (identifyer.compare("synthetic") == 0) { 
        data = new SyntheticQuandaryData(config, comm_optim, data_name, nlevels, lindbladtype);
      } else if (identifyer.compare("Tant2level") == 0) { 
        data = new Tant2levelData(config, comm_optim, data_name, nlevels, lindbladtype);
      } else if (identifyer.compare("Tant3level") == 0) {
        data = new Tant3levelData(config, comm_optim, data_name, nlevels, lindbladtype);
      }
      else {
        printf("Wrong setting for loading data. Needs prefix 'synthetic', or 'Tant2level', or 'Tant3level'.\n");
        exit(1);
      }

      /* Update the time-integration step-size such that it is an integer divisor of the data sampling size  */
      double dt_tmp = dt;
      dt = data->suggestTimeStepSize(dt_tmp);
      if (abs(dt - dt_tmp) > 1e-8 && !quietmode) printf(" -> Updated dt from %1.14e to %1.14e\n", dt_tmp, dt);
    } else {
      data = new Data(); // Dummy
    }

    /* Now create learning object */
    std::vector<std::string> learninit_str;
    config.GetVecStrParam("learnparams_initialization", learninit_str, "random, 0.0");
    learning = new Learning(nlevels, lindbladtype, UDEmodel, learninit_str, data, rand_engine, quietmode);

  } else {
    /* Create dummy learning. Does nothing. */
    Data* data = new Data();
    std::vector<std::string> dummy_learninit_str;
    std::vector<int> dummy_nlevels(0);
    learning = new Learning(dummy_nlevels, LindbladType::NONE, UDEmodelType::NONE, dummy_learninit_str, data, rand_engine, quietmode); 
  }

  /* Set total time */
  double total_time = ntime * dt;

  /* Initialize oscillators, with control paramterization and carrierwaves */
  // Set defaults 
  std::string default_seg_str = "spline, 10, 0.0, "+std::to_string(total_time); 
  std::string default_init_str = "constant, 0.0";
  // Iterate over oscillators
  for (int i = 0; i < nlevels.size(); i++){
    // Get carrier wave frequencies 
    std::vector<double> carrier_freq;
    std::string key = "carrier_frequency" + std::to_string(i);
    config.GetVecDoubleParam(key, carrier_freq, 0.0);
    // Get control type and initialization string
    std::vector<std::string> controltype_str;
    config.GetVecStrParam("control_segments" + std::to_string(i), controltype_str,default_seg_str);
    std::vector<std::string> controlinit_str;
    config.GetVecStrParam("control_initialization" + std::to_string(i), controlinit_str, default_init_str);

    // Create oscillator 
    oscil_vec[i] = new Oscillator(config, i, nlevels, controltype_str, controlinit_str, trans_freq[i], selfkerr[i], rot_freq[i], decay_time[i], dephase_time[i], carrier_freq, total_time, lindbladtype, rand_engine);
 
    // Update the default for control type for next iter
    default_seg_str = "";
    default_init_str = "";
    for (int l = 0; l<controltype_str.size(); l++) default_seg_str += controltype_str[l]+=", ";
    for (int l = 0; l<controlinit_str.size(); l++) default_init_str += controlinit_str[l]+=", ";
  }

  // If control initialization is read from file, pass it to oscillators
  std::vector<std::string> controlinit_str;
  config.GetVecStrParam("control_initialization0", controlinit_str, "constant, 0.0", false, false);
  if (controlinit_str.size() > 0 && controlinit_str[0].compare("file") == 0 ) {
    assert(controlinit_str.size() >=2);
    // Read file 
    int nparams = 0;
    for (int iosc = 0; iosc < nlevels.size(); iosc++){
      nparams += oscil_vec[iosc]->getNParams();
    }
    std::vector<double> initguess_fromfile(nparams, 0.0);
    if (mpirank_world == 0) read_vector(controlinit_str[1].c_str(), initguess_fromfile.data(), nparams, quietmode);
    MPI_Bcast(initguess_fromfile.data(), nparams, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    // Pass control initialization to oscillators
    int shift=0;
    for (int ioscil = 0; ioscil < nlevels.size(); ioscil++) {
      /* Copy x into the oscillators parameter array. */
      oscil_vec[ioscil]->setParams(initguess_fromfile.data() + shift);
      shift += oscil_vec[ioscil]->getNParams();
    }
  }

  // Get pi-pulses, if any, and pass it to the oscillators
  std::vector<std::string> pipulse_str;
  config.GetVecStrParam("apply_pipulse", pipulse_str, "none", true, false);
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
  // Get self and cross kers and coupling terms 
  std::vector<double> crosskerr, Jkl;
  config.GetVecDoubleParam("crosskerr", crosskerr, 0.0);   // cross ker \xi_{kl}, zz-coupling
  config.GetVecDoubleParam("Jkl", Jkl, 0.0); // Jaynes-Cummings (dipole-dipole) coupling coeff
  copyLast(crosskerr, (nlevels.size()-1) * nlevels.size()/ 2);
  copyLast(Jkl, (nlevels.size()-1) * nlevels.size()/ 2);
  // If not enough elements are given, fill up with zeros!
  // for (int i = crosskerr.size(); i < (noscillators-1) * noscillators / 2; i++)  crosskerr.push_back(0.0);
  // for (int i = Jkl.size(); i < (noscillators-1) * noscillators / 2; i++) Jkl.push_back(0.0);
  // Sanity check for matrix free solver
  bool usematfree = config.GetBoolParam("usematfree", false);
  if (usematfree && UDEmodel != UDEmodelType::NONE) {
    printf("\n\n WARNING: using matfree solver together with UDE model needs testing!! Not recommended at this point.\n\n");
  }
  if (usematfree && nlevels.size() > 5){
        printf("Warning: Matrix free solver is only implemented for systems with 2, 3, 4, or 5 oscillators. Switching to sparse-matrix solver now.\n");
        usematfree = false;
  }
  if (usematfree && mpisize_petsc > 1) {
    if (mpirank_world == 0) printf("ERROR: No Petsc-parallel version for the matrix free solver available!");
    exit(1);
  }
  // Compute coupling rotation frequencies eta_ij = w^r_i - w^r_j
  std::vector<double> eta(nlevels.size()*(nlevels.size()-1)/2.);
  int idx = 0;
  for (int iosc=0; iosc<nlevels.size(); iosc++){
    for (int josc=iosc+1; josc<nlevels.size(); josc++){
      eta[idx] = rot_freq[iosc] - rot_freq[josc];
      idx++;
    }
  }
  // Check if Hamiltonian should be read from file
  std::string hamiltonian_file = config.GetStrParam("hamiltonian_file", "none", true, false);
  if (hamiltonian_file.compare("none") != 0 && usematfree) {
    if (mpirank_world==0 && !quietmode) printf("# Warning: Matrix-free solver can not be used when Hamiltonian is read fromfile. Switching to sparse-matrix version.\n");
    usematfree = false;
  }
  // Initialize Master equation
  MasterEq* mastereq = new MasterEq(nlevels, nessential, oscil_vec, crosskerr, Jkl, eta, lindbladtype, usematfree, UDEmodel, x_is_control, learning, hamiltonian_file, quietmode);

  // Some screen output 
  if (mpirank_world == 0 && !quietmode) {
    std::cout<< "System: ";
    for (int i=0; i<nlevels.size(); i++) {
      std::cout<< nlevels[i];
      if (i < nlevels.size()-1) std::cout<< "x";
    }
    std::cout<<"  (essential levels: ";
    for (int i=0; i<nlevels.size(); i++) {
      std::cout<< nessential[i];
      if (i < nlevels.size()-1) std::cout<< "x";
    }
    std::cout << ") " << std::endl;

    std::cout<<"State dimension (complex): " << mastereq->getDim() << std::endl;
    std::cout << "Simulation time: [0:" << total_time << "], ";
    std::cout << "N="<< ntime << ", dt=" << dt << std::endl;
  }

  /* --- Initialize the time-stepper --- */
  LinearSolverType linsolvetype;
  std::string linsolvestr = config.GetStrParam("linearsolver_type", "gmres");
  int linsolve_maxiter = config.GetIntParam("linearsolver_maxiter", 10);
  if      (linsolvestr.compare("gmres")   == 0) linsolvetype = LinearSolverType::GMRES;
  else if (linsolvestr.compare("neumann") == 0) linsolvetype = LinearSolverType::NEUMANN;
  else {
    printf("\n\n ERROR: Unknown linear solver type: %s.\n\n", linsolvestr.c_str());
    exit(1);
  }

  /* My time stepper */
  int ninit_local = ninit / mpisize_init; 
  // If lindblad solver, store states during forward timestepping (needed for gradients). Otherwise, states will be recomputed during backward timestepping. 
  bool storeFWD;
  if (mastereq->lindbladtype != LindbladType::NONE ) { // Lindblad solver
    storeFWD = true;
  } else { // Schroedinger solver
    storeFWD = false;
  }
  std::string timesteppertypestr = config.GetStrParam("timestepper", "IMR");
  TimeStepper* mytimestepper;
  if (timesteppertypestr.compare("IMR")==0) mytimestepper = new ImplMidpoint(config, mastereq, ntime, total_time, linsolvetype, linsolve_maxiter, output, storeFWD);
  else if (timesteppertypestr.compare("IMR4")==0) mytimestepper = new CompositionalImplMidpoint(config, 4, mastereq, ntime, total_time, linsolvetype, linsolve_maxiter, output, storeFWD);
  else if (timesteppertypestr.compare("IMR8")==0) mytimestepper = new CompositionalImplMidpoint(config, 8, mastereq, ntime, total_time, linsolvetype, linsolve_maxiter, output, storeFWD);
  else if (timesteppertypestr.compare("EE")==0) mytimestepper = new ExplEuler(config, mastereq, ntime, total_time, output, storeFWD);
  else {
    printf("\n\n ERROR: Unknow timestepping type: %s.\n\n", timesteppertypestr.c_str());
    exit(1);
  }

  /* --- Initialize optimization --- */
  OptimProblem* optimctx = new OptimProblem(config, mytimestepper, comm_init, comm_optim, ninit, output, x_is_control, quietmode);

  /* Set upt solution and gradient vector */
  Vec xinit;
  VecCreateSeq(PETSC_COMM_SELF, optimctx->getNdesign(), &xinit);
  VecSetFromOptions(xinit);
  Vec grad;
  VecCreateSeq(PETSC_COMM_SELF, optimctx->getNdesign(), &grad);
  VecSetUp(grad);
  VecZeroEntries(grad);
  Vec opt;

  /* Some output */
  if (mpirank_world == 0)
  {
    /* Print parameters to file */
    snprintf(filename, 254, "%s/config_log.dat", output->datadir.c_str());
    std::ofstream logfile(filename);
    if (logfile.is_open()){
      logfile << log.str();
      logfile.close();
      if (!quietmode) printf("File written: %s\n", filename);
    }
    else std::cerr << "Unable to open " << filename;
  }

  /* Start timer */
  double StartTime = MPI_Wtime();
  double objective;
  double gnorm = 0.0;
  /* --- Solve primal --- */
  if (runtype == RunType::SIMULATION) {
    optimctx->getStartingPoint(xinit);
    VecCopy(xinit, optimctx->xinit); // Store the initial guess
    if (mpirank_world == 0 && !quietmode) printf("\nStarting primal solver... \n");
    objective = optimctx->evalF(xinit);
    if (mpirank_world == 0 && !quietmode) printf("\nTotal objective = %1.14e, \n", objective);
    optimctx->getSolution(&opt);
  } 
  
  /* --- Solve adjoint --- */
  if (runtype == RunType::GRADIENT) {
    optimctx->getStartingPoint(xinit);
    VecCopy(xinit, optimctx->xinit); // Store the initial guess
    if (mpirank_world == 0 && !quietmode) printf("\nStarting adjoint solver...\n");
    optimctx->evalGradF(xinit, grad);
    VecNorm(grad, NORM_2, &gnorm);
    // VecView(grad, PETSC_VIEWER_STDOUT_WORLD);
    if (mpirank_world == 0 && !quietmode) {
      printf("\nGradient norm: %1.14e\n", gnorm);
    }
    optimctx->output->writeGradient(grad);
  }

  /* --- Solve the optimization  --- */
  if (runtype == RunType::OPTIMIZATION) {
    /* Set initial starting point */
    optimctx->getStartingPoint(xinit);
    VecCopy(xinit, optimctx->xinit); // Store the initial guess
    if (mpirank_world == 0 && !quietmode) printf("\nStarting Optimization solver ... \n");
    optimctx->solve(xinit);
    optimctx->getSolution(&opt);
  }

    // /* If learning, print out the learned operators */
    if (!x_is_control){
      learning->viewOperators(output->datadir);
    }
   
  /* Write all control pulses to file */
  for (int ipulse_local = 0; ipulse_local < learning->data->getNPulses_local(); ipulse_local++){
    int ipulse_global = mpirank_optim * learning->data->getNPulses_local() + ipulse_local;
    if (!x_is_control) {
      mastereq->setControlFromData(ipulse_global);
    }
    int pulseID = -1;
    if (!x_is_control)  pulseID = ipulse_global;
    output->writeControls(optimctx->timestepper->mastereq, optimctx->timestepper->ntime, optimctx->timestepper->dt, pulseID);
  }

  /* Only evaluate and write control pulses (no propagation) */
  if (runtype == RunType::EVALCONTROLS) {
    std::vector<double> pt, qt;
    optimctx->getStartingPoint(xinit);
    if (mpirank_world == 0 && !quietmode) printf("\nEvaluating current controls ... \n");
    output->writeParams(xinit);
    output->writeControls(mastereq, ntime, dt, -1);
  }

  /* Output */
  if (runtype != RunType::OPTIMIZATION) optimctx->output->writeOptimFile(optimctx->getObjective(), gnorm, 0.0, optimctx->getFidelity(), optimctx->getCostT(), optimctx->getRegul(), optimctx->getPenalty(), optimctx->getPenaltyDpDm(), optimctx->getPenaltyEnergy());


  /* --- Finalize --- */

  /* Get timings */
  double UsedTime = MPI_Wtime() - StartTime;
  /* Get memory usage */
  struct rusage r_usage;
  getrusage(RUSAGE_SELF, &r_usage);
  double myMB = (double)r_usage.ru_maxrss / 1024.0;
  double globalMB = myMB;
  MPI_Allreduce(&myMB, &globalMB, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  /* Print statistics */
  if (mpirank_world == 0 && !quietmode) {
    printf("\n");
    printf(" Used Time:        %.2f seconds\n", UsedTime);
    printf(" Processors used:  %d\n", mpisize_world);
    printf(" Global Memory:    %.2f MB    [~ %.2f MB per proc]\n", globalMB, globalMB / mpisize_world);
    printf(" [NOTE: The memory unit is platform dependent. If you run on MacOS, the unit will likely be KB instead of MB.]\n");
    printf("\n");
  }
  // printf("Rank %d: %.2fMB\n", mpirank_world, myMB );

  /* Print timing to file */
  if (mpirank_world == 0) {
    snprintf(filename, 254, "%s/timing.dat", output->datadir.c_str());
    FILE* timefile = fopen(filename, "w");
    fprintf(timefile, "%d  %1.8e\n", mpisize_world, UsedTime);
    fclose(timefile);
  }


#if TEST_FD_GRAD
  if (mpirank_world == 0)  {
    printf("\n\n#########################\n");
    printf(" FD Testing for Gradient ... \n");
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
  if (mpirank_world == 0) printf("\nFinite Difference testing...\n");
  double max_err = 0.0;
  for (PetscInt i=0; i<optimctx->getNdesign(); i++){
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
    if (abs(err) > max_err) max_err = err;

    /* Restore parameter */
    VecSetValue(xinit, i, EPS, ADD_VALUES);
  }

  printf("\nMax. Finite Difference error: %1.14e\n\n", max_err);
  
#endif


#if TEST_FD_HESS
  if (mpirank_world == 0)  {
    printf("\n\n#########################\n");
    printf(" FD Testing for Hessian... \n");
    printf("#########################\n\n");
  }
  optimctx->getStartingPoint(xinit);

  /* Figure out which parameters are hitting bounds */
  double bound_tol = 1e-3;
  std::vector<int> Ihess; // Index set for all elements that do NOT hit a bound
  for (PetscInt i=0; i<optimctx->getNdesign(); i++){
    // get x_i and bounds for x_i
    double xi, blower, bupper;
    VecGetValues(xinit, 1, &i, &xi);
    VecGetValues(optimctx->xlower, 1, &i, &blower);
    VecGetValues(optimctx->xupper, 1, &i, &bupper);
    // compare 
    if (fabs(xi - blower) < bound_tol || 
        fabs(xi - bupper) < bound_tol  ) {
          printf("Parameter %d hits bound: x=%f\n", i, xi);
    } else {
      Ihess.push_back(i);
    }
  }

  double grad_org;
  double grad_pert1, grad_pert2;
  Mat Hess;
  int nhess = Ihess.size();
  MatCreateSeqDense(PETSC_COMM_SELF, nhess, nhess, NULL, &Hess);
  MatSetUp(Hess);

  Vec grad1, grad2;
  VecDuplicate(grad, &grad1);
  VecDuplicate(grad, &grad2);


  /* Iterate over all params that do not hit a bound */
  for (PetscInt k=0; k< Ihess.size(); k++){
    PetscInt j = Ihess[k];
    printf("Computing column %d\n", j);

    /* Evaluate \nabla_x J(x + eps * e_j) */
    VecSetValue(xinit, j, EPS, ADD_VALUES); 
    optimctx->evalGradF(xinit, grad);        
    VecCopy(grad, grad1);

    /* Evaluate \nabla_x J(x - eps * e_j) */
    VecSetValue(xinit, j, -2.*EPS, ADD_VALUES); 
    optimctx->evalGradF(xinit, grad);
    VecCopy(grad, grad2);

    for (PetscInt l=0; l<Ihess.size(); l++){
      PetscInt i = Ihess[l];

      /* Get the derivative wrt parameter i */
      VecGetValues(grad1, 1, &i, &grad_pert1);   // \nabla_x_i J(x+eps*e_j)
      VecGetValues(grad2, 1, &i, &grad_pert2);    // \nabla_x_i J(x-eps*e_j)

      /* Finite difference for element Hess(l,k) */
      double fd = (grad_pert1 - grad_pert2) / (2.*EPS);
      MatSetValue(Hess, l, k, fd, INSERT_VALUES);
    }

    /* Restore parameters xinit */
    VecSetValue(xinit, j, EPS, ADD_VALUES);
  }
  /* Assemble the Hessian */
  MatAssemblyBegin(Hess, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Hess, MAT_FINAL_ASSEMBLY);
  
  /* Clean up */
  VecDestroy(&grad1);
  VecDestroy(&grad2);


  /* Epsilon test: compute ||1/2(H-H^T)||_F  */
  MatScale(Hess, 0.5);
  Mat HessT, Htest;
  MatDuplicate(Hess, MAT_COPY_VALUES, &Htest);
  MatTranspose(Hess, MAT_INITIAL_MATRIX, &HessT);
  MatAXPY(Htest, -1.0, HessT, SAME_NONZERO_PATTERN);
  double fnorm;
  MatNorm(Htest, NORM_FROBENIUS, &fnorm);
  printf("EPS-test: ||1/2(H-H^T)||= %1.14e\n", fnorm);

  /* symmetrize H_symm = 1/2(H+H^T) */
  MatAXPY(Hess, 1.0, HessT, SAME_NONZERO_PATTERN);

  /* --- Print Hessian to file */
  
  snprintf(filename, 254, "%s/hessian.dat", output->datadir.c_str());
  printf("File written: %s.\n", filename);
  PetscViewer viewer;
  PetscViewerCreate(MPI_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, filename);
  // PetscViewerPushFormat(viewer, PETSC_VIEWER_ASCII_DENSE);
  MatView(Hess, viewer);
  PetscViewerPopFormat(viewer);
  PetscViewerDestroy(&viewer);

  // write again in binary
  snprintf(filename, 254, "%s/hessian_bin.dat", output->datadir.c_str());
  printf("File written: %s.\n", filename);
  PetscViewerBinaryOpen(MPI_COMM_WORLD, filename, FILE_MODE_WRITE, &viewer);
  MatView(Hess, viewer);
  PetscViewerDestroy(&viewer);

  MatDestroy(&Hess);

#endif

#if HESSIAN_DECOMPOSITION 
  /* --- Compute eigenvalues of Hessian --- */
  printf("\n\n#########################\n");
  printf(" Eigenvalue analysis... \n");
  printf("#########################\n\n");

  /* Load Hessian from file */
  Mat Hess;
  MatCreate(PETSC_COMM_SELF, &Hess);
  snprintf(filename, 254, "%s/hessian_bin.dat", output->datadir.c_str());
  printf("Reading file: %s\n", filename);
  PetscViewer viewer;
  PetscViewerCreate(MPI_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERBINARY);
  PetscViewerFileSetMode(viewer, FILE_MODE_READ);
  PetscViewerFileSetName(viewer, filename);
  PetscViewerPushFormat(viewer, PETSC_VIEWER_ASCII_DENSE);
  MatLoad(Hess, viewer);
  PetscViewerPopFormat(viewer);
  PetscViewerDestroy(&viewer);
  int nrows, ncols;
  MatGetSize(Hess, &nrows, &ncols);


  /* Set the percentage of eigenpairs that should be computed */
  double frac = 1.0;  // 1.0 = 100%
  int neigvals = nrows * frac;     // hopefully rounds to closest int 
  printf("\nComputing %d eigenpairs now...\n", neigvals);
  
  /* Compute eigenpair */
  std::vector<double> eigvals;
  std::vector<Vec> eigvecs;
  getEigvals(Hess, neigvals, eigvals, eigvecs);

  /* Print eigenvalues to file. */
  FILE *file;
  snprintf(filename, 254, "%s/eigvals.dat", output->datadir.c_str());
  file =fopen(filename,"w");
  for (int i=0; i<eigvals.size(); i++){
      fprintf(file, "% 1.8e\n", eigvals[i]);  
  }
  fclose(file);
  printf("File written: %s.\n", filename);

  /* Print eigenvectors to file. Columns wise */
  snprintf(filename, 254, "%s/eigvecs.dat", output->datadir.c_str());
  file =fopen(filename,"w");
  for (PetscInt j=0; j<nrows; j++){  // rows
    for (PetscInt i=0; i<eigvals.size(); i++){
      double val;
      VecGetValues(eigvecs[i], 1, &j, &val); // j-th row of eigenvalue i
      fprintf(file, "% 1.8e  ", val);  
    }
    fprintf(file, "\n");
  }
  fclose(file);
  printf("File written: %s.\n", filename);


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
  delete learning;
  delete optimctx;
  delete mytimestepper;
  delete output;

  VecDestroy(&xinit);
  VecDestroy(&grad);


  /* Finallize Petsc */
#ifdef WITH_SLEPC
  ierr = SlepcFinalize();
#else
  PetscOptionsSetValue(NULL, "-options_left", "no"); // Remove warning about unused options.
  ierr = PetscFinalize();
#endif


  MPI_Finalize();
  return ierr;
}
