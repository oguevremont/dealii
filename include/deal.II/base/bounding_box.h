// ---------------------------------------------------------------------
//
// Copyright (C) 2017 by the deal.II authors
//
// This file is part of the deal.II library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the deal.II distribution.
//
// ---------------------------------------------------------------------

#ifndef dealii_base_bounding_box_h
#define dealii_base_bounding_box_h


#include <deal.II/base/config.h>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/point.h>

DEAL_II_NAMESPACE_OPEN

/**
 * A class that represents a bounding box in a space with arbitrary dimension
 * <tt>spacedim</tt>.
 *
 * Objects of this class are used to represent bounding boxes. They are,
 * among other uses, useful in parallel distributed meshes to give a general
 * description the owners of each portion of the mesh.
 *
 * Bounding boxes are represented by two vertices (bottom left and top right).
 * Geometrically, a bounding box is:
 * - 1 d: a segment (represented by its vertices in the proper order)
 * - 2 d: a rectangle (represented by the vertices V at bottom left, top right)
 * @code
 * .--------V
 * |        |
 * V--------.
 * @endcode
 *
 * - 3 d: a cuboid (in which case the two vertices V follow the convention and
 * are not owned by the same face)
 * @code
 *   .------V
 *  /      /|
 * .------. |
 * |      | /
 * |      |/
 * V------.
 * @endcode
 * Notice the sides are always parallel to the respective axis.
 *
 */
template <int spacedim, typename Number=double>
class BoundingBox
{
public:
  /**
   * Standard constructor. Creates an object that corresponds to an empty box,
   * i.e. a degenerate box with both points being the origin.
   */
  BoundingBox () = default;

  /**
   * Standard constructor for non-empty boxes: it uses a pair of points
   * which describe the box: one for the bottom and one for the top
   * corner.
   */
  BoundingBox (const std::pair<Point<spacedim,Number>,Point<spacedim,Number>> &boundary_points);

  /**
   * Returns the boundary_points
   */
  const std::pair<Point<spacedim,Number>,Point<spacedim,Number>> &get_boundary_points () const;

  /**
   * Returns true if the point is inside the Bounding Box, false otherwise
   */
  bool point_inside (const Point<spacedim, Number> &p) const;

private:
  std::pair<Point<spacedim, Number>,Point<spacedim,Number>> boundary_points;
};

/*------------------------------- Inline functions: Point ---------------------------*/

#ifndef DOXYGEN


template <int spacedim, typename Number>
inline
BoundingBox<spacedim, Number>::BoundingBox (const std::pair<Point<spacedim,Number>,Point<spacedim,Number>> &boundary_points)
{
  //We check the Bounding Box is not degenerate
  for (unsigned int i=0; i<spacedim; ++i)
    Assert (boundary_points.first[i] < boundary_points.second[i],
            ExcMessage ("Bounding Box can't be created: the point's order should be bottom left, top right!"));

  this->boundary_points = boundary_points;
}

#endif // DOXYGEN
DEAL_II_NAMESPACE_CLOSE

#endif