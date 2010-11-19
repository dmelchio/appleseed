
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

#ifndef APPLESEED_FOUNDATION_UTILITY_BENCHMARK_BENCHMARKAGGREGATOR_H
#define APPLESEED_FOUNDATION_UTILITY_BENCHMARK_BENCHMARKAGGREGATOR_H

// appleseed.foundation headers.
#include "foundation/core/concepts/noncopyable.h"
#include "foundation/utility/containers/array.h"
#include "foundation/utility/implptr.h"
#include "foundation/utility/uid.h"

// boost headers.
#include "boost/date_time/posix_time/posix_time.hpp"

// Standard headers.
#include <cstddef>

// Forward declarations.
namespace foundation    { class Dictionary; }

//
// On Windows, define FOUNDATIONDLL to __declspec(dllexport) when building the DLL
// and to __declspec(dllimport) when building an application using the DLL.
// Other platforms don't use this export mechanism and the symbol FOUNDATIONDLL is
// defined to evaluate to nothing.
//

#ifndef FOUNDATIONDLL
#ifdef _WIN32
#ifdef APPLESEED_FOUNDATION_EXPORTS
#define FOUNDATIONDLL __declspec(dllexport)
#else
#define FOUNDATIONDLL __declspec(dllimport)
#endif
#else
#define FOUNDATIONDLL
#endif
#endif

namespace foundation
{

struct BenchmarkDataPoint
{
    boost::posix_time::ptime    m_date;
    double                      m_ticks;

    BenchmarkDataPoint()
    {
    }

    BenchmarkDataPoint(const boost::posix_time::ptime& date, const double ticks)
      : m_date(date)
      , m_ticks(ticks)
    {
    }
};

DECLARE_ARRAY(BenchmarkSerie, BenchmarkDataPoint);

class FOUNDATIONDLL BenchmarkAggregator
  : public NonCopyable
{
  public:
    BenchmarkAggregator();

    bool scan_file(const char* path);

    void scan_directory(const char* path);

    const Dictionary& get_benchmarks() const;

    const BenchmarkSerie& get_serie(const UniqueID case_uid) const;

  private:
    PIMPL(BenchmarkAggregator);
};

}       // namespace foundation

#endif  // !APPLESEED_FOUNDATION_UTILITY_BENCHMARK_BENCHMARKAGGREGATOR_H