#include "util.hpp"
#include <assert.h>

double sigmoid(double width, double x){
  return 1.0 / ( 1.0 + exp(-width*x) );
}

double sigmoid_diff(double width, double x){
  return sigmoid(width, x) * (1.0 - sigmoid(width, x)) * width;
}

double getRampFactor(const double time, const double tstart, const double tstop, const double tramp){

    double eps = 1e-4; // Cutoff for sigmoid ramp 
    double steep = log(1./eps - 1.) * 2. / tramp; // steepness of sigmoid such that ramp(x) small for x < - tramp/2
    // printf("steep eval %f\n", steep);

    double rampfactor = 0.0;
    if (time <= tstart + tramp) { // ramp up
      double center = tstart + tramp/2.0;
      // rampfactor = sigmoid(steep, time - center);
      rampfactor =  1.0/tramp * time - tstart/ tramp;
    }
    else if (tstart + tramp <= time && 
            time <= tstop - tramp) { // center
      rampfactor = 1.0;
    }
    else if (time >= tstop - tramp && time <= tstop) { // down
      double center = tstop - tramp/2.0;
      // rampfactor = sigmoid(steep, -(time - center));
      // steep = 1842.048073;
      // steep = 1000.0;
      rampfactor =  -1.0/tramp * time + tstop / tramp;
    }

    // If ramp time is larger than total amount of time, turn off control:
    if (tstop < tstart + 2*tramp) rampfactor = 0.0;

    return rampfactor;
}

double getRampFactor_diff(const double time, const double tstart, const double tstop, const double tramp){

    double eps = 1e-4; // Cutoff for sigmoid ramp 
    double steep = log(1./eps - 1.) * 2. / tramp; // steepness of sigmoid such that ramp(x) small for x < - tramp/2
    // printf("steep der %f\n", steep);

    double dramp_dtstop= 0.0;
    if (time <= tstart + tramp) { // ramp up
      dramp_dtstop = 0.0;
    }
    else if (tstart + tramp <= time && 
            time <= tstop - tramp) { // center
      dramp_dtstop = 0.0;
    }
    else if (time >= tstop - tramp && time <= tstop) { // down
      double center = tstop - tramp/2.0;
      // dramp_dtstop = sigmoid_diff(steep, -(time - center));
      // steep = 1842.048073;
      dramp_dtstop = 1.0/tramp;
    }
    
    // If ramp time is larger than total amount of time, turn off control:
    if (tstop < tstart + 2*tramp) dramp_dtstop= 0.0;

    return dramp_dtstop;
}

int getIndexReal(const int i) {
  return 2*i;
}

int getIndexImag(const int i) {
  return 2*i + 1;
}

int getVecID(const int row, const int col, const int dim){
  return row + col * dim;  
} 


int mapEssToFull(const int i, const std::vector<int> &nlevels, const std::vector<int> &nessential){

  int id = 0;
  int index = i;
  for (int iosc = 0; iosc<nlevels.size()-1; iosc++){
    int postdim = 1;
    int postdim_ess = 1;
    for (int j = iosc+1; j<nlevels.size(); j++){
      postdim *= nlevels[j];
      postdim_ess *= nessential[j];
    }
    int iblock = (int) index / postdim_ess;
    index = index % postdim_ess;
    // move id to that block
    id += iblock * postdim;  
  }
  // move to index in last block
  id += index;

  return id;
}

int mapFullToEss(const int i, const std::vector<int> &nlevels, const std::vector<int> &nessential){

  int id = 0;
  int index = i;
  for (int iosc = 0; iosc<nlevels.size(); iosc++){
    int postdim = 1;
    int postdim_ess = 1;
    for (int j = iosc+1; j<nlevels.size(); j++){
      postdim *= nlevels[j];
      postdim_ess *= nessential[j];
    }
    int iblock = (int) index / postdim;
    index = index % postdim;
    if (iblock >= nessential[iosc]) return -1; // this row/col belongs to a guard level, no mapping defined. 
    // move id to that block
    id += iblock * postdim_ess;  
  }

  return id;
}



// void projectToEss(Vec state,const std::vector<int> &nlevels, const std::vector<int> &nessential){

//   /* Get dimensions */
//   int dim_rho = 1;
//   for (int i=0; i<nlevels.size(); i++){
//     dim_rho *=nlevels[i];
//   }

//   /* Get local ownership of the state */
//   PetscInt ilow, iupp;
//   VecGetOwnershipRange(state, &ilow, &iupp);

//   /* Iterate over rows of system matrix, check if it corresponds to an essential level, and if not, set this row and colum to zero */
//   int reID, imID;
//   for (int i=0; i<dim_rho; i++) {
//     // zero out row and column if this does not belong to an essential level
//     if (!isEssential(i, nlevels, nessential)) { 
//       for (int j=0; j<dim_rho; j++) {
//         // zero out row
//         reID = getIndexReal(getVecID(i,j,dim_rho));
//         imID = getIndexImag(getVecID(i,j,dim_rho));
//         if (ilow <= reID && reID < iupp) VecSetValue(state, reID, 0.0, INSERT_VALUES);
//         if (ilow <= imID && imID < iupp) VecSetValue(state, imID, 0.0, INSERT_VALUES);
//         // zero out colum
//         reID = getIndexReal(getVecID(j,i,dim_rho));
//         imID = getIndexImag(getVecID(j,i,dim_rho));
//         if (ilow <= reID && reID < iupp) VecSetValue(state, reID, 0.0, INSERT_VALUES);
//         if (ilow <= imID && imID < iupp) VecSetValue(state, imID, 0.0, INSERT_VALUES);
//       }
//     } 
//   }
//   VecAssemblyBegin(state);
//   VecAssemblyEnd(state);


// }

int isEssential(const int i, const std::vector<int> &nlevels, const std::vector<int> &nessential) {

  int isEss = 1;
  int index = i;
  for (int iosc = 0; iosc < nlevels.size(); iosc++){

    int postdim = 1;
    for (int j = iosc+1; j<nlevels.size(); j++){
      postdim *= nlevels[j];
    }
    int itest = (int) index / postdim;
    // test if essential for this oscillator
    if (itest >= nessential[iosc]) {
      isEss = 0;
      break;
    }
    index = index % postdim;
  }

  return isEss; 
}

int isGuardLevel(const int i, const std::vector<int> &nlevels, const std::vector<int> &nessential){
  int isGuard =  0;
  int index = i;
  for (int iosc = 0; iosc < nlevels.size(); iosc++){

    int postdim = 1;
    for (int j = iosc+1; j<nlevels.size(); j++){
      postdim *= nlevels[j];
    }
    int itest = (int) index / postdim;   // floor(i/n_post)
    // test if this is a guard level for this oscillator
    if (itest == nlevels[iosc] - 1 && itest >= nessential[iosc]) {  // last energy level for system 'iosc'
      isGuard = 1;
      break;
    }
    index = index % postdim;
  }

  return isGuard;
}

PetscErrorCode Ikron(const Mat A,const  int dimI, const double alpha, Mat *Out, InsertMode insert_mode){

    PetscInt ierr;
    PetscInt ncols;
    const PetscInt* cols; 
    const PetscScalar* Avals;
    PetscInt* shiftcols;
    PetscScalar* vals;
    PetscInt dimA;
    PetscInt dimOut;
    PetscInt nonzeroOut;
    PetscInt rowID;

    MatGetSize(A, &dimA, NULL);

    ierr = PetscMalloc1(dimA, &shiftcols); CHKERRQ(ierr);
    ierr = PetscMalloc1(dimA, &vals); CHKERRQ(ierr);

    /* Loop over dimension of I */
    for (PetscInt i = 0; i < dimI; i++){

        /* Set the diagonal block (i*dimA)::(i+1)*dimA */
        for (PetscInt j=0; j<dimA; j++){
            MatGetRow(A, j, &ncols, &cols, &Avals);
            rowID = i*dimA + j;
            for (int k=0; k<ncols; k++){
                shiftcols[k] = cols[k] + i*dimA;
                vals[k] = Avals[k] * alpha;
            }
            MatSetValues(*Out, 1, &rowID, ncols, shiftcols, vals, insert_mode);
            MatRestoreRow(A, j, &ncols, &cols, &Avals);
        }

    }
    // MatAssemblyBegin(*Out, MAT_FINAL_ASSEMBLY);
    // MatAssemblyEnd(*Out, MAT_FINAL_ASSEMBLY);

    PetscFree(shiftcols);
    PetscFree(vals);
    return 0;
}

PetscErrorCode kronI(const Mat A, const int dimI, const double alpha, Mat *Out, InsertMode insert_mode){
    
    PetscInt ierr;
    PetscInt dimA;
    const PetscInt* cols; 
    const PetscScalar* Avals;
    PetscInt rowid;
    PetscInt colid;
    PetscScalar insertval;
    PetscInt dimOut;
    PetscInt nonzeroOut;
    PetscInt ncols;
    MatInfo Ainfo;
    MatGetSize(A, &dimA, NULL);

    ierr = PetscMalloc1(dimA, &cols); CHKERRQ(ierr);
    ierr = PetscMalloc1(dimA, &Avals);

    /* Loop over rows in A */
    for (PetscInt i = 0; i < dimA; i++){
        MatGetRow(A, i, &ncols, &cols, &Avals);

        /* Loop over non negative columns in row i */
        for (PetscInt j = 0; j < ncols; j++){
            //printf("A: row = %d, col = %d, val = %f\n", i, cols[j], Avals[j]);
            
            // dimI rows. global row indices: i, i+dimI
            for (int k=0; k<dimI; k++) {
               rowid = i*dimI + k;
               colid = cols[j]*dimI + k;
               insertval = Avals[j] * alpha;
               MatSetValues(*Out, 1, &rowid, 1, &colid, &insertval, insert_mode);
              //  printf("Setting %d,%d %f\n", rowid, colid, insertval);
            }
        }
        MatRestoreRow(A, i, &ncols, &cols, &Avals);
    }

    // MatAssemblyBegin(*Out, MAT_FINAL_ASSEMBLY);
    // MatAssemblyEnd(*Out, MAT_FINAL_ASSEMBLY);

    PetscFree(cols);
    PetscFree(Avals);

    return 0;
}



PetscErrorCode AkronB(const Mat A, const Mat B, const double alpha, Mat *Out, InsertMode insert_mode){
    PetscInt Adim1, Adim2, Bdim1, Bdim2;
    MatGetSize(A, &Adim1, &Adim2);
    MatGetSize(B, &Bdim1, &Bdim2);

    PetscInt ncolsA, ncolsB;
    const PetscInt *colsA, *colsB;
    const double *valsA, *valsB;
    // Iterate over rows of A 
    for (PetscInt irowA = 0; irowA < Adim1; irowA++){
        // Iterate over non-zero columns in this row of A
        MatGetRow(A, irowA, &ncolsA, &colsA, &valsA);
        for (PetscInt j=0; j<ncolsA; j++) {
            PetscInt icolA = colsA[j];
            PetscScalar valA = valsA[j];
            /* put a B-block at position (irowA*Bdim1, icolA*Bdim2): */
            // Iterate over rows of B 
            for (PetscInt irowB = 0; irowB < Bdim1; irowB++){
                // Iterate over non-zero columns in this B-row
                MatGetRow(B, irowB, &ncolsB, &colsB, &valsB);
                for (PetscInt k=0; k< ncolsB; k++) {
                    PetscInt icolB = colsB[k];
                    PetscScalar valB = valsB[k];
                    /* Insert values in Out */
                    PetscInt rowOut = irowA*Bdim1 + irowB;
                    PetscInt colOut = icolA*Bdim2 + icolB;
                    PetscScalar valOut = valA * valB * alpha; 
                    MatSetValue(*Out, rowOut, colOut, valOut, insert_mode);
                }
                MatRestoreRow(B, irowB, &ncolsB, &colsB, &valsB);
            }
        }   
        MatRestoreRow(A, irowA, &ncolsA, &colsA, &valsA);
    }  

  return 0;
}


PetscErrorCode MatIsAntiSymmetric(Mat A, PetscReal tol, PetscBool *flag) {
  
  int ierr; 

  /* Create B = -A */
  Mat B;
  ierr = MatConvert(A, MATSAME, MAT_INITIAL_MATRIX, &B); CHKERRQ(ierr);
  ierr = MatScale(B, -1.0); CHKERRQ(ierr);

  /* Test if B^T = A */
  ierr = MatIsTranspose(B, A, tol, flag); CHKERRQ(ierr);

  /* Cleanup */
  ierr = MatDestroy(&B); CHKERRQ(ierr);

  return ierr;
}



PetscErrorCode StateIsHermitian(Vec x, PetscReal tol, PetscBool *flag) {
  int ierr;
  int i, j;

  /* TODO: Either make this work in Petsc-parallel, or add error exit if this runs in parallel. */
  
  /* Get u and v from x */
  PetscInt dim;
  ierr = VecGetSize(x, &dim); CHKERRQ(ierr);
  dim = dim/2;
  Vec u, v;
  IS isu, isv;

  int dimis = dim;
  ierr = ISCreateStride(PETSC_COMM_WORLD, dimis, 0, 2, &isu); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_WORLD, dimis, 1, 2, &isv); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isu, &u); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isv, &v); CHKERRQ(ierr);

  /* Init flags*/
  *flag = PETSC_TRUE;

  /* Check for symmetric u and antisymmetric v */
  const double *u_array;
  const double *v_array;
  double u_diff, v_diff;
  ierr = VecGetArrayRead(u, &u_array); CHKERRQ(ierr);
  ierr = VecGetArrayRead(v, &v_array); CHKERRQ(ierr);
  int N = sqrt(dim);
  for (i=0; i<N; i++) {
    for (j=i; j<N; j++) {
      u_diff = u_array[i*N+j] - u_array[j*N+i];
      v_diff = v_array[i*N+j] + v_array[j*N+i];
      if (fabs(u_diff) > tol || fabs(v_diff) > tol ) {
        *flag = PETSC_FALSE;
        break;
      }
    }
  }

  ierr = VecRestoreArrayRead(u, &u_array);
  ierr = VecRestoreArrayRead(v, &v_array);
  ierr = VecRestoreSubVector(x, isu, &u);
  ierr = VecRestoreSubVector(x, isv, &v);
  ISDestroy(&isu);
  ISDestroy(&isv);

  return ierr;
}



PetscErrorCode StateHasTrace1(Vec x, PetscReal tol, PetscBool *flag) {

  int ierr;
  int i;

  /* Get u and v from x */
  PetscInt dim;
  ierr = VecGetSize(x, &dim); CHKERRQ(ierr);
  int dimis = dim/2;
  Vec u, v;
  IS isu, isv;
  ierr = ISCreateStride(PETSC_COMM_WORLD, dimis, 0, 2, &isu); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_WORLD, dimis, 1, 2, &isv); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isu, &u); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isv, &v); CHKERRQ(ierr);

  /* Init flags*/
  *flag = PETSC_FALSE;
  PetscBool u_hastrace1 = PETSC_FALSE;
  PetscBool v_hasdiag0  = PETSC_FALSE;

  /* Check if diagonal of u sums to 1, and diagonal elements of v are 0 */ 
  const double *u_array;
  const double *v_array;
  double u_sum = 0.0;
  double v_sum = 0.0;
  ierr = VecGetArrayRead(u, &u_array); CHKERRQ(ierr);
  ierr = VecGetArrayRead(v, &v_array); CHKERRQ(ierr);
  int N = sqrt(dimis);
  for (i=0; i<N; i++) {
    u_sum += u_array[i*N+i];
    v_sum += fabs(v_array[i*N+i]);
  }
  if ( fabs(u_sum - 1.0) < tol ) u_hastrace1 = PETSC_TRUE;
  if ( fabs(v_sum      ) < tol ) v_hasdiag0  = PETSC_TRUE;

  /* Restore vecs */
  ierr = VecRestoreArrayRead(u, &u_array);
  ierr = VecRestoreArrayRead(v, &v_array);
  ierr = VecRestoreSubVector(x, isu, &u);
  ierr = VecRestoreSubVector(x, isv, &v);

  /* Answer*/
  if (u_hastrace1 && v_hasdiag0) {
    *flag = PETSC_TRUE;
  }
  
  /* Destroy vector strides */
  ISDestroy(&isu);
  ISDestroy(&isv);


  return ierr;
}



PetscErrorCode SanityTests(Vec x, double time){

  /* Sanity check. Be careful: This is costly! */
  printf("Trace check %f ...\n", time);
  PetscBool check;
  double tol = 1e-10;
  StateIsHermitian(x, tol, &check);
  if (!check) {
    printf("WARNING at t=%f: rho is not hermitian!\n", time);
    printf("\n rho :\n");
    VecView(x, PETSC_VIEWER_STDOUT_WORLD);
    exit(1);
  }
  else printf("IsHermitian check passed.\n");
  StateHasTrace1(x, tol, &check);
  if (!check) {
    printf("WARNING at t=%f: Tr(rho) is NOT one!\n", time);
    printf("\n rho :\n");
    VecView(x, PETSC_VIEWER_STDOUT_WORLD);
    exit(1);
  }
  else printf("Trace1 check passed.\n");

  return 0;
}


int read_vector(const char *filename, double *var, int dim, bool quietmode, int skiplines, const std::string testheader) {

  FILE *file;
  double tmp;
  int success = 0;

  file = fopen(filename, "r");

  if (file != NULL) {
    if (!quietmode) printf("Reading file %s, starting from line %d.\n", filename, skiplines+1);

    /* Scip first <skiplines> lines */
    char buffer[51]; // need one extra element because fscanf adds a '\0' at the end
    for (int ix = 0; ix < skiplines; ix++) {
      fscanf(file, "%50[^\n]%*c", buffer); // // NOTE: &buffer[50] is a pointer to buffer[50] (i.e. its last element)
      // printf("Skipping %d lines: %s \n:", skiplines, buffer);
    }

    // Test the header, if given, and set vals to zero if header doesn't match.
    if (testheader.size()>0) {
      if (!quietmode) printf("Compare to Header '%s': ", testheader.c_str());
      // read one (first) line
      int ret = fscanf(file, "%50[^\n]%*c", buffer); // NOTE: &buffer[50] is a pointer to buffer[50] (i.e. its last element)
      std::string header = buffer;
      // Compare to testheader, return if it doesn't match 
      if (ret==EOF || header.compare(0,testheader.size(),testheader) != 0) {
        // printf("Header not found: %s != %s\n", header.c_str(), testheader.c_str());
        printf("Header not found.\n");
        for (int ix = 0; ix < dim; ix++) var[ix] = 0.0;
        fclose(file);
        return success;
      } else {
        if (!quietmode) printf(" Header correct! Reading now.\n");
      }
    }
 
    // printf("Either matching header, or no header given. Now reading lines \n");

    /* Read <dim> lines from file */
    for (int ix = 0; ix < dim; ix++) {
      double myval = 0.0;
      // read the next line
      int ret = fscanf(file, "%lf", &myval); 
      // if end of file, set remaining vars to zero
      if (ret == EOF){ 
        for (int j = ix; j<dim; j++) var[j] = 0.0;
        break;
      } else { // otherwise, set the value
        var[ix] = myval;
      }
      success = 1;
    }
  } else {
    printf("ERROR: Can't open file %s\n", filename);
    exit(1);
  }

  fclose(file);
  return success;
}


/* Compute eigenvalues */
int getEigvals(const Mat A, const int neigvals, std::vector<double>& eigvals, std::vector<Vec>& eigvecs){

int nconv = 0;
#ifdef WITH_SLEPC

  /* Create Slepc's eigensolver */
  EPS eigensolver;       
  EPSCreate(PETSC_COMM_WORLD, &eigensolver);
  EPSSetOperators(eigensolver, A, NULL);
  EPSSetProblemType(eigensolver, EPS_NHEP);
  EPSSetFromOptions(eigensolver);

  /* Number of requested eigenvalues */
  EPSSetDimensions(eigensolver,neigvals,PETSC_DEFAULT,PETSC_DEFAULT);

  // Solve eigenvalue problem
  int ierr = EPSSolve(eigensolver); CHKERRQ(ierr);

  /* Get information about convergence */
  int its, nev, maxit;
  EPSType type;
  double tol;
  EPSGetIterationNumber(eigensolver,&its);
  EPSGetType(eigensolver,&type);
  EPSGetDimensions(eigensolver,&nev,NULL,NULL);
  EPSGetTolerances(eigensolver,&tol,&maxit);
  EPSGetConverged(eigensolver, &nconv );

  PetscPrintf(PETSC_COMM_WORLD," Solution method: %s\n",type);
  PetscPrintf(PETSC_COMM_WORLD," Number of requested eigenpairs: %D\n",nev);
  PetscPrintf(PETSC_COMM_WORLD," Number of iterations taken: %D / %D\n",its, maxit);
  PetscPrintf(PETSC_COMM_WORLD," Stopping condition: tol=%.4g\n",(double)tol);
  PetscPrintf(PETSC_COMM_WORLD," Number of converged eigenpairs: %D\n\n",nconv);

  /* Allocate eigenvectors */
  Vec eigvec;
  MatCreateVecs(A, &eigvec, NULL);

  // Get the result
  double kr, ki, error;
  // printf("Eigenvalues: \n");
  for (int j=0; j<nconv; j++) {
      EPSGetEigenpair( eigensolver, j, &kr, &ki, eigvec, NULL);
      EPSComputeError( eigensolver, j, EPS_ERROR_RELATIVE, &error );
      // printf("%f + i%f (err %f)\n", kr, ki, error);

      /* Store the eigenpair */
      eigvals.push_back(kr);
      eigvecs.push_back(eigvec);
      if (ki != 0.0) printf("Warning: eigenvalue imaginary! : %f", ki);
  }
  // printf("\n");
  // EPSView(eigensolver, PETSC_VIEWER_STDOUT_WORLD);

  /* Clean up*/
  EPSDestroy(&eigensolver);
#endif
  return nconv;
}

// test if A+iB is a unitary matrix: (A+iB)^\dag (A+iB) = I!
bool isUnitary(const Mat V_re, const Mat V_im){
  Mat C, D;
  double norm;
  bool isunitary = true;

  // test: C=V_re^T V_re + Vim^TVim should be the identity!
  MatTransposeMatMult(V_re, V_re, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &C);
  MatTransposeMatMult(V_im, V_im, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &D);
  MatAXPY(C, 1.0, D, DIFFERENT_NONZERO_PATTERN); 
  MatShift(C, -1.0);
  MatNorm(C, NORM_FROBENIUS, &norm);
  if (norm > 1e-12) {
    printf("Unitary Test: V_re^TVre+Vim^TVim is not the identity! %1.14e\n", norm);
    // MatView(C, NULL);
    isunitary = false;
  } 
  MatDestroy(&C);
  MatDestroy(&D);

  // test: C=V_re^T V_im - Vre^TVim should be zero!
  MatTransposeMatMult(V_re, V_im, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &C);
  MatTransposeMatMult(V_im, V_re, MAT_INITIAL_MATRIX, PETSC_DEFAULT, &D);
  MatAXPY(C, -1.0, D, DIFFERENT_NONZERO_PATTERN); 
  MatNorm(C, NORM_FROBENIUS, &norm);
  if (norm > 1e-12) {
    printf("Unitary Test: Vre^TVim - Vim^TVre is not zero! %1.14e\n", norm);
    // MatView(C,NULL);
    isunitary = false;
  }
  MatDestroy(&C);
  MatDestroy(&D);

  return isunitary;
}

bool isUnitary(const Vec &x, const std::vector<IS> &IS_interm_states, const int &ninit, const int &nwindows) {
  bool is_unitary = true;

  Vec v = NULL;
  VecGetSubVector(x, IS_interm_states[0], &v);
  PetscInt dim2, dim;
  VecGetSize(v, &dim2);
  assert(dim2 % 2 == 0);
  dim = dim2 / 2;

  assert(dim == ninit);

  Mat V_re, V_im;
  /* Currently all processes own the same global Vec x. */
  MatCreate(PETSC_COMM_SELF, &V_re);
  MatCreate(PETSC_COMM_SELF, &V_im);
  MatSetSizes(V_re, PETSC_DECIDE, PETSC_DECIDE, dim, dim);
  MatSetSizes(V_im, PETSC_DECIDE, PETSC_DECIDE, dim, dim);
  MatSetUp(V_re);
  MatSetUp(V_im);

  PetscScalar *vptr;

  PetscInt *row_idx;
  PetscScalar *out_re, *out_im;
  PetscMalloc1(dim, &out_re); 
  PetscMalloc1(dim, &out_im); 
  PetscMalloc1(dim, &row_idx);
  for (int d = 0; d < dim; d++) row_idx[d] = d;

  Vec vj = NULL;

  for (int iwindow = 0; iwindow < nwindows-1; iwindow++) {
    for (PetscInt iinit = 0; iinit < ninit; iinit++) {
      int idxi = iinit * (nwindows - 1) + iwindow;
      VecGetSubVector(x, IS_interm_states[idxi], &v);
      VecGetArray(v, &vptr);

      for (int d = 0; d < dim; d++) {
        out_re[d] = vptr[2 * d];
        out_im[d] = vptr[2 * d + 1];
      }

      // MatSetValues(V_re, dim, row_idx, 1, &iinit, vptr, INSERT_VALUES);
      // MatSetValues(V_im, dim, row_idx, 1, &iinit, &vptr[dim], INSERT_VALUES);
      MatSetValues(V_re, dim, row_idx, 1, &iinit, out_re, INSERT_VALUES);
      MatSetValues(V_im, dim, row_idx, 1, &iinit, out_im, INSERT_VALUES);

      VecRestoreArray(v, &vptr);

      // for (PetscInt jinit = 0; jinit < ninit; jinit++) {
      //   int idxj = jinit * (nwindows - 1) + iwindow;
      //   VecGetSubVector(x, IS_interm_states[idxj], &vj);
      //   double re, im;
      //   complex_inner_product(v, vj, re, im);
      //   printf("inner product (%d, %d): (%.2e, %.2e)\n", iinit, jinit, re, im);
      // }
    }
    MatAssemblyBegin(V_re, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(V_re, MAT_FINAL_ASSEMBLY);
    MatAssemblyBegin(V_im, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(V_im, MAT_FINAL_ASSEMBLY);

    is_unitary = isUnitary(V_re, V_im);
    if (!is_unitary) {
      printf("window %d is not unitary!\n", iwindow);
    }
  }
  

  MatDestroy(&V_re);
  MatDestroy(&V_im);
  PetscFree(row_idx);
  PetscFree(out_re);
  PetscFree(out_im);

  return is_unitary;
}

void complex_inner_product(const Vec &x, const Vec &y, double &re, double &im) {
  PetscInt dim2, dummy;
  VecGetSize(x, &dim2);
  VecGetSize(y, &dummy);
  assert(dim2 == dummy);
  assert(dim2 % 2 == 0);

  PetscInt dim = dim2 / 2;

  IS IS_re, IS_im;
  ISCreateStride(PETSC_COMM_WORLD, dim, 0, 2, &IS_re);
  ISCreateStride(PETSC_COMM_WORLD, dim, 1, 2, &IS_im);

  Vec xre, xim, yre, yim;
  VecGetSubVector(x, IS_re, &xre);
  VecGetSubVector(x, IS_im, &xim);
  VecGetSubVector(y, IS_re, &yre);
  VecGetSubVector(y, IS_im, &yim);

  re = 0.0; im = 0.0;
  PetscScalar tmp;

  // Re[conj(x) dot y] = Re[x] * Re[y] + Im[x] * Im[y]
  VecDot(xre, yre, &tmp);
  re += tmp;
  VecDot(xim, yim, &tmp);
  re += tmp;

  // Im[conj(x) dot y] = Re[x] * Im[y] - Im[x] * Re[y]
  VecDot(xre, yim, &tmp);
  im += tmp;
  VecDot(xim, yre, &tmp);
  im -= tmp;

  VecRestoreSubVector(x, IS_re, &xre);
  VecRestoreSubVector(x, IS_im, &xim);
  VecRestoreSubVector(y, IS_re, &yre);
  VecRestoreSubVector(y, IS_im, &yim);

  ISDestroy(&IS_re);
  ISDestroy(&IS_im);

  return;
}

void unitarize(const Vec &x, const std::vector<IS> &IS_interm_states, std::vector<std::vector<Vec>> &interm_ic, std::vector<std::vector<double>> &vnorms) {
  Vec v = NULL;
  VecGetSubVector(x, IS_interm_states[0], &v);
  PetscInt dim2, dim;
  VecGetSize(v, &dim2);
  assert(dim2 % 2 == 0);
  dim = dim2 / 2;

  IS IS_re, IS_im;
  ISCreateStride(PETSC_COMM_WORLD, dim, 0, 2, &IS_re);
  ISCreateStride(PETSC_COMM_WORLD, dim, 1, 2, &IS_im);

  const int ninit = interm_ic.size();
  const int nwindows = interm_ic[0].size() + 1;
  vnorms.resize(ninit);
  for (int iinit = 0; iinit < ninit; iinit++)
    vnorms[iinit].resize(nwindows-1);
  
  Vec ure, uim;
  // Vec vre, vim;
  double uv_re, uv_im, vnorm;
  for (int iwindow = 0; iwindow < nwindows-1; iwindow++) {
    /* classic? GS */
    /* This is actually equivalent to the modified GS. Only changing the order of loop. */
    // for (int iinit = 0; iinit < ninit; iinit++) {
    //   int idxi = iinit * (nwindows - 1) + iwindow;
    //   VecISCopy(x, IS_interm_states[idxi], SCATTER_REVERSE, interm_ic[iinit][iwindow]); 

    //   for (int jinit = 0; jinit < iinit; jinit++) {
    //     // complex_inner_product(interm_ic[iinit][iwindow], interm_ic[jinit][iwindow], uv_re, uv_im);
    //     complex_inner_product(interm_ic[jinit][iwindow], interm_ic[iinit][iwindow], uv_re, uv_im);

    //     VecGetSubVector(interm_ic[jinit][iwindow], IS_re, &vre);
    //     VecGetSubVector(interm_ic[jinit][iwindow], IS_im, &vim);

    //     // Re[v] -= Re[u.v] * Re[u] - Im[u.v] * Im[u]
    //     VecISAXPY(interm_ic[iinit][iwindow], IS_re, -uv_re, vre);
    //     VecISAXPY(interm_ic[iinit][iwindow], IS_re, uv_im, vim);
    //     // Im[v] -= Re[u.v] * Im[u] + Im[u.v] * Re[u]
    //     VecISAXPY(interm_ic[iinit][iwindow], IS_im, -uv_re, vim);
    //     VecISAXPY(interm_ic[iinit][iwindow], IS_im, -uv_im, vre);

    //     VecRestoreSubVector(interm_ic[jinit][iwindow], IS_re, &vre);
    //     VecRestoreSubVector(interm_ic[jinit][iwindow], IS_im, &vim);
    //   }

    //   VecNorm(interm_ic[iinit][iwindow], NORM_2, &vnorm);
    //   VecScale(interm_ic[iinit][iwindow], 1.0 / vnorm);
    //   vnorms[iinit][iwindow] = vnorm;
    // }

    /* modified GS */
    for (int iinit = 0; iinit < ninit; iinit++) {
      int idxi = iinit * (nwindows - 1) + iwindow;
      VecISCopy(x, IS_interm_states[idxi], SCATTER_REVERSE, interm_ic[iinit][iwindow]); 
    }

    for (int iinit = 0; iinit < ninit; iinit++) {
      VecNorm(interm_ic[iinit][iwindow], NORM_2, &vnorm);
      VecScale(interm_ic[iinit][iwindow], 1.0 / vnorm);
      vnorms[iinit][iwindow] = vnorm;

      VecGetSubVector(interm_ic[iinit][iwindow], IS_re, &ure);
      VecGetSubVector(interm_ic[iinit][iwindow], IS_im, &uim);

      for (int jinit = iinit+1; jinit < ninit; jinit++) {
        complex_inner_product(interm_ic[iinit][iwindow], interm_ic[jinit][iwindow], uv_re, uv_im);

        // Re[v] -= Re[u.v] * Re[u] - Im[u.v] * Im[u]
        VecISAXPY(interm_ic[jinit][iwindow], IS_re, -uv_re, ure);
        VecISAXPY(interm_ic[jinit][iwindow], IS_re, uv_im, uim);
        // Im[v] -= Re[u.v] * Im[u] + Im[u.v] * Re[u]
        VecISAXPY(interm_ic[jinit][iwindow], IS_im, -uv_re, uim);
        VecISAXPY(interm_ic[jinit][iwindow], IS_im, -uv_im, ure);
      }

      VecRestoreSubVector(interm_ic[iinit][iwindow], IS_re, &ure);
      VecRestoreSubVector(interm_ic[iinit][iwindow], IS_im, &uim);
    }
  }

  ISDestroy(&IS_re);
  ISDestroy(&IS_im);
}

void unitarize_grad(const Vec &x, const std::vector<IS> &IS_interm_states, const std::vector<std::vector<Vec>> &interm_ic, const std::vector<std::vector<double>> &vnorms, Vec &G) {
  /* The adjoint for unitarize function.
     While unitarize uses the modified Gram-Schmidt, the adjoint formulation is based on the classic Gram-Schmidt.
     The adjoint for the modified G-S requires to store all intermediate v states,
      while that for classic G-S requires only the norm of final v states.
     Their gradients, due to orthonormality, are equivalent to each other.
   */
  Vec w = NULL;
  VecGetSubVector(x, IS_interm_states[0], &w);
  PetscInt dim2, dim;
  VecGetSize(w, &dim2);
  assert(dim2 % 2 == 0);
  dim = dim2 / 2;

  const int ninit = interm_ic.size();
  const int nwindows = interm_ic[0].size() + 1;

  std::vector<Vec> vs(0);
  for (int k = 0; k < ninit; k++) {
    Vec state;
    VecCreate(PETSC_COMM_WORLD, &state);
    VecSetSizes(state, PETSC_DECIDE, dim2);
    VecSetFromOptions(state);
    vs.push_back(state);
  }
  
  Vec us = NULL, ws = NULL;
  VecCreate(PETSC_COMM_WORLD, &us);
  VecSetSizes(us, PETSC_DECIDE, dim2);
  VecSetFromOptions(us);
  VecCreate(PETSC_COMM_WORLD, &ws);
  VecSetSizes(ws, PETSC_DECIDE, dim2);
  VecSetFromOptions(ws);
  PetscScalar *wptr, *vsptr, *usptr, *uptr;
  double wu_re, wu_im, vsu_re, vsu_im;
  int dreal, dimag;
  for (int iwindow = 0; iwindow < nwindows-1; iwindow++) {
    for (int iinit = ninit-1; iinit >= 0; iinit--) {
      int idxi = iinit * (nwindows - 1) + iwindow;
      VecISCopy(G, IS_interm_states[idxi], SCATTER_REVERSE, us); 

      for (int cinit = iinit+1; cinit < ninit; cinit++) {
        int idxc = cinit * (nwindows - 1) + iwindow;
        VecGetSubVector(x, IS_interm_states[idxc], &w);
        complex_inner_product(interm_ic[iinit][iwindow], w, wu_re, wu_im);
        complex_inner_product(interm_ic[iinit][iwindow], vs[cinit], vsu_re, vsu_im);

        VecGetArray(us, &usptr);
        VecGetArray(w, &wptr);
        VecGetArray(vs[cinit], &vsptr);
        for (int d = 0; d < dim; d++) {
          dreal = 2 * d;
          dimag = 2 * d + 1;

          usptr[dreal] -= wu_re * vsptr[dreal] + wu_im * vsptr[dimag] + vsu_re * wptr[dreal] + vsu_im * wptr[dimag];
          usptr[dimag] -= -wu_im * vsptr[dreal] + wu_re * vsptr[dimag] - vsu_im * wptr[dreal] + vsu_re * wptr[dimag];
        }
        VecRestoreArray(us, &usptr);
        VecRestoreArray(w, &wptr);
        VecRestoreArray(vs[cinit], &vsptr);
      }

      double vnorm = vnorms[iinit][iwindow];
      double usu, dummy;
      complex_inner_product(interm_ic[iinit][iwindow], us, usu, dummy);
      VecGetArray(us, &usptr);
      VecGetArray(vs[iinit], &vsptr);
      VecGetArray(interm_ic[iinit][iwindow], &uptr);
      for (int d = 0; d < dim2; d++) {
        vsptr[d] = 1.0 / vnorm * (usptr[d] - usu * uptr[d]);
      }
      VecRestoreArray(us, &usptr);
      VecRestoreArray(vs[iinit], &vsptr);
      VecRestoreArray(interm_ic[iinit][iwindow], &uptr);

      VecCopy(vs[iinit], ws);
      for (int cinit = 0; cinit < iinit; cinit++) {
        complex_inner_product(interm_ic[cinit][iwindow], vs[iinit], vsu_re, vsu_im);

        VecGetArray(ws, &wptr);
        VecGetArray(interm_ic[cinit][iwindow], &uptr);
        for (int d = 0; d < dim; d++) {
          dreal = 2 * d;
          dimag = 2 * d + 1;

          wptr[dreal] -= vsu_re * uptr[dreal] - vsu_im * uptr[dimag];
          wptr[dimag] -= vsu_im * uptr[dreal] + vsu_re * uptr[dimag];
        }
        VecRestoreArray(ws, &wptr);
        VecRestoreArray(interm_ic[cinit][iwindow], &uptr);
      }

      VecISCopy(G, IS_interm_states[idxi], SCATTER_FORWARD, ws);
    }
  }

  for (int k = 0; k < ninit; k++) {
    VecDestroy(&(vs[k]));
  }
  VecDestroy(&ws);
  VecDestroy(&us);
}