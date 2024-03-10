#include "optimtarget.hpp"

OptimTarget::OptimTarget(){
  dim = 0;
  dim_rho = 0;
  dim_ess = 0;
  noscillators = 0;
  target_type = TargetType::GATE;
  objective_type = ObjectiveType::JTRACE;
  initcond_type = InitialConditionType::BASIS;
  lindbladtype = LindbladType::NONE;
  targetgate = NULL;
  purity_rho0 = 1.0;
  purestateID = -1;
  target_filename = "";
  targetstate = NULL;
}


OptimTarget::OptimTarget(std::vector<std::string> target_str, std::string objective_str, std::vector<std::string> initcond_str, MasterEq* mastereq, double total_time, std::vector<double> read_gate_rot, Vec rho_t0, bool quietmode_) : OptimTarget() {

  // initialize
  dim = mastereq->getDim();
  dim_rho = mastereq->getDimRho();
  dim_ess = mastereq->getDimEss();
  quietmode = quietmode_;
  lindbladtype = mastereq->lindbladtype;

  // Get global communicator rank
  int mpirank_world;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);

  /* Get initial condition type */
  if (initcond_str[0].compare("file") == 0)              initcond_type = InitialConditionType::FROMFILE;
  else if  (initcond_str[0].compare("pure") == 0)        initcond_type = InitialConditionType::PURE;
  else if  (initcond_str[0].compare("ensemble") == 0)    initcond_type = InitialConditionType::ENSEMBLE;
  else if  (initcond_str[0].compare("performance") == 0) initcond_type = InitialConditionType::PERFORMANCE;
  else if  (initcond_str[0].compare("3states") == 0)     initcond_type = InitialConditionType::THREESTATES;
  else if  (initcond_str[0].compare("Nplus1") == 0)      initcond_type = InitialConditionType::NPLUSONE;
  else if  (initcond_str[0].compare("diagonal") == 0)    initcond_type = InitialConditionType::DIAGONAL;
  else if  (initcond_str[0].compare("basis") == 0)       initcond_type = InitialConditionType::BASIS;
  else {
    printf("ERROR: Unknown initial condition type %s!\n", initcond_str[0].c_str());
    exit(1);
  }
  /* Sanity check for Schrodinger solver initial conditions */
  if (lindbladtype == LindbladType::NONE){
    if (initcond_type == InitialConditionType::ENSEMBLE ||
        initcond_type == InitialConditionType::THREESTATES ||
        initcond_type == InitialConditionType::NPLUSONE ){
          printf("\n\n ERROR for initial condition setting: \n When running Schroedingers solver (collapse_type == NONE), the initial condition needs to be either 'pure' or 'from file' or 'diagonal' or 'basis'. Note that 'diagonal' and 'basis' in the Schroedinger case are the same (all unit vectors).\n\n");
          exit(1);
    } else if (initcond_type == InitialConditionType::BASIS) {
      // DIAGONAL and BASIS initial conditions in the Schroedinger case are the same. Overwrite it to DIAGONAL
      initcond_type = InitialConditionType::DIAGONAL;  
    }
  }
  /* Get list of involved oscillators */
  if (initcond_str.size() < 2) 
    for (int j=0; j<mastereq->getNOscillators(); j++)   
      initcond_str.push_back(std::to_string(j)); // Default: all oscillators
  for (int i=1; i<initcond_str.size(); i++) 
    initcond_IDs.push_back(atoi(initcond_str[i].c_str())); // Overwrite with config option, if given.

  /* Prepare initial state rho_t0 if PURE or FROMFILE or ENSEMBLE initialization. Otherwise they are set within prepareInitialState during evalF. */
  PetscInt ilow, iupp;
  VecGetOwnershipRange(rho_t0, &ilow, &iupp);
  if (initcond_type == InitialConditionType::PURE) { 
    /* Initialize with tensor product of unit vectors. */
    if (initcond_IDs.size() != mastereq->getNOscillators()) {
      printf("ERROR during pure-state initialization: List of IDs must contain %d elements!\n", mastereq->getNOscillators());
      exit(1);
    }
    int diag_id = 0;
    for (int k=0; k < initcond_IDs.size(); k++) {
      if (initcond_IDs[k] > mastereq->getOscillator(k)->getNLevels()-1){
        printf("ERROR in config setting. The requested pure state initialization |%d> exceeds the number of allowed levels for that oscillator (%d).\n", initcond_IDs[k], mastereq->getOscillator(k)->getNLevels());
        exit(1);
      }
      assert (initcond_IDs[k] < mastereq->getOscillator(k)->getNLevels());
      int dim_postkron = 1;
      for (int m=k+1; m < initcond_IDs.size(); m++) {
        dim_postkron *= mastereq->getOscillator(m)->getNLevels();
      }
      diag_id += initcond_IDs[k] * dim_postkron;
    }
    int ndim = mastereq->getDimRho();
    int vec_id = -1;
    if (lindbladtype != LindbladType::NONE) vec_id = getIndexReal(getVecID( diag_id, diag_id, ndim )); // Real part of x
    else vec_id = getIndexReal(diag_id);
    if (ilow <= vec_id && vec_id < iupp) VecSetValue(rho_t0, vec_id, 1.0, INSERT_VALUES);
  }
  else if (initcond_type == InitialConditionType::FROMFILE) { 
    /* Read initial condition from file */
    int nelems = 0;
    if (mastereq->lindbladtype != LindbladType::NONE) nelems = 2*dim_ess*dim_ess;
    else nelems = 2 * dim_ess;
    double * vec = new double[nelems];
    if (mpirank_world == 0) {
      assert (initcond_str.size()==2);
      std::string filename = initcond_str[1];
      read_vector(filename.c_str(), vec, nelems, quietmode);
    }
    MPI_Bcast(vec, nelems, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (lindbladtype != LindbladType::NONE) { // Lindblad solver, fill density matrix
      for (int i = 0; i < dim_ess*dim_ess; i++) {
        int k = i % dim_ess;
        int j = (int) i / dim_ess;
        if (dim_ess*dim_ess < mastereq->getDim()) {
          k = mapEssToFull(k, mastereq->nlevels, mastereq->nessential);
          j = mapEssToFull(j, mastereq->nlevels, mastereq->nessential);
        }
        int elemid_re = getIndexReal(getVecID(k,j,dim_rho));
        int elemid_im = getIndexImag(getVecID(k,j,dim_rho));
        if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(rho_t0, elemid_re, vec[i], INSERT_VALUES);        // RealPart
        if (ilow <= elemid_im && elemid_im < iupp) VecSetValue(rho_t0, elemid_im, vec[i + dim_ess*dim_ess], INSERT_VALUES); // Imaginary Part
      }
    } else { // Schroedinger solver, fill vector 
      for (int i = 0; i < dim_ess; i++) {
        int k = i;
        if (dim_ess < mastereq->getDim()) 
          k = mapEssToFull(i, mastereq->nlevels, mastereq->nessential);
        int elemid_re = getIndexReal(k);
        int elemid_im = getIndexImag(k);
        if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(rho_t0, elemid_re, vec[i], INSERT_VALUES);        // RealPart
        if (ilow <= elemid_im && elemid_im < iupp) VecSetValue(rho_t0, elemid_im, vec[i + dim_ess], INSERT_VALUES); // Imaginary Part
      }
    }
    delete [] vec;
  } else if (initcond_type == InitialConditionType::ENSEMBLE) {
    // Sanity check for the list in initcond_IDs!
    assert(initcond_IDs.size() >= 1); // at least one element 
    assert(initcond_IDs[initcond_IDs.size()-1] < mastereq->getNOscillators()); // last element can't exceed total number of oscillators
    for (int i=0; i < initcond_IDs.size()-1; i++){ // list should be consecutive!
      if (initcond_IDs[i]+1 != initcond_IDs[i+1]) {
        printf("ERROR: List of oscillators for ensemble initialization should be consecutive!\n");
        exit(1);
      }
    }
    // get dimension of subsystems defined by initcond_IDs, as well as the one before and after. Span in essential levels only.
    int dimpre = 1;
    int dimpost = 1;
    int dimsub = 1;
    for (int i=0; i<mastereq->getNOscillators(); i++){
      if (i < initcond_IDs[0]) dimpre *= mastereq->nessential[i];
      else if (initcond_IDs[0] <= i && i <= initcond_IDs[initcond_IDs.size()-1]) dimsub *= mastereq->nessential[i];
      else dimpost *= mastereq->nessential[i];
    }
    int dimrho = mastereq->getDimRho();
    int dimrhoess = mastereq->getDimEss();
    // Loop over ensemble state elements in essential level dimensions of the subsystem defined by the initcond_ids:
    for (int i=0; i < dimsub; i++){
      for (int j=i; j < dimsub; j++){
        int ifull = i * dimpost; // account for the system behind
        int jfull = j * dimpost;
        if (dimrhoess < dimrho) ifull = mapEssToFull(ifull, mastereq->nlevels, mastereq->nessential);
        if (dimrhoess < dimrho) jfull = mapEssToFull(jfull, mastereq->nlevels, mastereq->nessential);
        // printf(" i=%d j=%d ifull %d, jfull %d\n", i, j, ifull, jfull);
        if (i == j) { 
          // diagonal element: 1/N_sub
          int elemid_re = getIndexReal(getVecID(ifull, jfull, dimrho));
          if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(rho_t0, elemid_re, 1./dimsub, INSERT_VALUES);
        } else {
          // upper diagonal (0.5 + 0.5*i) / (N_sub^2)
          int elemid_re = getIndexReal(getVecID(ifull, jfull, dimrho));
          int elemid_im = getIndexImag(getVecID(ifull, jfull, dimrho));
          if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(rho_t0, elemid_re, 0.5/(dimsub*dimsub), INSERT_VALUES);
          if (ilow <= elemid_im && elemid_im < iupp) VecSetValue(rho_t0, elemid_im, 0.5/(dimsub*dimsub), INSERT_VALUES);
          // lower diagonal (0.5 - 0.5*i) / (N_sub^2)
          elemid_re = getIndexReal(getVecID(jfull, ifull, dimrho));
          elemid_im = getIndexImag(getVecID(jfull, ifull, dimrho));
          if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(rho_t0, elemid_re,  0.5/(dimsub*dimsub), INSERT_VALUES);
          if (ilow <= elemid_im && elemid_im < iupp) VecSetValue(rho_t0, elemid_im, -0.5/(dimsub*dimsub), INSERT_VALUES);
        } 
      }
    }
  }
  VecAssemblyBegin(rho_t0); VecAssemblyEnd(rho_t0);

  /* Get target type */  
  purestateID = -1;
  target_filename = "";
  if ( target_str[0].compare("gate") ==0 ) {
    target_type = TargetType::GATE;

    /* Get gate rotation frequencies. Default: use rotational frequencies for the gate. */
    int noscillators = mastereq->nlevels.size();
    copyLast(read_gate_rot, noscillators);
    std::vector<double> gate_rot_freq(noscillators); 
    for (int iosc=0; iosc<noscillators; iosc++) {
      if (read_gate_rot[0] < 1e20) // the config option exists, use it, else use mastereq rotationnal frequency as default
        gate_rot_freq[iosc] = read_gate_rot[iosc];
      else
        gate_rot_freq[iosc] = mastereq->getOscillator(iosc)->getRotFreq();
    }
    /* Initialize the targetgate */
    targetgate = initTargetGate(target_str, mastereq->nlevels, mastereq->nessential, total_time, lindbladtype, gate_rot_freq, quietmode);
  }  
  else if (target_str[0].compare("pure")==0) {
    target_type = TargetType::PURE;
    purestateID = 0;
    if (target_str.size() < 2) {
      printf("# Warning: You want to prepare a pure state, but didn't specify which one. Taking default: ground-state |0...0> \n");
    } else {
      /* Compute the index m for preparing e_m e_m^\dagger. Note that the input is given for pure states PER OSCILLATOR such as |m_1 m_2 ... m_Q> and hence m = m_1 * dimPost(oscil 1) + m_2 * dimPost(oscil 2) + ... + m_Q */
      if (target_str.size() - 1 < mastereq->getNOscillators()) {
        copyLast(target_str, mastereq->getNOscillators()+1);
      }
      for (int i=0; i < mastereq->getNOscillators(); i++) {
        int Qi_state = atoi(target_str[i+1].c_str());
        if (Qi_state >= mastereq->getOscillator(i)->getNLevels()) {
          printf("ERROR in config setting. The requested pure state target |%d> exceeds the number of modeled levels for that oscillator (%d).\n", Qi_state, mastereq->getOscillator(i)->getNLevels());
          exit(1);
        }
        purestateID += Qi_state * mastereq->getOscillator(i)->dim_postOsc;
      }
    }
  } 
  else if (target_str[0].compare("file")==0) { 
    // Get the name of the file and pass it to the OptimTarget type later.
    target_type = TargetType::FROMFILE;
    assert(target_str.size() >= 2);
    target_filename = target_str[1];
  }
  else {
      printf("\n\n ERROR: Unknown optimization target: %s\n", target_str[0].c_str());
      exit(1);
  }

  /* Get the objective function */
  if (objective_str.compare("Jfrobenius")==0)     objective_type = ObjectiveType::JFROBENIUS;
  else if (objective_str.compare("Jtrace")==0)    objective_type = ObjectiveType::JTRACE;
  else if (objective_str.compare("Jmeasure")==0)  objective_type = ObjectiveType::JMEASURE;
  else  {
    printf("\n\n ERROR: Unknown objective function: %s\n", objective_str.c_str());
    exit(1);
  }

  /* Allocate target state, if it is read from file, or if target is a gate transformation VrhoV. If pure target, only store the ID. */
  if (target_type == TargetType::GATE || target_type == TargetType::FROMFILE) {
    VecCreate(PETSC_COMM_WORLD, &targetstate); 
    VecSetSizes(targetstate,PETSC_DECIDE, 2*dim);   // input dim is either N^2 (lindblad eq) or N (schroedinger eq)
    VecSetFromOptions(targetstate);
  }

  /* Read the target state from file into vec */
  if (target_type == TargetType::FROMFILE) {
    int mpirank_world;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
    double* vec = new double[2*dim];
    if (mpirank_world == 0) read_vector(target_filename.c_str(), vec, 2*dim, quietmode);
    MPI_Bcast(vec, 2*dim, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    // pass vec into the targetstate
    PetscInt ilow, iupp;
    VecGetOwnershipRange(targetstate, &ilow, &iupp);
    for (int i = 0; i < dim; i++) { 
      int elemid_re = getIndexReal(i);
      int elemid_im = getIndexImag(i);
      if (ilow <= elemid_re && elemid_re < iupp) VecSetValue(targetstate, elemid_re, vec[i],       INSERT_VALUES); // RealPart
      if (ilow <= elemid_im && elemid_im < iupp) VecSetValue(targetstate, elemid_im, vec[i + dim], INSERT_VALUES); // Imaginary Part
    }
    VecAssemblyBegin(targetstate); VecAssemblyEnd(targetstate);
    delete [] vec;
    // VecView(targetstate, NULL);
  }

  /* Allocate an auxiliary vec needed for evaluating the frobenius norm */
  if (objective_type == ObjectiveType::JFROBENIUS) {
    VecCreate(PETSC_COMM_WORLD, &aux); 
    VecSetSizes(aux,PETSC_DECIDE, 2*dim);
    VecSetFromOptions(aux);
  }
}

OptimTarget::~OptimTarget(){
  if (objective_type == ObjectiveType::JFROBENIUS) VecDestroy(&aux);
  if (target_type == TargetType::GATE || target_type == TargetType::FROMFILE)  VecDestroy(&targetstate);

  delete targetgate;
}

double OptimTarget::FrobeniusDistance(const Vec state){
  // Frobenius distance F = 1/2 || targetstate - state ||^2_F  = 1/2 || vec(targetstate-state)||^2_2
  double norm;
  VecAYPX(aux, 0.0, targetstate);    // aux = targetstate
  VecAXPY(aux, -1.0, state);   // aux = targetstate - state
  VecNorm(aux, NORM_2, &norm);
  double J = norm * norm;

  return J;
}

void OptimTarget::FrobeniusDistance_diff(const Vec state, Vec statebar, const double Jbar){
  
  // Derivative of frobenius distance : statebar += 2 * (targetstate - state) * (-1) * Jbar 
  VecAXPY(statebar,  2.0*Jbar, state);
  VecAXPY(statebar, -2.0*Jbar, targetstate);  
}

void OptimTarget::HilbertSchmidtOverlap(const Vec state, const bool scalebypurity, double* HS_re_ptr, double* HS_im_ptr ){
  /* Lindblas solver: Tr(state * target^\dagger) = vec(target)^dagger * vec(state), will be real!
   * Schroedinger:    Tr(state * target^\dagger) = target^\dag * state, will be complex!*/
  double HS_re = 0.0;
  double HS_im = 0.0;

  /* Simplify computation if the target is PURE, i.e. target = e_m or e_m * e_m^\dag */
  /* Tr(...) = phi_m if Schroedinger, or \rho_mm if Lindblad */
  if (target_type == TargetType::PURE){
    PetscInt ilo, ihi;
    VecGetOwnershipRange(state, &ilo, &ihi);

    int idm = purestateID;
    if (lindbladtype != LindbladType::NONE) idm = getVecID(purestateID, purestateID, (int)sqrt(dim));
    int idm_re = getIndexReal(idm);
    int idm_im = getIndexImag(idm);
    if (ilo <= idm_re && idm_re < ihi) VecGetValues(state, 1, &idm_re, &HS_re); // local!
    if (ilo <= idm_im && idm_im < ihi) VecGetValues(state, 1, &idm_im, &HS_im); // local! Should be 0.0 if Lindblad!
    if (lindbladtype != LindbladType::NONE) assert(fabs(HS_im) <= 1e-14);

    // Communicate over all petsc processors.
    double myre = HS_re;
    double myim = HS_im;
    MPI_Allreduce(&myre, &HS_re, 1, MPI_DOUBLE, MPI_SUM, PETSC_COMM_WORLD);
    MPI_Allreduce(&myim, &HS_im, 1, MPI_DOUBLE, MPI_SUM, PETSC_COMM_WORLD);

  } else { // Target is not of the form e_m (schroedinger) or e_m e_m^\dagger (lindblad).

    if (lindbladtype != LindbladType::NONE) // Lindblad solver. HS overlap is real!
      VecTDot(targetstate, state, &HS_re);  
    else {  // Schroedinger solver. target^\dagger * state
      const PetscScalar* target_ptr;
      const PetscScalar* state_ptr;
      VecGetArrayRead(targetstate, &target_ptr); // these are local vectors
      VecGetArrayRead(state, &state_ptr);
      int ilo, ihi;
      VecGetOwnershipRange(state, &ilo, &ihi);
      for (int i=0; i<dim; i++){
        int ia = getIndexReal(i);
        int ib = getIndexImag(i);
        if (ilo <= ia && ia < ihi) {
          int idre = ia - ilo;
          int idim = ib - ilo;
          HS_re +=  target_ptr[idre]*state_ptr[idre] + target_ptr[idim]*state_ptr[idim];
          HS_im += -target_ptr[idim]*state_ptr[idre] + target_ptr[idre]*state_ptr[idim];
        }
      } 
      VecRestoreArrayRead(targetstate, &target_ptr);
      VecRestoreArrayRead(state, &state_ptr);
      // The above computation was local, so have to sum up here.
      double re=HS_re;
      double im=HS_im;
      MPI_Allreduce(&re, &HS_re, 1, MPI_DOUBLE, MPI_SUM, PETSC_COMM_WORLD);
      MPI_Allreduce(&im, &HS_im, 1, MPI_DOUBLE, MPI_SUM, PETSC_COMM_WORLD);
    }
  }

  // scale by purity Tr(rho(0)^2). 
  if (scalebypurity){ 
    HS_re = HS_re / purity_rho0;
  }

  // return
  *HS_re_ptr = HS_re;
  *HS_im_ptr = HS_im;
}

void OptimTarget::HilbertSchmidtOverlap_diff(const Vec state, Vec statebar, bool scalebypurity, const double HS_re_bar, const double HS_im_bar){

  double scale = 1.0;
  if (scalebypurity){ 
    scale = 1./purity_rho0;
  }

  // Simplified computation if target is pure 
  if (target_type == TargetType::PURE){
    PetscInt ilo, ihi;
    VecGetOwnershipRange(state, &ilo, &ihi);
    int idm = purestateID;
    if (lindbladtype != LindbladType::NONE) idm = getVecID(purestateID, purestateID, (int)sqrt(dim));
    int idm_re = getIndexReal(idm);
    int idm_im = getIndexImag(idm);
    if (ilo <= idm_re && idm_re < ihi) VecSetValue(statebar, idm_re, HS_re_bar*scale, ADD_VALUES);
    if (ilo <= idm_im && idm_im < ihi) VecSetValue(statebar, idm_im, HS_im_bar, ADD_VALUES);

  } else { // Target is not of the form e_m or e_m*e_m^\dagger 

    if (lindbladtype != LindbladType::NONE)
      VecAXPY(statebar, HS_re_bar*scale, targetstate);
    else {
      const PetscScalar* target_ptr;
      PetscScalar* statebar_ptr;
      VecGetArrayRead(targetstate, &target_ptr); 
      VecGetArray(statebar, &statebar_ptr);
      int ilo, ihi;
      VecGetOwnershipRange(state, &ilo, &ihi);
      for (int i=0; i<dim; i++){
        int ia = getIndexReal(i);
        int ib = getIndexImag(i);
        if (ilo <= ia && ia < ihi) {
          int idre = ia - ilo;
          int idim = ib - ilo;
          statebar_ptr[idre] += target_ptr[idre] * HS_re_bar*scale  - target_ptr[idim] * HS_im_bar;
          statebar_ptr[idim] += target_ptr[idim] * HS_re_bar*scale  + target_ptr[idre] * HS_im_bar;
        }
      }
      VecRestoreArrayRead(targetstate, &target_ptr);
      VecRestoreArray(statebar, &statebar_ptr);
    }
  }
}


int OptimTarget::prepareInitialState(const int iinit, const int ninit, std::vector<int> nlevels, std::vector<int> nessential, Vec rho0){

  PetscInt ilow, iupp; 
  PetscInt elemID;
  double val;
  int dim_post;
  int initID = 0;    // Output: ID for this initial condition */

  /* Switch over type of initial condition */
  switch (initcond_type) {

    case InitialConditionType::PERFORMANCE:
      /* Set up Input state psi = 1/sqrt(2N)*(Ones(N) + im*Ones(N)) or rho = psi*psi^\dag */
      VecZeroEntries(rho0);
      VecGetOwnershipRange(rho0, &ilow, &iupp);
      for (int i=0; i<dim_rho; i++){
        if (lindbladtype == LindbladType::NONE) {
          int elem_re = getIndexReal(i);
          int elem_im = getIndexImag(i);
          double val = 1./ sqrt(2.*dim_rho);
          if (ilow <= elem_re && elem_re < iupp) VecSetValue(rho0, elem_re, val, INSERT_VALUES);
          if (ilow <= elem_im && elem_im < iupp) VecSetValue(rho0, elem_im, val, INSERT_VALUES);
        } else {
          int elem_re = getIndexReal(getVecID(i, i, dim_rho));
          double val = 1./ dim_rho;
          if (ilow <= elem_re && elem_re < iupp) VecSetValue(rho0, elem_re, val, INSERT_VALUES);
        }
      }
      break;

    case InitialConditionType::FROMFILE:
      /* Do nothing. Init cond is already stored */
      break;

    case InitialConditionType::PURE:
      /* Do nothing. Init cond is already stored */
      break;

    case InitialConditionType::ENSEMBLE:
      /* Do nothing. Init cond is already stored */
      break;

    case InitialConditionType::THREESTATES:
      assert(lindbladtype != LindbladType::NONE);
      VecZeroEntries(rho0);
      VecGetOwnershipRange(rho0, &ilow, &iupp);

      /* Set the <iinit>'th initial state */
      if (iinit == 0) {
        // 1st initial state: rho(0)_IJ = 2(N-i)/(N(N+1)) Delta_IJ
        initID = 1;
        for (int i_full = 0; i_full<dim_rho; i_full++) {
          int diagID = getIndexReal(getVecID(i_full,i_full,dim_rho));
          double val = 2.*(dim_rho - i_full) / (dim_rho * (dim_rho + 1));
          if (ilow <= diagID && diagID < iupp) VecSetValue(rho0, diagID, val, INSERT_VALUES);
        }
      } else if (iinit == 1) {
        // 2nd initial state: rho(0)_IJ = 1/N
        initID = 2;
        for (int i_full = 0; i_full<dim_rho; i_full++) {
          for (int j_full = 0; j_full<dim_rho; j_full++) {
            double val = 1./dim_rho;
            int index = getIndexReal(getVecID(i_full,j_full,dim_rho));   // Re(rho_ij)
            if (ilow <= index && index < iupp) VecSetValue(rho0, index, val, INSERT_VALUES); 
          }
        }
      } else if (iinit == 2) {
        // 3rd initial state: rho(0)_IJ = 1/N Delta_IJ
        initID = 3;
        for (int i_full = 0; i_full<dim_rho; i_full++) {
          int diagID = getIndexReal(getVecID(i_full,i_full,dim_rho));
          double val = 1./ dim_rho;
          if (ilow <= diagID && diagID < iupp) VecSetValue(rho0, diagID, val, INSERT_VALUES);
        }
      } else {
        printf("ERROR: Wrong initial condition setting! Should never happen.\n");
        exit(1);
      }
      VecAssemblyBegin(rho0); VecAssemblyEnd(rho0);
      break;

    case InitialConditionType::NPLUSONE:
      assert(lindbladtype != LindbladType::NONE);
      VecGetOwnershipRange(rho0, &ilow, &iupp);

      if (iinit < dim_rho) {// Diagonal e_j e_j^\dag
        VecZeroEntries(rho0);
        elemID = getIndexReal(getVecID(iinit, iinit, dim_rho));
        val = 1.0;
        if (ilow <= elemID && elemID < iupp) VecSetValues(rho0, 1, &elemID, &val, INSERT_VALUES);
      }
      else if (iinit == dim_rho) { // fully rotated 1/d*Ones(d)
        for (int i=0; i<dim_rho; i++){
          for (int j=0; j<dim_rho; j++){
            elemID = getIndexReal(getVecID(i,j,dim_rho));
            val = 1.0 / dim_rho;
            if (ilow <= elemID && elemID < iupp) VecSetValues(rho0, 1, &elemID, &val, INSERT_VALUES);
          }
        }
      }
      else {
        printf("Wrong initial condition index. Should never happen!\n");
        exit(1);
      }
      initID = iinit;
      VecAssemblyBegin(rho0); VecAssemblyEnd(rho0);
      break;

    case InitialConditionType::DIAGONAL:
      int row, diagelem;
      VecZeroEntries(rho0);

      /* Get dimension of partial system behind last oscillator ID (essential levels only) */
      dim_post = 1;
      for (int k = initcond_IDs[initcond_IDs.size()-1] + 1; k < nessential.size(); k++) {
        // dim_post *= getOscillator(k)->getNLevels();
        dim_post *= nessential[k];
      }

      /* Compute index of the nonzero element in rho_m(0) = E_pre \otimes |m><m| \otimes E_post */
      diagelem = iinit * dim_post;
      if (dim_ess < dim_rho)  diagelem = mapEssToFull(diagelem, nlevels, nessential);

      /* Set B_{mm} */
      if (lindbladtype != LindbladType::NONE) elemID = getIndexReal(getVecID(diagelem, diagelem, dim_rho)); // density matrix
      else  elemID = getIndexReal(diagelem); 
      val = 1.0;
      VecGetOwnershipRange(rho0, &ilow, &iupp);
      if (ilow <= elemID && elemID < iupp) VecSetValues(rho0, 1, &elemID, &val, INSERT_VALUES);
      VecAssemblyBegin(rho0); VecAssemblyEnd(rho0);

      /* Set initial conditon ID */
      if (lindbladtype != LindbladType::NONE) initID = iinit * ninit + iinit;
      else initID = iinit;

      break;

    case InitialConditionType::BASIS:
      assert(lindbladtype != LindbladType::NONE); // should never happen. For Schroedinger: BASIS equals DIAGONAL, and should go into the above switch case. 

      /* Reset the initial conditions */
      VecZeroEntries(rho0);

      /* Get distribution */
      VecGetOwnershipRange(rho0, &ilow, &iupp);

      /* Get dimension of partial system behind last oscillator ID (essential levels only) */
      dim_post = 1;
      for (int k = initcond_IDs[initcond_IDs.size()-1] + 1; k < nessential.size(); k++) {
        dim_post *= nessential[k];
      }

      /* Get index (k,j) of basis element B_{k,j} for this initial condition index iinit */
      int k, j;
      k = iinit % ( (int) sqrt(ninit) );
      j = (int) iinit / ( (int) sqrt(ninit) );

      /* Set initial condition ID */
      initID = j * ( (int) sqrt(ninit)) + k;

      /* Set position in rho */
      k = k*dim_post;
      j = j*dim_post;
      if (dim_ess < dim_rho) { 
        k = mapEssToFull(k, nlevels, nessential);
        j = mapEssToFull(j, nlevels, nessential);
      }

      if (k == j) {
        /* B_{kk} = E_{kk} -> set only one element at (k,k) */
        elemID = getIndexReal(getVecID(k, k, dim_rho)); // real part in vectorized system
        double val = 1.0;
        if (ilow <= elemID && elemID < iupp) VecSetValues(rho0, 1, &elemID, &val, INSERT_VALUES);
      } else {
      //   /* B_{kj} contains four non-zeros, two per row */
        PetscInt* rows = new PetscInt[4];
        PetscScalar* vals = new PetscScalar[4];

        /* Get storage index of Re(x) */
        rows[0] = getIndexReal(getVecID(k, k, dim_rho)); // (k,k)
        rows[1] = getIndexReal(getVecID(j, j, dim_rho)); // (j,j)
        rows[2] = getIndexReal(getVecID(k, j, dim_rho)); // (k,j)
        rows[3] = getIndexReal(getVecID(j, k, dim_rho)); // (j,k)

        if (k < j) { // B_{kj} = 1/2(E_kk + E_jj) + 1/2(E_kj + E_jk)
          vals[0] = 0.5;
          vals[1] = 0.5;
          vals[2] = 0.5;
          vals[3] = 0.5;
          for (int i=0; i<4; i++) {
            if (ilow <= rows[i] && rows[i] < iupp) VecSetValues(rho0, 1, &(rows[i]), &(vals[i]), INSERT_VALUES);
          }
        } else {  // B_{kj} = 1/2(E_kk + E_jj) + i/2(E_jk - E_kj)
          vals[0] = 0.5;
          vals[1] = 0.5;
          for (int i=0; i<2; i++) {
            if (ilow <= rows[i] && rows[i] < iupp) VecSetValues(rho0, 1, &(rows[i]), &(vals[i]), INSERT_VALUES);
          }
          vals[2] = -0.5;
          vals[3] = 0.5;
          rows[2] = getIndexImag(getVecID(k, j, dim_rho)); // (k,j)
          rows[3] = getIndexImag(getVecID(j, k, dim_rho)); // (j,k)
          for (int i=2; i<4; i++) {
            if (ilow <= rows[i] && rows[i] < iupp) VecSetValues(rho0, 1, &(rows[i]), &(vals[i]), INSERT_VALUES);
          }
        }
        delete [] rows;
        delete [] vals;
      }

      /* Assemble rho0 */
      VecAssemblyBegin(rho0); VecAssemblyEnd(rho0);

      break;

    default:
      printf("ERROR! Wrong initial condition type: %d\n This should never happen!\n", initcond_type);
      exit(1);
  }

  return initID;
}


void OptimTarget::prepareTargetState(const Vec rho_t0){
  // If gate optimization, apply the gate and store targetstate for later use. Else, do nothing.
  if (target_type == TargetType::GATE) targetgate->applyGate(rho_t0, targetstate);

  /* Compute and store the purity of rho(0), Tr(rho(0)^2), so that it can be used by JTrace (HS overlap) */
  VecNorm(rho_t0, NORM_2, &purity_rho0);
  purity_rho0 = purity_rho0 * purity_rho0;
}



void OptimTarget::evalJ(const Vec state, double* J_re_ptr, double* J_im_ptr){
  double J_re = 0.0;
  double J_im = 0.0;
  PetscInt diagID, diagID_re, diagID_im;
  double sum, mine, rhoii, rhoii_re, rhoii_im, lambdai, norm;
  PetscInt ilo, ihi;
  int dimsq;

  switch(objective_type) {

    /* J_Frob = 1/2 * || rho_target - rho(T)||^2_F  */
    case ObjectiveType::JFROBENIUS:

      if (target_type == TargetType::GATE || target_type == TargetType::FROMFILE ) {
        // target state is already set. Either \rho_target = Vrho(0)V^\dagger or read from file. Just eval norm.
        J_re = FrobeniusDistance(state) / 2.0;
      } 
      else {  // target = e_me_m^\dagger ( or target = e_m for Schroedinger)
        assert(target_type == TargetType::PURE);
        // substract 1.0 from m-th diagonal element then take the vector norm 
        if (lindbladtype != LindbladType::NONE) diagID = getIndexReal(getVecID(purestateID,purestateID,(int)sqrt(dim)));
        else diagID = getIndexReal(purestateID);
        VecGetOwnershipRange(state, &ilo, &ihi);
        if (ilo <= diagID && diagID < ihi) VecSetValue(state, diagID, -1.0, ADD_VALUES);
        VecAssemblyBegin(state); VecAssemblyEnd(state);
        norm = 0.0;
        VecNorm(state, NORM_2, &norm);
        J_re = pow(norm, 2.0) / 2.0;
        if (ilo <= diagID && diagID < ihi) VecSetValue(state, diagID, +1.0, ADD_VALUES); // restore original state!
        VecAssemblyBegin(state); VecAssemblyEnd(state);
      }
      break;  // case Frobenius

    /* J_Trace:  1 / purity * Tr(state * target^\dagger)  =  HilbertSchmidtOverlap(target, state) is real if Lindblad, and complex if Schroedinger! */
    case ObjectiveType::JTRACE:

      HilbertSchmidtOverlap(state, true, &J_re, &J_im); // is real if Lindblad solver. 
      break; // case J_Trace

    /* J_Measure = Tr(O_m rho(T)) = \sum_i |i-m| rho_ii(T) if Lindblad and \sum_i |i-m| |phi_i(T)|^2  if Schroedinger */
    case ObjectiveType::JMEASURE:
      // Sanity check
      if (target_type != TargetType::PURE) {
        printf("Error: Wrong setting for objective function. Jmeasure can only be used for 'pure' targets.\n");
        exit(1);
      }

      if (lindbladtype != LindbladType::NONE) dimsq = (int)sqrt(dim); // Lindblad solver: dim = N^2
      else dimsq = dim;   // Schroedinger solver: dim = N

      VecGetOwnershipRange(state, &ilo, &ihi);
      // iterate over diagonal elements 
      sum = 0.0;
      for (int i=0; i<dimsq; i++){
        if (lindbladtype != LindbladType::NONE) {
          diagID = getIndexReal(getVecID(i,i,dimsq));
          rhoii = 0.0;
          if (ilo <= diagID && diagID < ihi) VecGetValues(state, 1, &diagID, &rhoii);
        } else  {
          diagID_re = getIndexReal(i);
          diagID_im = getIndexImag(i);
          rhoii_re = 0.0;
          rhoii_im = 0.0;
          if (ilo <= diagID_re && diagID_re < ihi) VecGetValues(state, 1, &diagID_re, &rhoii_re);
          if (ilo <= diagID_im && diagID_im < ihi) VecGetValues(state, 1, &diagID_im, &rhoii_im);
          rhoii = pow(rhoii_re, 2.0) + pow(rhoii_im, 2.0);
        }
        lambdai = fabs(i - purestateID);
        sum += lambdai * rhoii;
      }
      J_re = sum;
      MPI_Allreduce(&sum, &J_re, 1, MPI_DOUBLE, MPI_SUM, PETSC_COMM_WORLD);
      break; // case J_MEASURE
  }

  // return
  *J_re_ptr = J_re;
  *J_im_ptr = J_im;
}


void OptimTarget::evalJ_diff(const Vec state, Vec statebar, const double J_re_bar, const double J_im_bar){
  PetscInt ilo, ihi;
  double lambdai, val, val_re, val_im, rhoii_re, rhoii_im;
  PetscInt diagID, diagID_re, diagID_im, dimsq;

  switch (objective_type) {

    case ObjectiveType::JFROBENIUS:

      if (target_type == TargetType::GATE || target_type == TargetType::FROMFILE ) {
        FrobeniusDistance_diff(state, statebar, J_re_bar/ 2.0);
      } else {
        assert(target_type == TargetType::PURE);         
        // Derivative of J = 1/2||x||^2 is xbar += x * Jbar, where x = rho(t) - E_mm
        VecAXPY(statebar, J_re_bar, state);
        // now substract 1.0*Jbar from m-th diagonal element
        if (lindbladtype != LindbladType::NONE) diagID = getIndexReal(getVecID(purestateID,purestateID,(int)sqrt(dim)));
        else diagID = getIndexReal(purestateID);
        VecGetOwnershipRange(state, &ilo, &ihi);
        if (ilo <= diagID && diagID < ihi) VecSetValue(statebar, diagID, -1.0*J_re_bar, ADD_VALUES);
      }
      break; // case JFROBENIUS

    case ObjectiveType::JTRACE:
      HilbertSchmidtOverlap_diff(state, statebar, true, J_re_bar, J_im_bar);
    break;

    case ObjectiveType::JMEASURE:
      assert(target_type == TargetType::PURE);         

      if (lindbladtype != LindbladType::NONE) dimsq = (int)sqrt(dim); // Lindblad solver: dim = N^2
      else dimsq = dim;   // Schroedinger solver: dim = N

      // iterate over diagonal elements 
      for (int i=0; i<dimsq; i++){
        lambdai = fabs(i - purestateID);
        VecGetOwnershipRange(state, &ilo, &ihi);
        if (lindbladtype != LindbladType::NONE) {
          diagID = getIndexReal(getVecID(i,i,dimsq));
          val = lambdai * J_re_bar;
          if (ilo <= diagID && diagID < ihi) VecSetValue(statebar, diagID, val, ADD_VALUES);
        } else {
          diagID_re = getIndexReal(i);
          diagID_im = getIndexImag(i);
          rhoii_re = 0.0;
          rhoii_im = 0.0;
          if (ilo <= diagID_re && diagID_re < ihi) VecGetValues(state, 1, &diagID_re, &rhoii_re);
          if (ilo <= diagID_im && diagID_im < ihi) VecGetValues(state, 1, &diagID_im, &rhoii_im);
          if (ilo <= diagID_re && diagID_re < ihi) VecSetValue(statebar, diagID_re, 2.*J_re_bar*lambdai*rhoii_re, ADD_VALUES);
          if (ilo <= diagID_im && diagID_im < ihi) VecSetValue(statebar, diagID_im, 2.*J_re_bar*lambdai*rhoii_im, ADD_VALUES);
        }
      }
    break;
  }
  VecAssemblyBegin(statebar); VecAssemblyEnd(statebar);
}

double OptimTarget::finalizeJ(const double obj_cost_re, const double obj_cost_im) {
  double obj_cost = 0.0;

  if (objective_type == ObjectiveType::JTRACE) {
    if (lindbladtype == LindbladType::NONE) {
      obj_cost = 1.0 - (pow(obj_cost_re,2.0) + pow(obj_cost_im, 2.0));
    } else {
      obj_cost = 1.0 - obj_cost_re;
    }
  } else {
    obj_cost = obj_cost_re;
    assert(obj_cost_im <= 1e-14);
  }

  return obj_cost;
}


void OptimTarget::finalizeJ_diff(const double obj_cost_re, const double obj_cost_im, double* obj_cost_re_bar, double* obj_cost_im_bar){

  if (objective_type == ObjectiveType::JTRACE) {
    if (lindbladtype == LindbladType::NONE) {
      // obj_cost = 1.0 - (pow(obj_cost_re,2.0) + pow(obj_cost_im, 2.0));
      *obj_cost_re_bar = -2.*obj_cost_re;
      *obj_cost_im_bar = -2.*obj_cost_im;
    } else {
      *obj_cost_re_bar = -1.0;
      *obj_cost_im_bar = 0.0;
    }
  } else {
    *obj_cost_re_bar = 1.0;
    *obj_cost_im_bar = 0.0;
  }
}
