// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2000 - 2025 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------


#include <deal.II/base/utilities.h>

#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparsity_pattern.h>
#include <deal.II/lac/sparsity_tools.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>

DEAL_II_NAMESPACE_OPEN

constexpr SparsityPattern::size_type SparsityPattern::invalid_entry;



SparsityPattern::SparsityPattern()
  : SparsityPatternBase()
  , store_diagonal_first_in_row(false)
  , max_dim(0)
  , max_vec_len(0)
  , max_row_length(0)
  , compressed(false)
{
  reinit(0, 0, 0);
}



SparsityPattern::SparsityPattern(const SparsityPattern &s)
  : SparsityPattern()
{
  Assert(s.empty(),
         ExcMessage(
           "This constructor can only be called if the provided argument "
           "is the sparsity pattern for an empty matrix. This constructor can "
           "not be used to copy-construct a non-empty sparsity pattern."));

  reinit(0, 0, 0);
}



SparsityPattern::SparsityPattern(const size_type    m,
                                 const size_type    n,
                                 const unsigned int max_per_row)
  : SparsityPattern()
{
  reinit(m, n, max_per_row);
}



SparsityPattern::SparsityPattern(const size_type                  m,
                                 const size_type                  n,
                                 const std::vector<unsigned int> &row_lengths)
  : SparsityPattern()
{
  reinit(m, n, row_lengths);
}



SparsityPattern::SparsityPattern(const size_type    m,
                                 const unsigned int max_per_row)
  : SparsityPattern()
{
  reinit(m, m, max_per_row);
}



SparsityPattern::SparsityPattern(const size_type                  m,
                                 const std::vector<unsigned int> &row_lengths)
  : SparsityPattern()
{
  reinit(m, m, row_lengths);
}



SparsityPattern::SparsityPattern(const SparsityPattern &original,
                                 const unsigned int     max_per_row,
                                 const size_type        extra_off_diagonals)
  : SparsityPattern()
{
  Assert(original.n_rows() == original.n_cols(), ExcNotQuadratic());
  Assert(original.is_compressed(), ExcNotCompressed());

  reinit(original.n_rows(), original.n_cols(), max_per_row);

  // now copy the entries from the other object
  for (size_type row = 0; row < original.rows; ++row)
    {
      // copy the elements of this row of the other object
      //
      // note that the first object actually is the main-diagonal element,
      // which we need not copy
      //
      // we do the copying in two steps: first we note that the elements in
      // @p{original} are sorted, so we may first copy all the elements up to
      // the first side-diagonal one which is to be filled in. then we insert
      // the side-diagonals, finally copy the rest from that element onwards
      // which is not a side-diagonal any more.
      const size_type *const original_row_start =
        &original.colnums[original.rowstart[row]] + 1;
      // the following requires that @p{original} be compressed since
      // otherwise there might be invalid_entry's
      const size_type *const original_row_end =
        &original.colnums[original.rowstart[row + 1]];

      // find pointers before and after extra off-diagonals. if at top or
      // bottom of matrix, then set these pointers such that no copying is
      // necessary (see the @p{copy} commands)
      const size_type *const original_last_before_side_diagonals =
        (row > extra_off_diagonals ?
           Utilities::lower_bound(original_row_start,
                                  original_row_end,
                                  row - extra_off_diagonals) :
           original_row_start);

      const size_type *const original_first_after_side_diagonals =
        (row < rows - extra_off_diagonals - 1 ?
           std::upper_bound(original_row_start,
                            original_row_end,
                            row + extra_off_diagonals) :
           original_row_end);

      // find first free slot. the first slot in each row is the diagonal
      // element
      size_type *next_free_slot = &colnums[rowstart[row]] + 1;

      // copy elements before side-diagonals
      next_free_slot = std::copy(original_row_start,
                                 original_last_before_side_diagonals,
                                 next_free_slot);

      // insert left and right side-diagonals
      for (size_type i = 1; i <= std::min(row, extra_off_diagonals);
           ++i, ++next_free_slot)
        *next_free_slot = row - i;
      for (size_type i = 1; i <= std::min(extra_off_diagonals, rows - row - 1);
           ++i, ++next_free_slot)
        *next_free_slot = row + i;

      // copy rest
      next_free_slot = std::copy(original_first_after_side_diagonals,
                                 original_row_end,
                                 next_free_slot);

      // this error may happen if the sum of previous elements per row and
      // those of the new diagonals exceeds the maximum number of elements per
      // row given to this constructor
      Assert(next_free_slot <= &colnums[rowstart[row + 1]],
             ExcNotEnoughSpace(0, rowstart[row + 1] - rowstart[row]));
    }
}



SparsityPattern &
SparsityPattern::operator=(const SparsityPattern &s)
{
  Assert(s.empty(),
         ExcMessage(
           "This operator can only be called if the provided argument "
           "is the sparsity pattern for an empty matrix. This operator can "
           "not be used to copy a non-empty sparsity pattern."));

  Assert(this->empty(),
         ExcMessage("This operator can only be called if the current object is "
                    "empty."));

  return *this;
}



void
SparsityPattern::reinit(const size_type                      m,
                        const size_type                      n,
                        const ArrayView<const unsigned int> &row_lengths)
{
  AssertDimension(row_lengths.size(), m);
  resize(m, n);

  // delete empty matrices
  if ((m == 0) || (n == 0))
    {
      rowstart.reset();
      colnums.reset();

      max_vec_len = max_dim = 0;
      // if dimension is zero: ignore max_per_row
      max_row_length = 0;
      compressed     = false;

      return;
    }

  // first, if the matrix is quadratic, we will have to make sure that each
  // row has at least one entry for the diagonal element. make this more
  // obvious by having a variable which we can query
  store_diagonal_first_in_row = (m == n);

  // find out how many entries we need in the @p{colnums} array. if this
  // number is larger than @p{max_vec_len}, then we will need to reallocate
  // memory
  //
  // note that the number of elements per row is bounded by the number of
  // columns
  //
  std::size_t vec_len = 0;
  for (size_type i = 0; i < m; ++i)
    vec_len += std::min(static_cast<size_type>(store_diagonal_first_in_row ?
                                                 std::max(row_lengths[i], 1U) :
                                                 row_lengths[i]),
                        n);

  // sometimes, no entries are requested in the matrix (this most often
  // happens when blocks in a block matrix are simply zero). in that case,
  // allocate exactly one element, to have a valid pointer to some memory
  if (vec_len == 0)
    {
      vec_len     = 1;
      max_vec_len = vec_len;
      colnums     = std::make_unique<size_type[]>(max_vec_len);
    }

  max_row_length =
    (row_lengths.empty() ?
       0 :
       std::min(static_cast<size_type>(
                  *std::max_element(row_lengths.begin(), row_lengths.end())),
                n));

  if (store_diagonal_first_in_row && (max_row_length == 0) && (m != 0))
    max_row_length = 1;

  // allocate memory for the rowstart values, if necessary. even though we
  // re-set the pointers again immediately after deleting their old content,
  // set them to zero in between because the allocation might fail, in which
  // case we get an exception and the destructor of this object will be called
  // -- where we look at the non-nullness of the (now invalid) pointer again
  // and try to delete the memory a second time.
  if (rows > max_dim)
    {
      max_dim  = rows;
      rowstart = std::make_unique<std::size_t[]>(max_dim + 1);
    }

  // allocate memory for the column numbers if necessary
  if (vec_len > max_vec_len)
    {
      max_vec_len = vec_len;
      colnums     = std::make_unique<size_type[]>(max_vec_len);
    }

  // set the rowstart array
  rowstart[0] = 0;
  for (size_type i = 1; i <= rows; ++i)
    rowstart[i] = rowstart[i - 1] +
                  (store_diagonal_first_in_row ?
                     std::clamp(static_cast<size_type>(row_lengths[i - 1]),
                                static_cast<size_type>(1U),
                                n) :
                     std::min(static_cast<size_type>(row_lengths[i - 1]), n));
  Assert((rowstart[rows] == vec_len) ||
           ((vec_len == 1) && (rowstart[rows] == 0)),
         ExcInternalError());

  // preset the column numbers by a value indicating it is not in use
  std::fill_n(colnums.get(), vec_len, invalid_entry);

  // if diagonal elements are special: let the first entry in each row be the
  // diagonal value
  if (store_diagonal_first_in_row)
    for (size_type i = 0; i < n_rows(); ++i)
      colnums[rowstart[i]] = i;

  compressed = false;
}



void
SparsityPattern::reinit(const size_type                  m,
                        const size_type                  n,
                        const std::vector<unsigned int> &row_lengths)
{
  reinit(m, n, make_array_view(row_lengths));
}



void
SparsityPattern::reinit(const size_type    m,
                        const size_type    n,
                        const unsigned int max_per_row)
{
  // simply map this function to the other @p{reinit} function
  const std::vector<unsigned int> row_lengths(m, max_per_row);
  reinit(m, n, row_lengths);
}



void
SparsityPattern::compress()
{
  // nothing to do if the object corresponds to an empty matrix
  if ((rowstart == nullptr) && (colnums == nullptr))
    {
      compressed = true;
      return;
    }

  // do nothing if already compressed
  if (compressed)
    return;

  std::size_t next_free_entry = 0, next_row_start = 0, row_length = 0;

  // first find out how many non-zero elements there are, in order to allocate
  // the right amount of memory
  const std::size_t nonzero_elements =
    std::count_if(&colnums[rowstart[0]],
                  &colnums[rowstart[rows]],
                  [](const size_type col) { return col != invalid_entry; });
  // now allocate the respective memory
  std::unique_ptr<size_type[]> new_colnums(new size_type[nonzero_elements]);


  // reserve temporary storage to store the entries of one row
  std::vector<size_type> tmp_entries(max_row_length);

  // Traverse all rows
  for (size_type line = 0; line < n_rows(); ++line)
    {
      // copy used entries, break if first unused entry is reached
      row_length = 0;
      for (std::size_t j = rowstart[line]; j < rowstart[line + 1];
           ++j, ++row_length)
        if (colnums[j] != invalid_entry)
          tmp_entries[row_length] = colnums[j];
        else
          break;
      // now @p{rowstart} is the number of entries in this line

      // Sort only beginning at the second entry, if optimized storage of
      // diagonal entries is on.

      // if this line is empty or has only one entry, don't sort
      if (row_length > 1)
        std::sort((store_diagonal_first_in_row) ? tmp_entries.begin() + 1 :
                                                  tmp_entries.begin(),
                  tmp_entries.begin() + row_length);

      // insert column numbers into the new field
      for (size_type j = 0; j < row_length; ++j)
        new_colnums[next_free_entry++] = tmp_entries[j];

      // note new start of this and the next row
      rowstart[line] = next_row_start;
      next_row_start = next_free_entry;

      // some internal checks: either the matrix is not quadratic, or if it
      // is, then the first element of this row must be the diagonal element
      // (i.e. with column index==line number)
      // this test only makes sense if we have written to the index
      // rowstart_line in new_colnums which is the case if row_length is not 0,
      // so check this first
      Assert((!store_diagonal_first_in_row) ||
               (row_length != 0 && new_colnums[rowstart[line]] == line),
             ExcInternalError());
      // assert that the first entry does not show up in the remaining ones
      // and that the remaining ones are unique among themselves (this handles
      // both cases, quadratic and rectangular matrices)
      //
      // the only exception here is if the row contains no entries at all
      Assert((rowstart[line] == next_row_start) ||
               (std::find(&new_colnums[rowstart[line] + 1],
                          &new_colnums[next_row_start],
                          new_colnums[rowstart[line]]) ==
                &new_colnums[next_row_start]),
             ExcInternalError());
      Assert((rowstart[line] == next_row_start) ||
               (std::adjacent_find(&new_colnums[rowstart[line] + 1],
                                   &new_colnums[next_row_start]) ==
                &new_colnums[next_row_start]),
             ExcInternalError());
    }

  // assert that we have used all allocated space, no more and no less
  Assert(next_free_entry == nonzero_elements, ExcInternalError());

  // set iterator-past-the-end
  rowstart[rows] = next_row_start;

  // set colnums to the newly allocated array and delete previous content
  // in the process
  colnums = std::move(new_colnums);

  // store the size
  max_vec_len = nonzero_elements;

  compressed = true;
}



void
SparsityPattern::copy_from(const SparsityPattern &sp)
{
  // first determine row lengths for each row. if the matrix is quadratic,
  // then we might have to add an additional entry for the diagonal, if that
  // is not yet present. as we have to call compress anyway later on, don't
  // bother to check whether that diagonal entry is in a certain row or not
  const bool                do_diag_optimize = (sp.n_rows() == sp.n_cols());
  std::vector<unsigned int> row_lengths(sp.n_rows());
  for (size_type i = 0; i < sp.n_rows(); ++i)
    {
      row_lengths[i] = sp.row_length(i);
      if (do_diag_optimize && !sp.exists(i, i))
        ++row_lengths[i];
    }
  reinit(sp.n_rows(), sp.n_cols(), row_lengths);

  // now enter all the elements into the matrix, if there are any. note that
  // if the matrix is quadratic, then we already have the diagonal element
  // preallocated
  if (n_rows() != 0 && n_cols() != 0)
    for (size_type row = 0; row < sp.n_rows(); ++row)
      {
        size_type *cols = &colnums[rowstart[row]] + (do_diag_optimize ? 1 : 0);
        typename SparsityPattern::iterator col_num = sp.begin(row),
                                           end_row = sp.end(row);

        for (; col_num != end_row; ++col_num)
          {
            const size_type col = col_num->column();
            if ((col != row) || !do_diag_optimize)
              *cols++ = col;
          }
      }

  // do not need to compress the sparsity pattern since we already have
  // allocated the right amount of data, and the SparsityPattern data is
  // sorted, too.
  compressed = true;
}



// Use a special implementation for DynamicSparsityPattern where we can use
// the column_number method to gain faster access to the
// entries. DynamicSparsityPattern::iterator can show quadratic complexity in
// case many rows are empty and the begin() method needs to jump to the next
// free row. Otherwise, the code is exactly the same as above.
void
SparsityPattern::copy_from(const DynamicSparsityPattern &dsp)
{
  const bool  do_diag_optimize = (dsp.n_rows() == dsp.n_cols());
  const auto &row_index_set    = dsp.row_index_set();

  std::vector<unsigned int> row_lengths(dsp.n_rows());

  if (row_index_set.size() == 0)
    {
      for (size_type i = 0; i < dsp.n_rows(); ++i)
        {
          row_lengths[i] = dsp.row_length(i);
          if (do_diag_optimize && !dsp.exists(i, i))
            ++row_lengths[i];
        }
    }
  else
    {
      for (size_type i = 0; i < dsp.n_rows(); ++i)
        {
          if (row_index_set.is_element(i))
            {
              row_lengths[i] = dsp.row_length(i);
              if (do_diag_optimize && !dsp.exists(i, i))
                ++row_lengths[i];
            }
          else
            {
              // If the row i is not stored in the DynamicSparsityPattern we
              // nevertheless need to allocate 1 entry per row for the
              // "diagonal optimization". (We store a pointer to the next row
              // in place of the repeated index i for the diagonal element.)
              row_lengths[i] = do_diag_optimize ? 1 : 0;
            }
        }
    }
  reinit(dsp.n_rows(), dsp.n_cols(), row_lengths);

  if (n_rows() != 0 && n_cols() != 0)
    for (size_type row = 0; row < dsp.n_rows(); ++row)
      {
        size_type *cols = &colnums[rowstart[row]] + (do_diag_optimize ? 1 : 0);
        const unsigned int row_length = dsp.row_length(row);
        for (unsigned int index = 0; index < row_length; ++index)
          {
            const size_type col = dsp.column_number(row, index);
            if ((col != row) || !do_diag_optimize)
              *cols++ = col;
          }
      }

  // do not need to compress the sparsity pattern since we already have
  // allocated the right amount of data, and the SparsityPatternType data is
  // sorted, too.
  compressed = true;
}



template <typename number>
void
SparsityPattern::copy_from(const FullMatrix<number> &matrix)
{
  // first init with the number of entries per row. if this matrix is square
  // then we also have to allocate memory for the diagonal entry, unless we
  // have already counted it
  const bool matrix_is_square = (matrix.m() == matrix.n());

  std::vector<unsigned int> entries_per_row(matrix.m(), 0);
  for (size_type row = 0; row < matrix.m(); ++row)
    {
      for (size_type col = 0; col < matrix.n(); ++col)
        if (matrix(row, col) != 0)
          ++entries_per_row[row];
      if (matrix_is_square && (matrix(row, row) == 0))
        ++entries_per_row[row];
    }

  reinit(matrix.m(), matrix.n(), entries_per_row);

  // now set entries. if we enter entries row by row, then we'll get
  // quadratic complexity in the number of entries per row. this is
  // not usually a problem (we don't usually create dense matrices),
  // but there are cases where it matters -- so we may as well be
  // gentler and hand over a whole row of entries at a time
  std::vector<size_type> column_indices;
  column_indices.reserve(
    entries_per_row.size() > 0 ?
      *std::max_element(entries_per_row.begin(), entries_per_row.end()) :
      0);
  for (size_type row = 0; row < matrix.m(); ++row)
    {
      column_indices.resize(entries_per_row[row]);

      size_type current_index = 0;
      for (size_type col = 0; col < matrix.n(); ++col)
        if (matrix(row, col) != 0)
          {
            column_indices[current_index] = col;
            ++current_index;
          }
        else
          // the (row,col) entry is zero; check if we need to add it
          // anyway because it's the diagonal entry of a square
          // matrix
          if (matrix_is_square && (col == row))
            {
              column_indices[current_index] = row;
              ++current_index;
            }

      // check that we really added the correct number of indices
      Assert(current_index == entries_per_row[row], ExcInternalError());

      // now bulk add all of these entries
      add_entries(row, column_indices.begin(), column_indices.end(), true);
    }

  // finally compress
  compress();
}



bool
SparsityPattern::empty() const
{
  // let's try to be on the safe side of life by using multiple possibilities
  // in the check for emptiness... (sorry for this kludge -- emptying matrices
  // and freeing memory was not present in the original implementation and I
  // don't know at how many places I missed something in adding it, so I try
  // to be cautious. wb)
  if ((rowstart == nullptr) || (n_rows() == 0) || (n_cols() == 0))
    {
      Assert(rowstart == nullptr, ExcInternalError());
      Assert(n_rows() == 0, ExcInternalError());
      Assert(n_cols() == 0, ExcInternalError());
      Assert(colnums == nullptr, ExcInternalError());
      Assert(max_vec_len == 0, ExcInternalError());

      return true;
    }
  return false;
}



SparsityPattern::size_type
SparsityPattern::operator()(const size_type i, const size_type j) const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  AssertIndexRange(i, n_rows());
  AssertIndexRange(j, n_cols());
  Assert(compressed, ExcNotCompressed());

  // let's see whether there is something in this line
  if (rowstart[i] == rowstart[i + 1])
    return invalid_entry;

  // If special storage of diagonals was requested, we can get the diagonal
  // element faster by this query.
  if (store_diagonal_first_in_row && (i == j))
    return rowstart[i];

  // all other entries are sorted, so we can use a binary search algorithm
  //
  // note that the entries are only sorted upon compression, so this would
  // fail for non-compressed sparsity patterns; however, that is why the
  // Assertion is at the top of this function, so it may not be called for
  // noncompressed structures.
  const size_type *sorted_region_start =
    (store_diagonal_first_in_row ? &colnums[rowstart[i] + 1] :
                                   &colnums[rowstart[i]]);
  const size_type *const p =
    Utilities::lower_bound<const size_type *>(sorted_region_start,
                                              &colnums[rowstart[i + 1]],
                                              j);
  if ((p != &colnums[rowstart[i + 1]]) && (*p == j))
    return (p - colnums.get());
  else
    return invalid_entry;
}



bool
SparsityPattern::exists(const size_type i, const size_type j) const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  AssertIndexRange(i, n_rows());
  AssertIndexRange(j, n_cols());

  for (size_type k = rowstart[i]; k < rowstart[i + 1]; ++k)
    {
      // entry already exists
      if (colnums[k] == j)
        return true;
    }
  return false;
}



std::pair<SparsityPattern::size_type, SparsityPattern::size_type>
SparsityPattern::matrix_position(const std::size_t global_index) const
{
  Assert(compressed == true, ExcNotCompressed());
  AssertIndexRange(global_index, n_nonzero_elements());

  // first find the row in which the entry is located. for this note that the
  // rowstart array indexes the global indices at which each row starts. since
  // it is sorted, and since there is an element for the one-past-last row, we
  // can simply use a bisection search on it
  const size_type row =
    (std::upper_bound(rowstart.get(), rowstart.get() + rows, global_index) -
     rowstart.get() - 1);

  // now, the column index is simple since that is what the colnums array
  // stores:
  const size_type col = colnums[global_index];

  // so return the respective pair
  return std::make_pair(row, col);
}



SparsityPattern::size_type
SparsityPattern::row_position(const size_type i, const size_type j) const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  AssertIndexRange(i, n_rows());
  AssertIndexRange(j, n_cols());

  for (size_type k = rowstart[i]; k < rowstart[i + 1]; ++k)
    {
      // entry exists
      if (colnums[k] == j)
        return k - rowstart[i];
    }
  return numbers::invalid_size_type;
}



SparsityPattern::size_type
SparsityPattern::bandwidth() const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  size_type b = 0;
  for (size_type i = 0; i < n_rows(); ++i)
    for (size_type j = rowstart[i]; j < rowstart[i + 1]; ++j)
      if (colnums[j] != invalid_entry)
        {
          if (static_cast<size_type>(
                std::abs(static_cast<int>(i - colnums[j]))) > b)
            b = std::abs(static_cast<signed int>(i - colnums[j]));
        }
      else
        // leave if at the end of the entries of this line
        break;
  return b;
}



SparsityPattern::size_type
SparsityPattern::max_entries_per_row() const
{
  // if compress() has not yet been called, we can get the maximum number of
  // elements per row using the stored value
  if (!compressed)
    return max_row_length;

  // if compress() was called, we use a better algorithm which gives us a
  // sharp bound
  size_type m = 0;
  for (size_type i = 1; i <= rows; ++i)
    m = std::max(m, static_cast<size_type>(rowstart[i] - rowstart[i - 1]));

  return m;
}



void
SparsityPattern::add(const size_type i, const size_type j)
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  AssertIndexRange(i, n_rows());
  AssertIndexRange(j, n_cols());
  Assert(compressed == false, ExcMatrixIsCompressed());

  for (std::size_t k = rowstart[i]; k < rowstart[i + 1]; ++k)
    {
      // entry already exists
      if (colnums[k] == j)
        return;
      // empty entry found, put new entry here
      if (colnums[k] == invalid_entry)
        {
          colnums[k] = j;
          return;
        }
    }

  // if we came thus far, something went wrong: there was not enough space in
  // this line
  Assert(false, ExcNotEnoughSpace(i, rowstart[i + 1] - rowstart[i]));
}



template <typename ForwardIterator>
void
SparsityPattern::add_entries(const size_type row,
                             ForwardIterator begin,
                             ForwardIterator end,
                             const bool      indices_are_sorted)
{
  AssertIndexRange(row, n_rows());
  if (indices_are_sorted == true)
    {
      if (begin != end)
        {
          ForwardIterator it                 = begin;
          bool            has_larger_entries = false;
          // skip diagonal
          std::size_t k = rowstart[row] + store_diagonal_first_in_row;
          for (; k < rowstart[row + 1]; ++k)
            if (colnums[k] == invalid_entry)
              break;
            else if (colnums[k] >= *it)
              {
                has_larger_entries = true;
                break;
              }
          if (has_larger_entries == false)
            for (; it != end; ++it)
              {
                AssertIndexRange(*it, n_cols());
                if (store_diagonal_first_in_row && *it == row)
                  continue;
                Assert(k <= rowstart[row + 1],
                       ExcNotEnoughSpace(row,
                                         rowstart[row + 1] - rowstart[row]));
                colnums[k++] = *it;
              }
          else
            // cannot just append the new range at the end, forward to the
            // other function
            for (ForwardIterator p = begin; p != end; ++p)
              add(row, *p);
        }
    }
  else
    {
      // forward to the other function.
      for (ForwardIterator it = begin; it != end; ++it)
        add(row, *it);
    }
}



void
SparsityPattern::add_row_entries(const size_type                  &row,
                                 const ArrayView<const size_type> &columns,
                                 const bool indices_are_sorted)
{
  add_entries(row, columns.begin(), columns.end(), indices_are_sorted);
}



void
SparsityPattern::symmetrize()
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());
  Assert(compressed == false, ExcMatrixIsCompressed());
  // Note that we only require a quadratic matrix here, no special treatment
  // of diagonals
  Assert(n_rows() == n_cols(), ExcNotQuadratic());

  // loop over all elements presently in the sparsity pattern and add the
  // transpose element. note:
  //
  // 1. that the sparsity pattern changes which we work on, but not the
  // present row
  //
  // 2. that the @p{add} function can be called on elements that already exist
  // without any harm
  for (size_type row = 0; row < n_rows(); ++row)
    for (size_type k = rowstart[row]; k < rowstart[row + 1]; ++k)
      {
        // check whether we are at the end of the entries of this row. if so,
        // go to next row
        if (colnums[k] == invalid_entry)
          break;

        // otherwise add the transpose entry if this is not the diagonal (that
        // would not harm, only take time to check up)
        if (colnums[k] != row)
          add(colnums[k], row);
      }
}



bool
SparsityPattern::operator==(const SparsityPattern &sp2) const
{
  if (store_diagonal_first_in_row != sp2.store_diagonal_first_in_row)
    return false;

  // it isn't quite necessary to compare *all* member variables. by only
  // comparing the essential ones, we can say that two sparsity patterns are
  // equal even if one is compressed and the other is not (in which case some
  // of the member variables are not yet set correctly)
  if (rows != sp2.rows || cols != sp2.cols || compressed != sp2.compressed)
    return false;

  if (rows > 0)
    {
      for (size_type i = 0; i < rows + 1; ++i)
        if (rowstart[i] != sp2.rowstart[i])
          return false;

      for (size_type i = 0; i < rowstart[rows]; ++i)
        if (colnums[i] != sp2.colnums[i])
          return false;
    }

  return true;
}



void
SparsityPattern::print(std::ostream &out) const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());

  AssertThrow(out.fail() == false, ExcIO());

  for (size_type i = 0; i < n_rows(); ++i)
    {
      out << '[' << i;
      for (size_type j = rowstart[i]; j < rowstart[i + 1]; ++j)
        if (colnums[j] != invalid_entry)
          out << ',' << colnums[j];
      out << ']' << std::endl;
    }

  AssertThrow(out.fail() == false, ExcIO());
}



void
SparsityPattern::print_gnuplot(std::ostream &out) const
{
  Assert((rowstart != nullptr) && (colnums != nullptr), ExcEmptyObject());

  AssertThrow(out.fail() == false, ExcIO());

  for (size_type i = 0; i < n_rows(); ++i)
    for (size_type j = rowstart[i]; j < rowstart[i + 1]; ++j)
      if (colnums[j] != invalid_entry)
        // while matrix entries are usually written (i,j), with i vertical and
        // j horizontal, gnuplot output is x-y, that is we have to exchange
        // the order of output
        out << colnums[j] << " " << -static_cast<signed int>(i) << std::endl;

  AssertThrow(out.fail() == false, ExcIO());
}



void
SparsityPattern::print_svg(std::ostream &out) const
{
  const unsigned int m = this->n_rows();
  const unsigned int n = this->n_cols();
  out
    << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
    << n + 2 << " " << m + 2
    << " \">\n"
       "<style type=\"text/css\" >\n"
       "     <![CDATA[\n"
       "      rect.pixel {\n"
       "          fill:   #ff0000;\n"
       "      }\n"
       "    ]]>\n"
       "  </style>\n\n"
       "   <rect width=\""
    << n + 2 << "\" height=\"" << m + 2
    << "\" fill=\"rgb(128, 128, 128)\"/>\n"
       "   <rect x=\"1\" y=\"1\" width=\""
    << n + 0.1 << "\" height=\"" << m + 0.1
    << "\" fill=\"rgb(255, 255, 255)\"/>\n\n";

  for (const auto &entry : *this)
    {
      out << "  <rect class=\"pixel\" x=\"" << entry.column() + 1 << "\" y=\""
          << entry.row() + 1 << "\" width=\".9\" height=\".9\"/>\n";
    }
  out << "</svg>" << std::endl;
}



void
SparsityPattern::block_write(std::ostream &out) const
{
  AssertThrow(out.fail() == false, ExcIO());

  // first the simple objects, bracketed in [...]
  out << '[' << max_dim << ' ' << n_rows() << ' ' << n_cols() << ' '
      << max_vec_len << ' ' << max_row_length << ' ' << compressed << ' '
      << store_diagonal_first_in_row << "][";
  // then write out real data
  out.write(reinterpret_cast<const char *>(rowstart.get()),
            reinterpret_cast<const char *>(rowstart.get() + max_dim + 1) -
              reinterpret_cast<const char *>(rowstart.get()));
  out << "][";
  out.write(reinterpret_cast<const char *>(colnums.get()),
            reinterpret_cast<const char *>(colnums.get() + max_vec_len) -
              reinterpret_cast<const char *>(colnums.get()));
  out << ']';

  AssertThrow(out.fail() == false, ExcIO());
}



void
SparsityPattern::block_read(std::istream &in)
{
  AssertThrow(in.fail() == false, ExcIO());

  char c;

  // first read in simple data
  in >> c;
  AssertThrow(c == '[', ExcIO());
  in >> max_dim >> rows >> cols >> max_vec_len >> max_row_length >>
    compressed >> store_diagonal_first_in_row;

  in >> c;
  AssertThrow(c == ']', ExcIO());
  in >> c;
  AssertThrow(c == '[', ExcIO());

  // reallocate space
  rowstart = std::make_unique<std::size_t[]>(max_dim + 1);
  colnums  = std::make_unique<size_type[]>(max_vec_len);

  // then read data
  in.read(reinterpret_cast<char *>(rowstart.get()),
          reinterpret_cast<char *>(rowstart.get() + max_dim + 1) -
            reinterpret_cast<char *>(rowstart.get()));
  in >> c;
  AssertThrow(c == ']', ExcIO());
  in >> c;
  AssertThrow(c == '[', ExcIO());
  in.read(reinterpret_cast<char *>(colnums.get()),
          reinterpret_cast<char *>(colnums.get() + max_vec_len) -
            reinterpret_cast<char *>(colnums.get()));
  in >> c;
  AssertThrow(c == ']', ExcIO());
}



std::size_t
SparsityPattern::memory_consumption() const
{
  return (max_dim * sizeof(size_type) + sizeof(*this) +
          max_vec_len * sizeof(size_type));
}



#ifndef DOXYGEN
// explicit instantiations
template void
SparsityPattern::copy_from<float>(const FullMatrix<float> &);
template void
SparsityPattern::copy_from<double>(const FullMatrix<double> &);

template void
SparsityPattern::add_entries<const SparsityPattern::size_type *>(
  const size_type,
  const size_type *,
  const size_type *,
  const bool);
#  ifndef DEAL_II_VECTOR_ITERATOR_IS_POINTER
template void
SparsityPattern::add_entries<
  std::vector<SparsityPattern::size_type>::const_iterator>(
  const size_type,
  std::vector<size_type>::const_iterator,
  std::vector<size_type>::const_iterator,
  const bool);
#  endif
template void
SparsityPattern::add_entries<std::vector<SparsityPattern::size_type>::iterator>(
  const size_type,
  std::vector<size_type>::iterator,
  std::vector<size_type>::iterator,
  const bool);
#endif

DEAL_II_NAMESPACE_CLOSE
