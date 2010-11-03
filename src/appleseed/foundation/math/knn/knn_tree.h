
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010 Francois Beaune
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef APPLESEED_FOUNDATION_MATH_KNN_KNN_TREE_H
#define APPLESEED_FOUNDATION_MATH_KNN_KNN_TREE_H

// appleseed.foundation headers.
#include "foundation/core/concepts/noncopyable.h"
#include "foundation/math/knn/knn_node.h"
#include "foundation/math/vector.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <cstddef>
#include <vector>

DECLARE_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenZeroPoint_BuildsValidTree);
DECLARE_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenTwoPoints_BuildsValidTree);
DECLARE_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenFourPoints_BuildsValidTree);
DECLARE_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenEightPoints_GeneratesSevenNodes);

namespace foundation {
namespace knn {

template <typename T, size_t N>
class Tree
  : public NonCopyable
{
  public:
    typedef T ValueType;
    static const size_t Dimension = N;

    typedef Vector<T, N> VectorType;

    bool empty() const;

    // Return the size (in bytes) of this object in memory.
    size_t get_memory_size() const;

  private:
    template <typename, size_t N> friend class Builder;
    template <typename, size_t N> friend class Query;
    template <typename Tree, typename Builder> friend class TreeStatistics;

    GRANT_ACCESS_TO_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenZeroPoint_BuildsValidTree);
    GRANT_ACCESS_TO_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenTwoPoints_BuildsValidTree);
    GRANT_ACCESS_TO_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenFourPoints_BuildsValidTree);
    GRANT_ACCESS_TO_TEST_CASE(Foundation_Math_Knn_Builder, Build_GivenEightPoints_GeneratesSevenNodes);

    typedef Node<T> NodeType;

    std::vector<VectorType> m_points;
    std::vector<size_t>     m_indices;
    std::vector<NodeType>   m_nodes;
};

typedef Tree<float, 2>  Tree2f;
typedef Tree<double, 2> Tree2d;
typedef Tree<float, 3>  Tree3f;
typedef Tree<double, 3> Tree3d;


//
// Implementation.
//

template <typename T, size_t N>
inline bool Tree<T, N>::empty() const
{
    return m_points.empty();
}

template <typename T, size_t N>
inline size_t Tree<T, N>::get_memory_size() const
{
    size_t mem_size = sizeof(*this);
    mem_size += m_points.capacity() * sizeof(VectorType);
    mem_size += m_indices.capacity() * sizeof(size_t);
    mem_size += m_nodes.capacity() * sizeof(NodeType);
    return mem_size;
}

}       // namespace knn
}       // namespace foundation

#endif  // !APPLESEED_FOUNDATION_MATH_KNN_KNN_TREE_H