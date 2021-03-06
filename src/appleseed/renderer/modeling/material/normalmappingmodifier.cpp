
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2012 Francois Beaune, Jupiter Jazz Limited
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

// Interface header.
#include "normalmappingmodifier.h"

// appleseed.renderer headers.
#include "renderer/modeling/input/source.h"

// appleseed.foundation headers.
#include "foundation/image/color.h"
#include "foundation/math/basis.h"

// Standard headers.
#include <cassert>

using namespace foundation;

namespace renderer
{

NormalMappingModifier::NormalMappingModifier(const Source* map)
  : m_map(map)
{
}

Vector3d NormalMappingModifier::evaluate(
    TextureCache&       texture_cache,
    const Vector3d&     n,
    const Vector2d&     uv,
    const Vector3d&     dpdu,
    const Vector3d&     dpdv) const
{
    // Lookup the normal map.
    Color3f normal_rgb;
    m_map->evaluate(texture_cache, uv, normal_rgb);

    // Reconstruct the normal from the texel value.
    assert(is_saturated(normal_rgb));
    const Vector3f normal(
        normal_rgb[0] * 2.0f - 1.0f,
        normal_rgb[2] * 2.0f - 1.0f,
        normal_rgb[1] * 2.0f - 1.0f);

    // Transform the normal to world space and normalize it.
    const Basis3d basis(n, dpdu);
    return normalize(basis.transform_to_parent(Vector3d(normal)));
}

}   // namespace renderer
