
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2012 Francois Beaune, Jupiter Jazz
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
#include "imagebasedlighting.h"

// appleseed.renderer headers.
#include "renderer/kernel/lighting/tracer.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/environmentedf/environmentedf.h"
#include "renderer/modeling/input/inputevaluator.h"

// appleseed.foundation headers.
#include "foundation/math/mis.h"

using namespace foundation;

namespace renderer
{

namespace
{
    //
    // Compute image-based lighting via BSDF sampling.
    //

    void compute_ibl_bsdf_sampling(
        SamplingContext&            sampling_context,
        const ShadingContext&       shading_context,
        const EnvironmentEDF&       environment_edf,
        const Vector3d&             point,
        const Vector3d&             geometric_normal,
        const Basis3d&              shading_basis,
        const double                time,
        const Vector3d&             outgoing,
        const BSDF&                 bsdf,
        const void*                 bsdf_data,
        const size_t                bsdf_sample_count,
        const size_t                env_sample_count,
        Spectrum&                   radiance,
        const ShadingPoint*         parent_shading_point)
    {
        radiance.set(0.0f);

        for (size_t i = 0; i < bsdf_sample_count; ++i)
        {
            // Sample the BSDF.
            Vector3d incoming;
            Spectrum bsdf_value;
            double bsdf_prob;
            BSDF::Mode bsdf_mode;
            bsdf.sample(
                sampling_context,
                bsdf_data,
                false,              // not adjoint
                true,               // multiply by |cos(incoming, normal)|
                geometric_normal,
                shading_basis,
                outgoing,
                incoming,
                bsdf_value,
                bsdf_prob,
                bsdf_mode);

            // Ignore glossy/specular components: they must be handled by the parent.
            // See Physically Based Rendering vol. 1 page 732.
            if (bsdf_mode != BSDF::Diffuse)
                continue;

            // Since we're limiting ourselves to the diffuse case, the BSDF should not be a Dirac delta.
            assert(bsdf_prob > 0.0);

            // Compute the transmission factor toward the incoming direction.
            Tracer tracer(
                shading_context.get_intersector(),
                shading_context.get_texture_cache());
            double transmission;
            const ShadingPoint& shading_point =
                tracer.trace(
                    sampling_context,
                    point,
                    incoming,
                    time,
                    transmission,
                    parent_shading_point);

            // Discard occluded samples.
            if (shading_point.hit())
                continue;

            // Evaluate the environment's EDF.
            InputEvaluator input_evaluator(shading_context.get_texture_cache());
            Spectrum env_value;
            double env_prob;
            environment_edf.evaluate(
                input_evaluator,
                incoming,
                env_value,
                env_prob);

            // Compute MIS weight.
            const double mis_weight =
                bsdf_prob == BSDF::DiracDelta
                    ? 1.0
                    : mis_power2(
                          bsdf_sample_count * bsdf_prob,
                          env_sample_count * env_prob);

            // Add the contribution of this sample to the illumination.
            env_value *= static_cast<float>(transmission * mis_weight / bsdf_prob);
            env_value *= bsdf_value;
            radiance += env_value;
        }

        if (bsdf_sample_count > 1)
            radiance /= static_cast<float>(bsdf_sample_count);
    }


    //
    // Compute image-based lighting via environment sampling.
    //

    void compute_ibl_environment_sampling(
        SamplingContext&            sampling_context,
        const ShadingContext&       shading_context,
        const EnvironmentEDF&       environment_edf,
        const Vector3d&             point,
        const Vector3d&             geometric_normal,
        const Basis3d&              shading_basis,
        const double                time,
        const Vector3d&             outgoing,
        const BSDF&                 bsdf,
        const void*                 bsdf_data,
        const size_t                bsdf_sample_count,
        const size_t                env_sample_count,
        Spectrum&                   radiance,
        const ShadingPoint*         parent_shading_point)
    {
        radiance.set(0.0f);

        // todo: if we had a way to know that a BSDF is purely specular, we could
        // immediately return black here since there will be no contribution from
        // such a BSDF.

        sampling_context.split_in_place(2, env_sample_count);

        for (size_t i = 0; i < env_sample_count; ++i)
        {
            // Generate a uniform sample in [0,1)^2.
            const Vector2d s = sampling_context.next_vector2<2>();

            // Sample the environment.
            InputEvaluator input_evaluator(shading_context.get_texture_cache());
            Vector3d incoming;
            Spectrum env_value;
            double env_prob;
            environment_edf.sample(
                input_evaluator,
                s,
                incoming,
                env_value,
                env_prob);

            // Compute the transmission factor toward the incoming direction.
            SamplingContext child_sampling_context(sampling_context);
            Tracer tracer(
                shading_context.get_intersector(),
                shading_context.get_texture_cache());
            double transmission;
            const ShadingPoint& shading_point =
                tracer.trace(
                    child_sampling_context,
                    point,
                    incoming,
                    time,
                    transmission,
                    parent_shading_point);

            // Discard occluded samples.
            if (shading_point.hit())
                continue;

            // Evaluate the BSDF.
            Spectrum bsdf_value;
            double bsdf_prob;
            const bool bsdf_defined =
                bsdf.evaluate(
                    bsdf_data,
                    false,              // not adjoint
                    true,               // multiply by |cos(incoming, normal)|
                    geometric_normal,
                    shading_basis,
                    outgoing,
                    incoming,
                    bsdf_value,
                    &bsdf_prob);
            if (!bsdf_defined)
                continue;

            // Compute MIS weight.
            const double mis_weight =
                mis_power2(
                    env_sample_count * env_prob,
                    bsdf_sample_count * bsdf_prob);

            // Add the contribution of this sample to the illumination.
            env_value *= static_cast<float>(transmission / env_prob * mis_weight);
            env_value *= bsdf_value;
            radiance += env_value;
        }

        if (env_sample_count > 1)
            radiance /= static_cast<float>(env_sample_count);
    }
}


//
// Compute image-based lighting at a given point in space.
//

void compute_image_based_lighting(
    SamplingContext&            sampling_context,
    const ShadingContext&       shading_context,
    const EnvironmentEDF&       environment_edf,
    const Vector3d&             point,
    const Vector3d&             geometric_normal,
    const Basis3d&              shading_basis,
    const double                time,
    const Vector3d&             outgoing,
    const BSDF&                 bsdf,
    const void*                 bsdf_data,
    const size_t                bsdf_sample_count,
    const size_t                env_sample_count,
    Spectrum&                   radiance,
    const ShadingPoint*         parent_shading_point)
{
    assert(is_normalized(geometric_normal));
    assert(is_normalized(outgoing));

    // Compute IBL by sampling the BSDF.
    compute_ibl_bsdf_sampling(
        sampling_context,
        shading_context,
        environment_edf,
        point,
        geometric_normal,
        shading_basis,
        time,
        outgoing,
        bsdf,
        bsdf_data,
        bsdf_sample_count,
        env_sample_count,
        radiance,
        parent_shading_point);

    // Compute IBL by sampling the environment.
    Spectrum radiance_env_sampling;
    compute_ibl_environment_sampling(
        sampling_context,
        shading_context,
        environment_edf,
        point,
        geometric_normal,
        shading_basis,
        time,
        outgoing,
        bsdf,
        bsdf_data,
        bsdf_sample_count,
        env_sample_count,
        radiance_env_sampling,
        parent_shading_point);
    radiance += radiance_env_sampling;
}

}   // namespace renderer
