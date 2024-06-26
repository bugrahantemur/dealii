// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2016 - 2024 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------

#include <deal.II/matrix_free/portable_matrix_free.templates.h>

DEAL_II_NAMESPACE_OPEN



namespace Portable
{
  // Do not instantiate for dim = 1
  template class MatrixFree<2, float>;
  template class MatrixFree<2, double>;
  template class MatrixFree<3, float>;
  template class MatrixFree<3, double>;
} // namespace Portable

DEAL_II_NAMESPACE_CLOSE
