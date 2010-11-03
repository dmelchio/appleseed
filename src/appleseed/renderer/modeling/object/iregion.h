
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

#ifndef APPLESEED_RENDERER_MODELING_OBJECT_IREGION_H
#define APPLESEED_RENDERER_MODELING_OBJECT_IREGION_H

// appleseed.renderer headers.
#include "renderer/global/global.h"
#include "renderer/kernel/tessellation/statictessellation.h"

// appleseed.foundation headers.
#include "foundation/utility/lazy.h"

namespace renderer
{

//
// Region interface.
//

class IRegion
  : public foundation::IIdentifiable
{
  public:
    // Destructor.
    virtual ~IRegion() {}

    // Return the local space bounding box of the region.
    virtual const GAABB3& get_local_bbox() const = 0;

    // Return the static triangle tessellation of the region.
    virtual foundation::Lazy<StaticTriangleTess>& get_static_triangle_tess() const = 0;
};

}       // namespace renderer

#endif  // !APPLESEED_RENDERER_MODELING_OBJECT_IREGION_H