// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010 - 2025 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------


// Compare preconditioned Richardson with relaxation. All output diffs
// should be zero.

#include <deal.II/lac/precondition.h>
#include <deal.II/lac/solver_control.h>
#include <deal.II/lac/solver_relaxation.h>
#include <deal.II/lac/solver_richardson.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/vector.h>
#include <deal.II/lac/vector_memory.h>

#include "../tests.h"

#include "../testmatrix.h"

template <typename SolverType,
          typename MatrixType,
          typename VectorType,
          class PRECONDITION>
double
check_solve(SolverType         &solver,
            const MatrixType   &A,
            VectorType         &u,
            VectorType         &f,
            const PRECONDITION &P)
{
  double result = 0.;
  u             = 0.;
  f             = 1.;
  try
    {
      solver.solve(A, u, f, P);
    }
  catch (SolverControl::NoConvergence &e)
    {
      deallog << "Failure step " << e.last_step << " value " << e.last_residual
              << std::endl;
      result = e.last_residual;
    }
  return result;
}

int
main()
{
  const std::string logname = "output";
  std::ofstream     logfile(logname);
  //  logfile.setf(std::ios::fixed);
  deallog << std::setprecision(4);
  deallog.attach(logfile);

  SolverControl      control(10, 1.e-3);
  SolverRichardson<> rich(control);
  SolverRelaxation<> relax(control);

  for (unsigned int size = 7; size <= 30; size *= 3)
    {
      unsigned int dim = (size - 1) * (size - 1);

      deallog << "Size " << size << " Unknowns " << dim << std::endl;

      // Make matrix
      FDMatrix        testproblem(size, size);
      SparsityPattern structure(dim, dim, 5);
      testproblem.five_point_structure(structure);
      structure.compress();
      SparseMatrix<double> A(structure);
      testproblem.five_point(A);

      PreconditionJacobi<> prec_jacobi;
      prec_jacobi.initialize(A, 0.6);
      PreconditionSOR<> prec_sor;
      prec_sor.initialize(A, 1.2);
      PreconditionSSOR<> prec_ssor1;
      prec_ssor1.initialize(A, 1.);
      PreconditionSSOR<> prec_ssor2;
      prec_ssor2.initialize(A, 1.2);

      Vector<double> f(dim);
      Vector<double> u(dim);
      Vector<double> res(dim);

      f = 1.;
      u = 1.;

      try
        {
          double r1, r2;

          r1 = check_solve(rich, A, u, f, prec_jacobi);
          r2 = check_solve(relax, A, u, f, prec_jacobi);
          deallog << "Jacobi  diff " << std::fabs(r1 - r2) / r1 << std::endl;

          r1 = check_solve(rich, A, u, f, prec_sor);
          r2 = check_solve(relax, A, u, f, prec_sor);
          deallog << "SOR     diff " << std::fabs(r1 - r2) / r1 << std::endl;

          r1 = check_solve(rich, A, u, f, prec_ssor1);
          r2 = check_solve(relax, A, u, f, prec_ssor1);
          deallog << "SSOR1   diff " << std::fabs(r1 - r2) / r1 << std::endl;

          r1 = check_solve(rich, A, u, f, prec_ssor2);
          r2 = check_solve(relax, A, u, f, prec_ssor2);
          deallog << "SSOR1.2 diff " << std::fabs(r1 - r2) / r1 << std::endl;
        }
      catch (const std::exception &e)
        {
          std::cerr << "Exception: " << e.what() << std::endl;
        }
    };

  // Solve advection problem
  for (unsigned int size = 4; size <= 3; size *= 3)
    {
      unsigned int dim = (size - 1) * (size - 1);

      deallog << "Size " << size << " Unknowns " << dim << std::endl;

      // Make matrix
      FDMatrix        testproblem(size, size);
      SparsityPattern structure(dim, dim, 5);
      testproblem.five_point_structure(structure);
      structure.compress();
      SparseMatrix<double> A(structure);
      testproblem.upwind(A, true);

      PreconditionSOR<> prec_sor;
      prec_sor.initialize(A, 1.);

      std::vector<types::global_dof_index> permutation(dim);
      std::vector<types::global_dof_index> inverse_permutation(dim);

      // Create a permutation: Blocks
      // backwards and every second
      // block backwards
      unsigned int k = 0;
      for (unsigned int i = 0; i < size - 1; ++i)
        for (unsigned int j = 0; j < size - 1; ++j)
          {
            permutation[k++] = i * (size - 1) + size - j - 2;
          }

      for (unsigned int i = 0; i < permutation.size(); ++i)
        std::cerr << ' ' << permutation[i];
      std::cerr << std::endl;

      for (unsigned int i = 0; i < permutation.size(); ++i)
        inverse_permutation[permutation[i]] = i;

      for (unsigned int i = 0; i < permutation.size(); ++i)
        std::cerr << ' ' << inverse_permutation[i];
      std::cerr << std::endl;

      PreconditionPSOR<> prec_psor;
      prec_psor.initialize(A, permutation, inverse_permutation, 1.);

      Vector<double> f(dim);
      Vector<double> u(dim);
      f = 1.;
      u = 1.;

      std::cerr << "******************************" << std::endl;

      check_solve(rich, A, u, f, prec_sor);
      check_solve(rich, A, u, f, prec_psor);
    }
}
