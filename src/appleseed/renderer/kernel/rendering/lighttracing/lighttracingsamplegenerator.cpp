
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2011 Francois Beaune
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
#include "lighttracingsamplegenerator.h"

// appleseed.renderer headers.
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/lighting/lightsampler.h"
#include "renderer/kernel/lighting/pathtracer.h"
#include "renderer/kernel/lighting/transmission.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/rendering/sample.h"
#include "renderer/kernel/rendering/samplegeneratorbase.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/kernel/shading/shadingresult.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/camera/camera.h"
#include "renderer/modeling/edf/edf.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/input/inputevaluator.h"

// appleseed.foundation headers.
#include "foundation/image/spectrum.h"
#include "foundation/math/population.h"
#include "foundation/math/qmc.h"
#include "foundation/math/rng.h"
#include "foundation/math/vector.h"
#include "foundation/utility/memory.h"

// Forward declarations.
namespace foundation    { class LightingConditions; }

using namespace foundation;

namespace renderer
{

namespace
{
    //
    // LightTracingSampleGenerator class implementation.
    //

    class LightTracingSampleGenerator
      : public SampleGeneratorBase
    {
      public:
        LightTracingSampleGenerator(
            const Scene&            scene,
            const Frame&            frame,
            const TraceContext&     trace_context,
            const LightSampler&     light_sampler,
            const size_t            generator_index,
            const size_t            generator_count,
            const ParamArray&       params)
          : SampleGeneratorBase(generator_index, generator_count)
          , m_params(params)
          , m_scene(scene)
          , m_frame(frame)
          , m_lighting_conditions(frame.get_lighting_conditions())
          , m_light_sampler(light_sampler)
          , m_intersector(trace_context, true, m_params.m_report_self_intersections)
          , m_texture_cache(scene, m_params.m_texture_cache_size)
        {
        }

        virtual void release()
        {
            delete this;
        }

        virtual void reset()
        {
            SampleGeneratorBase::reset();
            m_rng = MersenneTwister();
        }

      private:
        struct Parameters
        {
            const size_t    m_texture_cache_size;           // size in bytes of the texture cache
            const bool      m_report_self_intersections;
            const size_t    m_minimum_path_length;          // minimum path length before Russian Roulette is used

            explicit Parameters(const ParamArray& params)
              : m_texture_cache_size(params.get_optional<size_t>("texture_cache_size", 16 * 1024 * 1024))
              , m_report_self_intersections(params.get_optional<bool>("report_self_intersections", false))
              , m_minimum_path_length(params.get_optional<size_t>("minimum_path_length", 3))
            {
            }
        };

        struct Statistics
        {
            size_t              m_path_count;
            Population<size_t>  m_path_length;

            Statistics()
              : m_path_count(0)
            {
            }
        };

        class PathVisitor
        {
          public:
            PathVisitor(
                const Scene&                scene,
                const LightingConditions&   lighting_conditions,
                const Intersector&          intersector,
                TextureCache&               texture_cache,
                SampleVector&               samples,
                const Spectrum&             initial_alpha)
              : m_camera(*scene.get_camera())
              , m_lighting_conditions(lighting_conditions)
              , m_shading_context(intersector, texture_cache)
              , m_samples(samples)
              , m_sample_count(0)
              , m_alpha(initial_alpha)
            {
                // Compute the world space position and direction of the camera.
                m_camera_position = m_camera.get_transform().transform_point_to_parent(Vector3d(0.0));
                m_camera_direction = m_camera.get_transform().transform_vector_to_parent(Vector3d(0.0, 0.0, -1.0));
                assert(is_normalized(m_camera_direction));

                // Compute the area (in m^2) of the camera film.
                const Vector2d& film_dimensions = m_camera.get_film_dimensions();
                m_rcp_film_area = 1.0 / (film_dimensions[0] * film_dimensions[1]);

                // Cache the focal length.
                m_focal_length = m_camera.get_focal_length();
            }

            size_t get_sample_count() const
            {
                return m_sample_count;
            }

            void visit_light_vertex(
                SamplingContext&            sampling_context,
                const Vector3d&             vertex_position_world)
            {
                // Transform the vertex position to camera space.
                const Vector3d vertex_position_camera =
                    m_camera.get_transform().transform_point_to_local(vertex_position_world);

                // Compute the position of the vertex on the image plane.
                const Vector2d sample_position_ndc = m_camera.project(vertex_position_camera);

                // Reject vertices that don't belong on the image plane of the camera.
                if (sample_position_ndc[0] < 0.0 || sample_position_ndc[0] >= 1.0 ||
                    sample_position_ndc[1] < 0.0 || sample_position_ndc[1] >= 1.0)
                    return;

                // Compute the transmission factor between this vertex and the camera.
                // Prevent self-intersections by letting the ray originate from the camera.
                const double transmission =
                    compute_transmission_between(
                        sampling_context,
                        m_shading_context,
                        m_camera_position,
                        vertex_position_world);

                // Reject vertices not directly visible from the camera.
                if (transmission == 0.0)
                    return;

                // Compute the vertex-to-camera direction vector.
                Vector3d vertex_to_camera = m_camera_position - vertex_position_world;
                const double square_distance = square_norm(vertex_to_camera);
                vertex_to_camera /= sqrt(square_distance);

                // Compute the square distance between the center of pixel and the camera position.
                const double cos_theta = abs(dot(-vertex_to_camera, m_camera_direction));
                const double dist_pixel_to_camera = m_focal_length / cos_theta;

                // Compute the flux-to-radiance factor.
                const double flux_to_radiance = square(dist_pixel_to_camera / cos_theta) * m_rcp_film_area;

                // Compute the geometric term:
                //  * we already know that visibility is 1
                //  * cos(vertex_to_camera, shading_normal) is already accounted for in bsdf_value.
                const double g = cos_theta / square_distance;
                assert(g >= 0.0);

                // Compute the contribution of this sample to the pixel.
                Spectrum radiance = m_alpha;
                radiance *= static_cast<float>(transmission * g * flux_to_radiance);

                // Create a sample for this vertex.
                Sample sample;
                sample.m_position = sample_position_ndc;
                sample.m_color.rgb() =
                    ciexyz_to_linear_rgb(
                        spectrum_to_ciexyz<float>(m_lighting_conditions, radiance));
                sample.m_color[3] = 1.0f;
                m_samples.push_back(sample);
                ++m_sample_count;
            }

            void visit_vertex(
                SamplingContext&            sampling_context,
                const ShadingPoint&         shading_point,
                const Vector3d&             outgoing,           // in this context, toward the light
                const BSDF*                 bsdf,
                const void*                 bsdf_data,
                const BSDF::Mode            bsdf_mode,
                const double                bsdf_prob,
                const Spectrum&             throughput)
            {
                // Retrieve the world space position of this vertex.
                const Vector3d& vertex_position_world = shading_point.get_point();

                // Transform the vertex position to camera space.
                const Vector3d vertex_position_camera =
                    m_camera.get_transform().transform_point_to_local(vertex_position_world);

                // Compute the position of the vertex on the image plane.
                const Vector2d sample_position_ndc = m_camera.project(vertex_position_camera);

                // Reject vertices that don't belong on the image plane of the camera.
                if (sample_position_ndc[0] < 0.0 || sample_position_ndc[0] >= 1.0 ||
                    sample_position_ndc[1] < 0.0 || sample_position_ndc[1] >= 1.0)
                    return;

                // Compute the transmission factor between this vertex and the camera.
                // Prevent self-intersections by letting the ray originate from the camera.
                const double transmission =
                    compute_transmission_between(
                        sampling_context,
                        m_shading_context,
                        m_camera_position,
                        vertex_position_world);

                // Reject vertices not directly visible from the camera.
                if (transmission == 0.0)
                    return;

                // Compute the vertex-to-camera direction vector.
                Vector3d vertex_to_camera = m_camera_position - vertex_position_world;
                const double square_distance = square_norm(vertex_to_camera);
                vertex_to_camera /= sqrt(square_distance);

                // Retrieve the shading and geometric normals at the vertex.
                const Vector3d& shading_normal = shading_point.get_shading_normal();
                Vector3d geometric_normal = shading_point.get_geometric_normal();

                // Make sure the geometric normal is in the same hemisphere as the shading normal.
                if (dot(shading_normal, geometric_normal) < 0.0)
                    geometric_normal = -geometric_normal;

                // Evaluate the BSDF at the vertex position.
                Spectrum bsdf_value;
                bsdf->evaluate(
                    bsdf_data,
                    true,
                    geometric_normal,
                    shading_point.get_shading_basis(),
                    outgoing,                           // outgoing
                    vertex_to_camera,                   // incoming
                    bsdf_value);

                // Compute the square distance between the center of pixel and the camera position.
                const double cos_theta = abs(dot(-vertex_to_camera, m_camera_direction));
                const double dist_pixel_to_camera = m_focal_length / cos_theta;

                // Compute the flux-to-radiance factor.
                const double flux_to_radiance = square(dist_pixel_to_camera / cos_theta) * m_rcp_film_area;

                // Compute the geometric term:
                //  * we already know that visibility is 1
                //  * cos(vertex_to_camera, shading_normal) is already accounted for in bsdf_value.
                const double g = cos_theta / square_distance;
                assert(g >= 0.0);

                // Update the particle weight.
                m_alpha *= throughput;

                // Compute the contribution of this sample to the pixel.
                Spectrum radiance = m_alpha;
                radiance *= bsdf_value;
                radiance *= static_cast<float>(transmission * g * flux_to_radiance);

                // Create a sample for this vertex.
                Sample sample;
                sample.m_position = sample_position_ndc;
                sample.m_color.rgb() =
                    ciexyz_to_linear_rgb(
                        spectrum_to_ciexyz<float>(m_lighting_conditions, radiance));
                sample.m_color[3] = 1.0f;
                m_samples.push_back(sample);
                ++m_sample_count;
            }

            void visit_environment(
                const ShadingPoint&         shading_point,
                const Vector3d&             outgoing,
                const Spectrum&             throughput)
            {
            }

          private:
            const Camera&               m_camera;
            const LightingConditions&   m_lighting_conditions;
            const ShadingContext        m_shading_context;
            Vector3d                    m_camera_position;      // camera position in world space
            Vector3d                    m_camera_direction;     // camera direction (gaze) in world space
            double                      m_rcp_film_area;
            double                      m_focal_length;
            SampleVector&               m_samples;
            size_t                      m_sample_count;         // the number of samples added to m_samples
            Spectrum                    m_alpha;                // flux of the current particle (in W)
        };

        const Parameters                m_params;
        Statistics                      m_stats;

        const Scene&                    m_scene;
        const Frame&                    m_frame;
        const LightingConditions&       m_lighting_conditions;

        const LightSampler&             m_light_sampler;
        Intersector                     m_intersector;
        TextureCache                    m_texture_cache;

        MersenneTwister                 m_rng;
        LightSampleVector               m_light_samples;

        virtual size_t generate_samples(
            const size_t                sequence_index,
            SampleVector&               samples)
        {
            // Create a sampling context.
            SamplingContext sampling_context(
                m_rng,
                2,                      // number of dimensions
                0,                      // number of samples
                sequence_index);        // initial instance number

            // Generate a uniform sample in [0,1)^2 that will be used to sample the EDF.
            const Vector2d s = sampling_context.next_vector2<2>();

            // todo: there are possible correlation artifacts since the sampling_context
            // object is forked twice from there: once by the light sampler and once by
            // the path tracer.

            // Get one light sample.
            LightSample light_sample;
            m_light_sampler.sample(sampling_context, light_sample);


            // Evaluate the input values of the EDF of this light sample.
            InputEvaluator edf_input_evaluator(m_texture_cache);
            const void* edf_data =
                edf_input_evaluator.evaluate(
                    light_sample.m_edf->get_inputs(),
                    light_sample.m_input_params);

            // Sample the EDF.
            Vector3d emission_direction;
            Spectrum initial_alpha;
            double emission_direction_probability;
            light_sample.m_edf->sample(
                edf_data,
                light_sample.m_input_params.m_geometric_normal,
                Basis3d(light_sample.m_input_params.m_shading_normal),
                s,
                emission_direction,
                initial_alpha,
                emission_direction_probability);

            // Compute the initial particle weight.
            initial_alpha /=
                static_cast<float>(light_sample.m_probability * emission_direction_probability);

            // Build the light ray.
            const ShadingRay light_ray(
                Intersector::offset(
                    light_sample.m_input_params.m_point,
                    light_sample.m_input_params.m_geometric_normal),
                emission_direction,
                0.0f,
                ~0);

            typedef PathTracer<
                PathVisitor,
                BSDF::Diffuse | BSDF::Glossy | BSDF::Specular,
                true                    // adjoint
            > PathTracer;

            // Build a path tracer.
            PathVisitor path_visitor(
                m_scene,
                m_lighting_conditions,
                m_intersector,
                m_texture_cache,
                samples,
                initial_alpha);
            PathTracer path_tracer(
                path_visitor,
                m_params.m_minimum_path_length);

            // Handle the light vertex separately.
            path_visitor.visit_light_vertex(
                sampling_context,
                light_sample.m_input_params.m_point);

            // Trace the light path.
            const size_t path_length =
                path_tracer.trace(
                    sampling_context,
                    m_intersector,
                    m_texture_cache,
                    light_ray);

            // Update path statistics.
            ++m_stats.m_path_count;
            m_stats.m_path_length.insert(path_length);

            // Return the number of samples generated when tracing this light path.
            return path_visitor.get_sample_count();
        }
    };
}


//
// LightTracingSampleGeneratorFactory class implementation.
//

LightTracingSampleGeneratorFactory::LightTracingSampleGeneratorFactory(
    const Scene&            scene,
    const Frame&            frame,
    const TraceContext&     trace_context,
    const LightSampler&     light_sampler,
    const ParamArray&       params)
  : m_scene(scene)
  , m_frame(frame)
  , m_trace_context(trace_context)
  , m_light_sampler(light_sampler)
  , m_params(params)
{
}

void LightTracingSampleGeneratorFactory::release()
{
    delete this;
}

ISampleGenerator* LightTracingSampleGeneratorFactory::create(
    const size_t            generator_index,
    const size_t            generator_count)
{
    return
        new LightTracingSampleGenerator(
            m_scene,
            m_frame,
            m_trace_context,
            m_light_sampler,
            generator_index,
            generator_count,
            m_params);
}

}   // namespace renderer