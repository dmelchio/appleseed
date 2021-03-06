
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
#include "spotlight.h"

// appleseed.renderer headers.
#include "renderer/global/globaltypes.h"
#include "renderer/modeling/input/inputarray.h"
#include "renderer/modeling/input/inputevaluator.h"
#include "renderer/modeling/input/source.h"
#include "renderer/utility/paramarray.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/matrix.h"
#include "foundation/math/sampling.h"
#include "foundation/math/scalar.h"
#include "foundation/math/transform.h"
#include "foundation/math/vector.h"
#include "foundation/utility/containers/dictionary.h"
#include "foundation/utility/containers/specializedarrays.h"

// Standard headers.
#include <cmath>

// Forward declarations.
namespace renderer  { class Assembly; }
namespace renderer  { class Project; }

using namespace foundation;
using namespace std;

namespace renderer
{

namespace
{
    //
    // Spot light.
    //

    const char* Model = "spot_light";

    class SpotLight
      : public Light
    {
      public:
        SpotLight(
            const char*         name,
            const ParamArray&   params)
          : Light(name, params)
        {
            m_inputs.declare("exitance", InputFormatSpectrum);
            m_inputs.declare("exitance_multiplier", InputFormatScalar, "1.0");
        }

        virtual void release() override
        {
            delete this;
        }

        virtual const char* get_model() const override
        {
            return Model;
        }

        virtual bool on_frame_begin(
            const Project&      project,
            const Assembly&     assembly) override
        {
            if (!Light::on_frame_begin(project, assembly))
                return false;

            m_exitance_source = m_inputs.source("exitance");
            m_exitance_multiplier_source = m_inputs.source("exitance_multiplier");

            check_non_zero_exitance(m_exitance_source, m_exitance_multiplier_source);

            const double inner_half_angle = deg_to_rad(m_params.get_required<double>("inner_angle", 20.0) / 2.0);
            const double outer_half_angle = deg_to_rad(m_params.get_required<double>("outer_angle", 30.0) / 2.0);
            const double tilt_angle = deg_to_rad(m_params.get_optional<double>("tilt_angle", 0.0));

            m_cos_inner_half_angle = cos(inner_half_angle);
            m_cos_outer_half_angle = cos(outer_half_angle);
            m_rcp_screen_half_size = 1.0 / tan(outer_half_angle);

            m_transform = Transformd(Matrix4d::rotation(Vector3d(1.0, 0.0, 0.0), -HalfPi)) * get_transform();
            m_axis = normalize(m_transform.vector_to_parent(Vector3d(0.0, 1.0, 0.0)));

            const Vector3d up = m_transform.vector_to_parent(Vector3d(sin(tilt_angle), 0.0, cos(tilt_angle)));
            const Vector3d v = -m_axis;
            const Vector3d u = normalize(cross(up, v));
            const Vector3d n = cross(v, u);

            m_screen_basis.build(n, u, v);

            return true;
        }

        void evaluate_inputs(
            InputEvaluator&     input_evaluator,
            const Vector3d&     outgoing) const override
        {
            const double cos_theta = dot(outgoing, m_axis);
            const Vector3d d = outgoing / cos_theta - m_axis;
            const double x = dot(d, m_screen_basis.get_tangent_u()) * m_rcp_screen_half_size;
            const double y = dot(d, m_screen_basis.get_normal()) * m_rcp_screen_half_size;
            const Vector2d uv(0.5 * (x + 1.0), 0.5 * (y + 1.0));

            input_evaluator.evaluate(m_inputs, uv);
        }

        virtual void sample(
            const void*         data,
            const Vector2d&     s,
            Vector3d&           outgoing,
            Spectrum&           value,
            double&             probability) const override
        {
            const Vector3d wo = sample_cone_uniform(s, m_cos_outer_half_angle);
            outgoing = m_transform.vector_to_parent(wo);

            compute_exitance(data, wo.y, value);

            probability = sample_cone_uniform_pdf(m_cos_outer_half_angle);
        }

        virtual void evaluate(
            const void*         data,
            const Vector3d&     outgoing,
            Spectrum&           value) const override
        {
            const double cos_theta = dot(outgoing, m_axis);

            if (cos_theta > m_cos_outer_half_angle)
                compute_exitance(data, cos_theta, value);
            else value.set(0.0f);
        }

        virtual void evaluate(
            const void*         data,
            const Vector3d&     outgoing,
            Spectrum&           value,
            double&             probability) const override
        {
            const double cos_theta = dot(outgoing, m_axis);

            if (cos_theta > m_cos_outer_half_angle)
            {
                compute_exitance(data, cos_theta, value);
                probability = sample_cone_uniform_pdf(m_cos_outer_half_angle);
            }
            else
            {
                value.set(0.0f);
                probability = 0.0f;
            }
        }

        virtual double evaluate_pdf(
            const void*         data,
            const Vector3d&     outgoing) const override
        {
            const double cos_theta = dot(outgoing, m_axis);

            return
                cos_theta > m_cos_outer_half_angle
                    ? sample_cone_uniform_pdf(m_cos_outer_half_angle)
                    : 0.0;
        }

      private:
        struct InputValues
        {
            Spectrum    m_exitance;             // radiant exitance, in W.m^-2
            Alpha       m_exitance_alpha;       // unused
            double      m_exitance_multiplier;  // radiant exitance multiplier
        };

        const Source*   m_exitance_source;
        const Source*   m_exitance_multiplier_source;

        double          m_cos_inner_half_angle;
        double          m_cos_outer_half_angle;
        double          m_rcp_screen_half_size;

        Transformd      m_transform;
        Vector3d        m_axis;                 // world space
        Basis3d         m_screen_basis;         // world space

        void compute_exitance(
            const void*         data,
            const double        cos_theta,
            Spectrum&           exitance) const
        {
            assert(cos_theta > m_cos_outer_half_angle);

            const InputValues* values = static_cast<const InputValues*>(data);
            exitance = values->m_exitance;
            exitance *= static_cast<float>(values->m_exitance_multiplier);

            if (cos_theta < m_cos_inner_half_angle)
            {
                exitance *=
                    static_cast<float>(
                        smoothstep(m_cos_outer_half_angle, m_cos_inner_half_angle, cos_theta));
            }
        }
    };
}


//
// SpotLightFactory class implementation.
//

const char* SpotLightFactory::get_model() const
{
    return Model;
}

const char* SpotLightFactory::get_human_readable_model() const
{
    return "Spot Light";
}

DictionaryArray SpotLightFactory::get_widget_definitions() const
{
    DictionaryArray definitions;

    definitions.push_back(
        Dictionary()
            .insert("name", "exitance")
            .insert("label", "Exitance")
            .insert("widget", "entity_picker")
            .insert("entity_types",
                Dictionary()
                    .insert("color", "Colors")
                    .insert("texture_instance", "Textures"))
            .insert("use", "required")
            .insert("default", ""));

    definitions.push_back(
        Dictionary()
            .insert("name", "exitance_multiplier")
            .insert("label", "Exitance Multiplier")
            .insert("widget", "entity_picker")
            .insert("entity_types",
                Dictionary().insert("texture_instance", "Textures"))
            .insert("use", "optional")
            .insert("default", "1.0"));

    definitions.push_back(
        Dictionary()
            .insert("name", "inner_angle")
            .insert("label", "Inner Angle")
            .insert("widget", "text_box")
            .insert("use", "required")
            .insert("default", "20.0"));

    definitions.push_back(
        Dictionary()
            .insert("name", "outer_angle")
            .insert("label", "Outer Angle")
            .insert("widget", "text_box")
            .insert("use", "required")
            .insert("default", "30.0"));

    definitions.push_back(
        Dictionary()
            .insert("name", "tilt_angle")
            .insert("label", "Tilt Angle")
            .insert("widget", "text_box")
            .insert("use", "optional")
            .insert("default", "0.0"));

    return definitions;
}

auto_release_ptr<Light> SpotLightFactory::create(
    const char*         name,
    const ParamArray&   params) const
{
    return auto_release_ptr<Light>(new SpotLight(name, params));
}

}   // namespace renderer
