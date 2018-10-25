#include "GLFW/glfw3.h"
#if defined(__linux__)
  #define GLFW_EXPOSE_NATIVE_X11
#elif defined(_WIN32)
  #define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "GLFW/glfw3native.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "vgfx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using float2   = glm::vec2;
using float3   = glm::vec3;
using float4   = glm::vec4;
using float4x4 = glm::mat4;


const char*         k_app_name = "13_SimpleTessellationShader";
const uint32_t      k_image_count = 1;
const std::string   k_asset_dir = "../samples/assets/";

tr_renderer*        m_renderer = nullptr;
tr_cmd_pool*        m_cmd_pool = nullptr;
tr_cmd**            m_cmds = nullptr;

tr_buffer*          m_color_vertex_buffer = nullptr;
uint32_t            m_color_vertex_count = 0;
tr_shader_program*  m_color_shader = nullptr;
tr_descriptor_set*  m_color_desc_set = nullptr;
tr_pipeline*        m_color_pipeline = nullptr;
tr_buffer*          m_color_uniform_buffer = nullptr;

tr_buffer*          m_isoline_vertex_buffer = nullptr;
uint32_t            m_isoline_vertex_count = 0;
tr_shader_program*  m_isoline_shader = nullptr;
tr_descriptor_set*  m_isoline_desc_set = nullptr;
tr_pipeline*        m_isoline_pipeline = nullptr;
tr_buffer*          m_isoline_uniform_buffer = nullptr;

uint32_t            s_window_width;
uint32_t            s_window_height;
uint64_t            s_frame_count = 0;

void init_tiny_renderer(GLFWwindow* window)
{
    std::vector<std::string> instance_layers = {
#if defined(_DEBUG)
        "VK_LAYER_LUNARG_standard_validation",
#endif
    };

    std::vector<std::string> device_layers;

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    s_window_width = (uint32_t)width;
    s_window_height = (uint32_t)height;

    tr_renderer_settings settings = {0};
#if defined(TINY_RENDERER_DX)
    settings.api = tr_api_d3d12;
#endif

#if defined(__linux__)
    settings.handle.connection              = XGetXCBConnection(glfwGetX11Display());
    settings.handle.window                  = glfwGetX11Window(window);
#elif defined(_WIN32)
    settings.handle.hinstance               = ::GetModuleHandle(NULL);
    settings.handle.hwnd                    = glfwGetWin32Window(window);
#endif
    settings.width                          = s_window_width;
    settings.height                         = s_window_height;
    settings.swapchain.image_count          = k_image_count;
    settings.swapchain.sample_count         = tr_sample_count_8;
    settings.swapchain.color_format         = tr_format_b8g8r8a8_unorm;
    settings.swapchain.depth_stencil_format = tr_format_d32_float;
    settings.swapchain.depth_stencil_clear_value.depth    = 1.0f;
    settings.swapchain.depth_stencil_clear_value.stencil  = 255;
    settings.log_fn                         = renderer_log;
#if defined(TINY_RENDERER_VK)
    settings.vk_debug_fn                    = vulkan_debug;
    settings.instance_layers          = instance_layers;
#endif
    tr_create_renderer(k_app_name, &settings, &m_renderer);

    tr_create_cmd_pool(m_renderer, m_renderer->graphics_queue, false, &m_cmd_pool);
    tr_create_cmd_n(m_cmd_pool, false, k_image_count, &m_cmds);
    
#if defined(TINY_RENDERER_VK)
    auto vert = load_file(k_asset_dir + "simple_tess_color.vs.spv");
    auto frag = load_file(k_asset_dir + "simple_tess_color.ps.spv");
    tr_create_shader_program(m_renderer, 
                             (uint32_t)vert.size(), (uint32_t*)(vert.data()), "VSMain",
                             (uint32_t)frag.size(), (uint32_t*)(frag.data()), "PSMain",
                             &m_color_shader);

         vert = load_file(k_asset_dir + "simple_tess_isoline.vs.spv");
    auto tesc = load_file(k_asset_dir + "simple_tess_isoline.hs.spv");
    auto tese = load_file(k_asset_dir + "simple_tess_isoline.ds.spv");
         frag = load_file(k_asset_dir + "simple_tess_isoline.ps.spv");
    tr_create_shader_program_n(m_renderer, 
                             (uint32_t)vert.size(), (uint32_t*)(vert.data()), "VSMain",
                             (uint32_t)tesc.size(), (uint32_t*)(tesc.data()), "HSMain", 
                             (uint32_t)tese.size(), (uint32_t*)(tese.data()), "DSMain", 
                             0, nullptr, nullptr,
                             (uint32_t)frag.size(), (uint32_t*)(frag.data()), "PSMain",
                             0, nullptr, nullptr,
                             &m_isoline_shader);
#elif defined(TINY_RENDERER_DX)
    auto hlsl = load_file(k_asset_dir + "simple_tess_color.hlsl");
    tr_create_shader_program(m_renderer,
                             (uint32_t)hlsl.size(), hlsl.data(), "VSMain", 
                             (uint32_t)hlsl.size(), hlsl.data(), "PSMain", 
                             &m_color_shader);

    hlsl = load_file(k_asset_dir + "simple_tess_isoline.hlsl");
    tr_create_shader_program_n(m_renderer,
                               (uint32_t)hlsl.size(), hlsl.data(), "VSMain", 
                               (uint32_t)hlsl.size(), hlsl.data(), "HSMain", 
                               (uint32_t)hlsl.size(), hlsl.data(), "DSMain", 
                               0, nullptr, nullptr,
                               (uint32_t)hlsl.size(), hlsl.data(), "PSMain", 
                               0, nullptr, nullptr,
                               &m_isoline_shader);
#endif

    std::vector<tr_descriptor> descriptors(1);
    descriptors[0].type          = tr_descriptor_type_uniform_buffer_cbv;
    descriptors[0].count         = 1;
    descriptors[0].binding       = 0;
    descriptors[0].shader_stages = (tr_shader_stage)(tr_shader_stage_vert | tr_shader_stage_tesc | tr_shader_stage_tese | tr_shader_stage_frag);
    tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_color_desc_set);
    tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(), &m_isoline_desc_set);

    tr_vertex_layout vertex_layout = {};
    vertex_layout.attrib_count = 1;
    vertex_layout.attribs[0].semantic = tr_semantic_position;
    vertex_layout.attribs[0].format   = tr_format_r32g32b32a32_float;
    vertex_layout.attribs[0].binding  = 0;
    vertex_layout.attribs[0].location = 0;
    vertex_layout.attribs[0].offset   = 0;
    tr_pipeline_settings pipeline_settings = {tr_primitive_topo_line_strip};
    tr_create_pipeline(m_renderer, m_color_shader, &vertex_layout, m_color_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_color_pipeline);

    pipeline_settings = {tr_primitive_topo_4_point_patch};
    tr_create_pipeline(m_renderer, m_isoline_shader, &vertex_layout, m_isoline_desc_set, m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_isoline_pipeline);


    float4 positions[13] = {
      { -0.25f,  1.00f,  0.0f, 1.0f }, //  0
      { -0.50f,  0.75f,  0.0f, 1.0f }, //  1
      { -0.50f,  0.25f,  0.0f, 1.0f }, //  2
      {  0.50f, -0.25f,  0.0f, 1.0f }, //  3
      {  0.50f, -0.75f,  0.0f, 1.0f }, //  4
      {  0.25f, -1.00f,  0.0f, 1.0f }, //  5
      { -0.25f, -1.00f,  0.0f, 1.0f }, //  6
      { -0.50f, -0.75f,  0.0f, 1.0f }, //  7
      { -0.50f, -0.25f,  0.0f, 1.0f }, //  8
      {  0.50f,  0.25f,  0.0f, 1.0f }, //  9
      {  0.50f,  0.75f,  0.0f, 1.0f }, // 10
      {  0.25f,  1.00f,  0.0f, 1.0f }, // 11
      { -0.25f,  1.00f,  0.0f, 1.0f }, // 12
    };

    struct Vertex {
      float4  position;
    };

    // Color vertex data
    {
      std::vector<Vertex> vertex_data = {
        { positions[ 0] },
        { positions[ 1] },
        { positions[ 2] },
        { positions[ 3] },
        { positions[ 4] },
        { positions[ 5] },
        { positions[ 6] },
        { positions[ 7] },
        { positions[ 8] },
        { positions[ 9] },
        { positions[10] },
        { positions[11] },
        { positions[12] },
      };

      uint32_t vertex_stride    = sizeof(Vertex);
      uint32_t vertex_count     = (uint32_t)vertex_data.size();
      uint32_t vertex_data_size = vertex_stride * vertex_count;
      tr_create_vertex_buffer(m_renderer, vertex_data_size, true, vertex_stride, &m_color_vertex_buffer);
      memcpy(m_color_vertex_buffer->cpu_mapped_address, vertex_data.data(), vertex_data_size);
      m_color_vertex_count = (uint32_t)vertex_data.size();
    }

    // Isoline vertex data
    {
      std::vector<Vertex> vertex_data = {
        // Patch 0
        { positions[ 0] },
        { positions[ 1] },
        { positions[ 2] },      
        { positions[ 3] },
        // Patch 1
        { positions[ 1] },
        { positions[ 2] },
        { positions[ 3] },      
        { positions[ 4] },
        // Patch 2
        { positions[ 2] },
        { positions[ 3] },
        { positions[ 4] },      
        { positions[ 5] },
        // Patch 3
        { positions[ 3] },
        { positions[ 4] },
        { positions[ 5] },      
        { positions[ 6] },
        // Patch 4
        { positions[ 4] },
        { positions[ 5] },
        { positions[ 6] },      
        { positions[ 7] },
        // Patch 5
        { positions[ 5] },
        { positions[ 6] },
        { positions[ 7] },      
        { positions[ 8] },
        // Patch 6
        { positions[ 6] },
        { positions[ 7] },
        { positions[ 8] },      
        { positions[ 9] },
        // Patch 7
        { positions[ 7] },
        { positions[ 8] },
        { positions[ 9] },      
        { positions[10] },
        // Patch 8
        { positions[ 8] },
        { positions[ 9] },
        { positions[10] },      
        { positions[11] },
        // Patch 9
        { positions[ 9] },
        { positions[10] },
        { positions[11] },      
        { positions[12] },
        // Patch 10
        { positions[10] },
        { positions[11] },
        { positions[ 0] },
        { positions[ 1] },
        // Patch 11
        { positions[11] },
        { positions[ 0] },
        { positions[ 1] },      
        { positions[ 2] },
      };

      uint32_t vertex_stride   = sizeof(Vertex);
      uint32_t vertex_count    = (uint32_t)vertex_data.size();
      uint32_t vertex_data_size = vertex_stride * vertex_count;
      tr_create_vertex_buffer(m_renderer, vertex_data_size, true, vertex_stride, &m_isoline_vertex_buffer);
      memcpy(m_isoline_vertex_buffer->cpu_mapped_address, vertex_data.data(), vertex_data_size);
      m_isoline_vertex_count = (uint32_t)vertex_data.size();
    }

    uint32_t ubo_size = sizeof(float4x4)  // float4x4  model_view_matrix
                      + sizeof(float4x4)  // float4x4  proj_matrix
                      + sizeof(float3);   // float3    color
    tr_create_uniform_buffer(m_renderer, ubo_size, true, &m_color_uniform_buffer);

    tr_create_uniform_buffer(m_renderer, ubo_size, true, &m_isoline_uniform_buffer);

    m_color_desc_set->descriptors[0].uniform_buffers[0] = m_color_uniform_buffer;
    tr_update_descriptor_set(m_renderer, m_color_desc_set);

    m_isoline_desc_set->descriptors[0].uniform_buffers[0] = m_isoline_uniform_buffer;
    tr_update_descriptor_set(m_renderer, m_isoline_desc_set);
}

void destroy_tiny_renderer()
{
    tr_destroy_renderer(m_renderer);
}

void draw_frame()
{
    uint32_t frameIdx = s_frame_count % m_renderer->settings.swapchain.image_count;

    tr_fence* image_acquired_fence = m_renderer->image_acquired_fences[frameIdx];
    tr_semaphore* image_acquired_semaphore = m_renderer->image_acquired_semaphores[frameIdx];
    tr_semaphore* render_complete_semaphores = m_renderer->render_complete_semaphores[frameIdx];

    tr_acquire_next_image(m_renderer, image_acquired_semaphore, image_acquired_fence);

    uint32_t swapchain_image_index = m_renderer->swapchain_image_index;
    tr_render_target* render_target = m_renderer->swapchain_render_targets[swapchain_image_index];

    float4x4 view  = glm::lookAt(float3(0, 0, 2),  float3(0, 0, 0), float3(0, 1, 0));                               
    float4x4 proj  = glm::perspective(glm::radians(60.0f), (float)s_window_width / (float)s_window_height, 0.1f, 10000.0f);
    float4x4 model = glm::translate(float3(-1, 0, 0));

    // Color constant buffer
    {
      struct cbuffer {
        // Type are padded for alignment
        float4x4  model_view_matrix;  // float4x4  model_view_matrix
        float4x4  proj_matrix;        // float4x4  proj_matrix
        float4    color;              // float4    color
      };

      cbuffer buffer = {};
      // This is intentional, same shader binary is used for both pipelines.
      // Color pipeline gets MVP stuffed into MV.
      buffer.model_view_matrix = proj * view * model;
      buffer.color = float4(1, 1, 0, 0);
      memcpy(m_color_uniform_buffer->cpu_mapped_address, &buffer, sizeof(buffer));
    }

    // Isoline constant buffer
    model = glm::translate(float3(1, 0, 0));
    {
      struct cbuffer {
        // Type are padded for alignment
        float4x4  model_view_matrix;  // float4x4  model_view_matrix
        float4x4  proj_matrix;        // float4x4  proj_matrix
        float4    color;              // float4    color
      };

      cbuffer buffer = {};
      buffer.model_view_matrix = view * model;
      buffer.proj_matrix = proj;
      buffer.color = float4(1, 1, 0, 0);
      memcpy(m_isoline_uniform_buffer->cpu_mapped_address, &buffer, sizeof(buffer));
    }

    tr_cmd* cmd = m_cmds[frameIdx];
    tr_begin_cmd(cmd);
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_present, tr_texture_usage_color_attachment); 
    tr_cmd_depth_stencil_transition(cmd, render_target, tr_texture_usage_sampled_image, tr_texture_usage_depth_stencil_attachment);
    tr_cmd_set_viewport(cmd, 0, 0, (float)s_window_width, (float)s_window_height, 0.0f, 1.0f);
    tr_cmd_set_scissor(cmd, 0, 0, s_window_width, s_window_height);
    tr_cmd_begin_render(cmd, render_target);
    tr_clear_value color_clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    tr_cmd_clear_color_attachment(cmd, 0, &color_clear_value);
    tr_clear_value depth_stencil_clear_value = { 0 };
    depth_stencil_clear_value.depth = 1.0f;
    depth_stencil_clear_value.stencil = 255;
    tr_cmd_clear_depth_stencil_attachment(cmd, &depth_stencil_clear_value);
    // Has no effect on D3D12
    tr_cmd_set_line_width(cmd, 1.0f);
    // Color 
    {
      tr_cmd_bind_pipeline(cmd, m_color_pipeline);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_color_vertex_buffer);
      tr_cmd_bind_descriptor_sets(cmd, m_color_pipeline, m_color_desc_set);
      tr_cmd_draw(cmd, m_color_vertex_count, 0);
    }
    // Isoline
    {
      tr_cmd_bind_pipeline(cmd, m_isoline_pipeline);
      tr_cmd_bind_vertex_buffers(cmd, 1, &m_isoline_vertex_buffer);
      tr_cmd_bind_descriptor_sets(cmd, m_isoline_pipeline, m_isoline_desc_set);
      tr_cmd_draw(cmd, m_isoline_vertex_count, 0);
    }
    tr_cmd_end_render(cmd);
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_color_attachment, tr_texture_usage_present); 
    tr_cmd_depth_stencil_transition(cmd, render_target, tr_texture_usage_depth_stencil_attachment, tr_texture_usage_sampled_image);
    tr_end_cmd(cmd);

    tr_queue_submit(m_renderer->graphics_queue, 1, &cmd, 1, &image_acquired_semaphore, 1, &render_complete_semaphores);
    tr_queue_present(m_renderer->present_queue, 1, &render_complete_semaphores);

    tr_queue_wait_idle(m_renderer->graphics_queue);
}

int main(int argc, char **argv)
{
    glfwSetErrorCallback(app_glfw_error);
    if (! glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, k_app_name, NULL, NULL);
    init_tiny_renderer(window);

    while (! glfwWindowShouldClose(window)) {
        draw_frame();
        glfwPollEvents();
    }
    
    destroy_tiny_renderer();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
