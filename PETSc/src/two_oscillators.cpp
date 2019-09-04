#include "two_oscillators.hpp"

PetscErrorCode ExactSolution(PetscReal t,Vec s, PetscReal freq)
{
  PetscScalar    *s_localptr;
  PetscErrorCode ierr;

  /* Get a pointer to vector data. */
  ierr = VecGetArray(s,&s_localptr);CHKERRQ(ierr);

  /* Write the solution into the array locations.
   *  Alternatively, we could use VecSetValues() or VecSetValuesLocal(). */
  PetscScalar phi = (1./4.) * (t - (1./ freq)*PetscSinScalar(freq*t));
  PetscScalar theta = (1./4.) * (t + (1./freq)*PetscCosScalar(freq*t) - 1.);
  PetscScalar cosphi = PetscCosScalar(phi);
  PetscScalar costheta = PetscCosScalar(theta);
  PetscScalar sinphi = PetscSinScalar(phi);
  PetscScalar sintheta = PetscSinScalar(theta);
  

  /* Real part */
  s_localptr[0] = cosphi*costheta*cosphi*costheta;
  s_localptr[1] = -1.*cosphi*sintheta*cosphi*costheta;
  s_localptr[2] = 0.;
  s_localptr[3] = 0.;
  s_localptr[4] = -1.*cosphi*costheta*cosphi*sintheta;
  s_localptr[5] = cosphi*sintheta*cosphi*sintheta;
  s_localptr[6] = 0.;
  s_localptr[7] = 0.;
  s_localptr[8] = 0.;
  s_localptr[9] = 0.;
  s_localptr[10] = sinphi*costheta*sinphi*costheta;
  s_localptr[11] = -1.*sinphi*sintheta*sinphi*costheta;
  s_localptr[12] = 0.;
  s_localptr[13] = 0.;
  s_localptr[14] = -1.*sinphi*costheta*sinphi*sintheta;
  s_localptr[15] = sinphi*sintheta*sinphi*sintheta;
  /* Imaginary part */
  s_localptr[16] = 0.;
  s_localptr[17] = 0.;
  s_localptr[18] = - sinphi*costheta*cosphi*costheta;
  s_localptr[19] = sinphi*sintheta*cosphi*costheta;
  s_localptr[20] = 0.;
  s_localptr[21] = 0.;
  s_localptr[22] = sinphi*costheta*cosphi*sintheta;
  s_localptr[23] = - sinphi*sintheta*cosphi*sintheta;
  s_localptr[24] = cosphi*costheta*sinphi*costheta;
  s_localptr[25] = - cosphi*sintheta*sinphi*costheta;
  s_localptr[26] = 0.;
  s_localptr[27] = 0.;
  s_localptr[28] = - cosphi*costheta*sinphi*sintheta;
  s_localptr[29] = cosphi*sintheta*sinphi*sintheta;
  s_localptr[30] = 0.;
  s_localptr[31] = 0.;

  /* Restore solution vector */
  ierr = VecRestoreArray(s,&s_localptr);CHKERRQ(ierr);
  return 0;
}


PetscErrorCode InitialConditions(Vec x, PetscReal freq)
{
  ExactSolution(0,x,freq);
  return 0;
}


PetscScalar F1(PetscReal t, TS_App *petsc_app)
{
  /* Get pointer to the beginning of the F1-coefficients in the coeffs vector */
  int istart = 0 * petsc_app->nvec;
  double* coeff_startptr = &petsc_app->spline_coeffs[istart];

  /* Return spline evaluation at time t */
  double val = petsc_app->spline->evaluate(t, coeff_startptr);;
  return val;
}


PetscScalar G1(PetscReal t,TS_App *petsc_app)
{
  /* Get pointer to the beginning of the G1-coefficients in the coeffs vector */
  int istart = 1 * petsc_app->nvec;
  double* coeff_startptr = &petsc_app->spline_coeffs[istart];

  /* Return spline evaluation at time t */
  double val = petsc_app->spline->evaluate(t, coeff_startptr);;
  return val;
}


PetscScalar F1_analytic(PetscReal t, PetscReal freq)
{
  PetscScalar f = (1./4.) * (1. - PetscCosScalar(freq * t));
  return f;
}


PetscScalar G2_analytic(PetscReal t,PetscReal freq)
{
  PetscScalar g = (1./4.) * (1. - PetscSinScalar(freq * t));
  return g;
}

PetscScalar F2(PetscReal t, TS_App *petsc_app)
{
  /* Get pointer to the beginning of the F2-coefficients in the coeffs vector */
  int istart = 2 * petsc_app->nvec;
  double* coeff_startptr = &petsc_app->spline_coeffs[istart];

  /* Return spline evaluation at time t */
  double val = petsc_app->spline->evaluate(t, coeff_startptr);
  return val;
}


PetscScalar G2(PetscReal t,TS_App *petsc_app)
{
  /* Get pointer to the beginning of the G2-coefficients in the coeffs vector */
  int istart = 3 * petsc_app->nvec;
  double* coeff_startptr = &petsc_app->spline_coeffs[istart];

  /* Return spline evaluation at time t */
  double val = petsc_app->spline->evaluate(t, coeff_startptr);
  return val;
}

PetscErrorCode RHSJacobian(TS ts,PetscReal t,Vec u,Mat M,Mat P,void *ctx)
{
  TS_App *petsc_app = (TS_App*)ctx;
  PetscInt nvec = petsc_app->nvec;
  PetscScalar f1, f2, g1, g2;
  PetscErrorCode ierr;
  PetscInt ncol;
  const PetscInt *col_idx;
  const PetscScalar *vals;
  PetscScalar *negvals;
  PetscInt *col_idx_shift;

  /* Allocate tmp vectors */
  ierr = PetscMalloc1(nvec, &col_idx_shift);CHKERRQ(ierr);
  ierr = PetscMalloc1(nvec, &negvals);CHKERRQ(ierr);

  /* Compute time-dependent control functions */
  f1 = F1(t, petsc_app);
  f2 = F2(t, petsc_app);
  g1 = G1(t, petsc_app);
  g2 = G2(t, petsc_app);

  /* Sum up real part of system matrix A = g1*A1 + g2*A2  */
  ierr = MatZeroEntries(petsc_app->A);CHKERRQ(ierr);
  // ierr = MatAXPY(petsc_app->A,g1,petsc_app->A1,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
  ierr = MatAXPY(petsc_app->A,g2,petsc_app->A2,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);

  /* Sum up imaginary part of system matrix B = f1*B1 + f2*B2 + H_const  */
  ierr = MatZeroEntries(petsc_app->B);CHKERRQ(ierr);
  ierr = MatAXPY(petsc_app->B,f1,petsc_app->B1,DIFFERENT_NONZERO_PATTERN); CHKERRQ(ierr);
  // ierr = MatAXPY(petsc_app->B,f2,petsc_app->B2,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);

  /* Set up Jacobian M 
   * M(0, 0) =  A    M(0,1) = B
   * M(0, 1) = -B    M(1,1) = A
   */
  for (int irow = 0; irow < nvec; irow++) {
    PetscInt irow_shift = irow + nvec;

    /* Get row in A */
    ierr = MatGetRow(petsc_app->A, irow, &ncol, &col_idx, &vals);CHKERRQ(ierr);
    for (int icol = 0; icol < ncol; icol++)
    {
      col_idx_shift[icol] = col_idx[icol] + nvec;
    }
    // Set A in M: M(0,0) = A  M(1,1) = A
    ierr = MatSetValues(M,1,&irow,ncol,col_idx,vals,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatSetValues(M,1,&irow_shift,ncol,col_idx_shift,vals,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatRestoreRow(petsc_app->A,irow,&ncol,&col_idx,&vals);CHKERRQ(ierr);

    /* Get row in B */
    ierr = MatGetRow(petsc_app->B, irow, &ncol, &col_idx, &vals);CHKERRQ(ierr);
    for (int icol = 0; icol < ncol; icol++)
    {
      col_idx_shift[icol] = col_idx[icol] + nvec;
      negvals[icol] = -vals[icol];
    }
    // Set B in M: M(1,0) = B, M(0,1) = -B
    ierr = MatSetValues(M,1,&irow,ncol,col_idx_shift,negvals,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatSetValues(M,1,&irow_shift,ncol,col_idx,vals,INSERT_VALUES);CHKERRQ(ierr);
    ierr = MatRestoreRow(petsc_app->B,irow,&ncol,&col_idx,&vals);CHKERRQ(ierr);
  }

  /* Assemble M */
  ierr = MatAssemblyBegin(M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(M,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  // MatView(M, PETSC_VIEWER_STDOUT_SELF);

  /* Cleanup */
  ierr = PetscFree(col_idx_shift);
  ierr = PetscFree(negvals);


  /* TODO: Do we really need to store A and B explicitely? They are only used here, so maybe we can assemble M from g,f,IKbMDb, bMbdTKI, aPadTKI, IKaPad directly... */


  return 0;
}




PetscErrorCode SetUpMatrices(TS_App *petsc_app)
{
  PetscInt       nvec = petsc_app->nvec;
  PetscInt       nlevels = petsc_app->nlevels;
  PetscInt       i, j;
  PetscScalar    v[1];
  PetscErrorCode ierr;
  Mat tmp;
  /* Create building matrices matrices */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&petsc_app->A1);CHKERRQ(ierr);
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&petsc_app->A2);CHKERRQ(ierr);
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&petsc_app->B1);CHKERRQ(ierr);
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&petsc_app->B2);CHKERRQ(ierr);
  /* Set Petsc Prefix for mat viewing */
  ierr = MatSetOptionsPrefix(petsc_app->A1, "A1");
  ierr = MatSetOptionsPrefix(petsc_app->A2, "A2");
  ierr = MatSetOptionsPrefix(petsc_app->B1, "B1");
  ierr = MatSetOptionsPrefix(petsc_app->B2, "B2");

  /* --- Set up A1 = C^{-}(n^2, n) - C^-(1,n^3)^T --- */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&tmp);CHKERRQ(ierr);
  ierr = BuildingBlock(tmp, nlevels, -1, 1, (int) pow(nlevels, 3));
  ierr = MatTranspose(tmp, MAT_INPLACE_MATRIX, &tmp);
  ierr = BuildingBlock(petsc_app->A1, nlevels, -1, (int)pow(nlevels, 2), nlevels);
  ierr = MatAXPY(petsc_app->A1, -1., tmp, DIFFERENT_NONZERO_PATTERN);
  ierr = MatAssemblyBegin(petsc_app->A1, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->A1, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatDestroy(&tmp);

  /* --- Set up A2 = C^{-}(n^3,1) - C^{-}(n,n^2)^T --- */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&tmp);CHKERRQ(ierr);
  ierr = BuildingBlock(tmp, nlevels, -1,nlevels, (int) pow(nlevels, 2));
  ierr = MatTranspose(tmp, MAT_INPLACE_MATRIX, &tmp);
  ierr = BuildingBlock(petsc_app->A2, nlevels, -1, (int)pow(nlevels, 3), 1);
  ierr = MatAXPY(petsc_app->A2, -1., tmp, DIFFERENT_NONZERO_PATTERN);
  ierr = MatAssemblyBegin(petsc_app->A2, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->A2, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatDestroy(&tmp);
  
  /* --- Set up B1 = C^+(1,n^3)^T - C^+(n^2, n) --- */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&tmp);CHKERRQ(ierr);
  ierr = BuildingBlock(tmp, nlevels, 1, (int) pow(nlevels, 2), nlevels);
  ierr = BuildingBlock(petsc_app->B1, nlevels, 1, 1, (int)pow(nlevels, 3));
  ierr = MatTranspose(petsc_app->B1, MAT_INPLACE_MATRIX, &petsc_app->B1);
  ierr = MatAXPY(petsc_app->B1, -1., tmp, DIFFERENT_NONZERO_PATTERN);
  ierr = MatAssemblyBegin(petsc_app->B1, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->B1, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatDestroy(&tmp);

  /* --- Set up B2 = C^+(n,n^2)^T - C^+(n^3, 1) --- */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,1,NULL,&tmp);CHKERRQ(ierr);
  ierr = BuildingBlock(tmp, nlevels, 1, nlevels, (int) pow(nlevels, 2));
  ierr = BuildingBlock(petsc_app->B2, nlevels, 1, nlevels, (int)pow(nlevels, 2));
  ierr = MatTranspose(petsc_app->B2, MAT_INPLACE_MATRIX, &petsc_app->B2);
  ierr = MatAXPY(petsc_app->B2, -1., tmp, DIFFERENT_NONZERO_PATTERN);
  ierr = MatAssemblyBegin(petsc_app->B2, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->B2, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatDestroy(&tmp);


  /* Allocate A */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,0,NULL,&petsc_app->A);CHKERRQ(ierr);
  ierr = MatSetFromOptions(petsc_app->A);CHKERRQ(ierr);
  ierr = MatSetUp(petsc_app->A);CHKERRQ(ierr);
  ierr = MatSetOption(petsc_app->A, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE);CHKERRQ(ierr);
  for (int irow = 0; irow < nvec; irow++)
  {
    ierr = MatSetValue(petsc_app->A, irow, irow, 0.0, INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = MatAssemblyBegin(petsc_app->A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);


  /* Allocate B */
  ierr = MatCreateSeqAIJ(PETSC_COMM_SELF,nvec,nvec,0,NULL,&petsc_app->B);CHKERRQ(ierr);
  ierr = MatSetFromOptions(petsc_app->B);CHKERRQ(ierr);
  ierr = MatSetUp(petsc_app->B);CHKERRQ(ierr);
  ierr = MatAssemblyBegin(petsc_app->B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(petsc_app->B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  return 0;
}


PetscErrorCode BuildingBlock(Mat C, PetscInt n, PetscInt sign, PetscInt k, PetscInt m) {
  int i, j;
  int ierr;
  double val;

  // iterate over the blocks
  for(int x = 0; x < k; x++) {
    // iterate over the unique entrys within each block
    for(int y = 0; y < n-1; y++) {
      // iterate over the repetitions of each entry
      for(int z = 0; z < m; z++) {
        // Position of upper element
        i = x*n*m + y*m + z;
        j = i + m;
        // Value of upper elements
        val = sqrt((double)(y+1));
        ierr = MatSetValues(C,1,&i,1,&j,&val,INSERT_VALUES);CHKERRQ(ierr);
        // Lower element
        val = sign * val;
        ierr = MatSetValues(C,1,&j,1,&i,&val,INSERT_VALUES);CHKERRQ(ierr);
      }
    }
  }

  ierr = MatAssemblyBegin(C,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(C,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  return 0;
}