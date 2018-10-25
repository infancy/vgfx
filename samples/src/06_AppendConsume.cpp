#include "GLFW/glfw3.h"
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "GLFW/glfw3native.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

// WARNING: "This sample currently does not work with Vulkan!"
#include "vgfx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const char* k_app_name = "06_AppendConsume";
const uint32_t k_image_count = 3;
const std::string k_asset_dir = "../samples/assets/";

tr_renderer* m_renderer = nullptr;
tr_descriptor_set* m_desc_set = nullptr;
tr_descriptor_set* m_compute_desc_set = nullptr;
tr_cmd_pool* m_cmd_pool = nullptr;
tr_cmd** m_cmds = nullptr;
tr_shader_program* m_compute_shader = nullptr;
tr_shader_program* m_texture_shader = nullptr;
tr_buffer* m_compute_src_counter_buffer = nullptr;
tr_buffer* m_compute_src_buffer = nullptr;
tr_buffer* m_compute_dst_counter_buffer = nullptr;
tr_buffer* m_compute_dst_buffer = nullptr;
tr_buffer* m_rect_index_buffer = nullptr;
tr_buffer* m_rect_vertex_buffer = nullptr;
tr_pipeline* m_pipeline = nullptr;
tr_pipeline* m_compute_pipeline = nullptr;
tr_texture* m_texture = nullptr;
tr_sampler* m_sampler = nullptr;

uint32_t s_window_width;
uint32_t s_window_height;
uint64_t s_frame_count = 0;

int m_image_width = 0;
int m_image_height = 0;
int m_image_row_stride = 0;

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
    settings.handle.connection = XGetXCBConnection(glfwGetX11Display());
    settings.handle.window = glfwGetX11Window(window);
#elif defined(_WIN32)
    settings.handle.hinstance = ::GetModuleHandle(NULL);
    settings.handle.hwnd = glfwGetWin32Window(window);
#endif
    settings.width = s_window_width;
    settings.height = s_window_height;
    settings.swapchain.image_count = k_image_count;
    settings.swapchain.sample_count = tr_sample_count_8;
    settings.swapchain.color_format = tr_format_b8g8r8a8_unorm;
    settings.swapchain.depth_stencil_format = tr_format_undefined;
    settings.log_fn = renderer_log;
#if defined(TINY_RENDERER_VK)
    settings.vk_debug_fn = vulkan_debug;
    settings.instance_layers = instance_layers;
#endif
    tr_create_renderer(k_app_name, &settings, &m_renderer);

    tr_create_cmd_pool(m_renderer, m_renderer->graphics_queue, false, &m_cmd_pool);
    tr_create_cmd_n(m_cmd_pool, false, k_image_count, &m_cmds);

#if defined(TINY_RENDERER_VK)
    auto comp = load_file(k_asset_dir + "append_consume.cs.spv");
    tr_create_shader_program_compute(m_renderer, (uint32_t)comp.size(), comp.data(), "main",
                                     &m_compute_shader);

    auto vert = load_file(k_asset_dir + "texture.vs.spv");
    auto frag = load_file(k_asset_dir + "texture.ps.spv");
    tr_create_shader_program(m_renderer, (uint32_t)vert.size(), (uint32_t*)(vert.data()), "VSMain",
                             (uint32_t)frag.size(), (uint32_t*)(frag.data()), "PSMain",
                             &m_texture_shader);
#elif defined(TINY_RENDERER_DX)
    auto hlsl = load_file(k_asset_dir + "append_consume.hlsl");
    tr_create_shader_program_compute(m_renderer, (uint32_t)hlsl.size(), hlsl.data(), "main",
                                     &m_compute_shader);

    hlsl = load_file(k_asset_dir + "texture.hlsl");
    tr_create_shader_program(m_renderer, (uint32_t)hlsl.size(), hlsl.data(), "VSMain",
                             (uint32_t)hlsl.size(), hlsl.data(), "PSMain", &m_texture_shader);
#endif

    std::vector<tr_descriptor> descriptors(2);
    descriptors[0].type = tr_descriptor_type_texture_srv;
    descriptors[0].count = 1;
    descriptors[0].binding = 0;
    descriptors[0].shader_stages = tr_shader_stage_frag;
    descriptors[1].type = tr_descriptor_type_sampler;
    descriptors[1].count = 1;
    descriptors[1].binding = 1;
    descriptors[1].shader_stages = tr_shader_stage_frag;
    tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(),
                             &m_desc_set);

    // See append_consume.hlsl for bindings for both Vulkan and D3D12
#if defined(TINY_RENDERER_VK)
    descriptors.resize(4);
    // Consume buffer data
    descriptors[0].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[0].count = 1;
    descriptors[0].binding = 0;
    descriptors[0].shader_stages = tr_shader_stage_comp;
    // Consume buffer counter
    descriptors[1].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[1].count = 1;
    descriptors[1].binding = 1;
    descriptors[1].shader_stages = tr_shader_stage_comp;
    // Append buffer data
    descriptors[2].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[2].count = 1;
    descriptors[2].binding = 2;
    descriptors[2].shader_stages = tr_shader_stage_comp;
    // Append buffer counter
    descriptors[3].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[3].count = 1;
    descriptors[3].binding = 3;
    descriptors[3].shader_stages = tr_shader_stage_comp;
    tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(),
                             &m_compute_desc_set);
#elif defined(TINY_RENDERER_DX)
    descriptors[0].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[0].count = 1;
    descriptors[0].binding = 0;
    descriptors[0].shader_stages = tr_shader_stage_comp;
    descriptors[1].type = tr_descriptor_type_storage_buffer_uav;
    descriptors[1].count = 1;
    descriptors[1].binding = 2;
    descriptors[1].shader_stages = tr_shader_stage_comp;
    tr_create_descriptor_set(m_renderer, (uint32_t)descriptors.size(), descriptors.data(),
                             &m_compute_desc_set);
#endif

    tr_vertex_layout vertex_layout = {};
    vertex_layout.attrib_count = 2;
    vertex_layout.attribs[0].semantic = tr_semantic_position;
    vertex_layout.attribs[0].format = tr_format_r32g32b32a32_float;
    vertex_layout.attribs[0].binding = 0;
    vertex_layout.attribs[0].location = 0;
    vertex_layout.attribs[0].offset = 0;
    vertex_layout.attribs[1].semantic = tr_semantic_texcoord0;
    vertex_layout.attribs[1].format = tr_format_r32g32_float;
    vertex_layout.attribs[1].binding = 0;
    vertex_layout.attribs[1].location = 1;
    vertex_layout.attribs[1].offset = tr_util_format_stride(tr_format_r32g32b32a32_float);
    tr_pipeline_settings pipeline_settings = {tr_primitive_topo_tri_list};
    tr_create_pipeline(m_renderer, m_texture_shader, &vertex_layout, m_desc_set,
                       m_renderer->swapchain_render_targets[0], &pipeline_settings, &m_pipeline);

    pipeline_settings = {};
    tr_create_compute_pipeline(m_renderer, m_compute_shader, m_compute_desc_set, &pipeline_settings,
                               &m_compute_pipeline);

    std::vector<float> vertexData = {
        -0.5f, 0.5f,  0.0f, 1.0f, 0.0f, 0.0f, -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.5f,  -0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f,  0.5f,  0.0f, 1.0f, 1.0f, 0.0f,
    };

    uint64_t vertexDataSize = sizeof(float) * vertexData.size();
    uint32_t vertexStride = sizeof(float) * 6;
    tr_create_vertex_buffer(m_renderer, vertexDataSize, true, vertexStride, &m_rect_vertex_buffer);
    memcpy(m_rect_vertex_buffer->cpu_mapped_address, vertexData.data(), vertexDataSize);

    std::vector<uint16_t> indexData = {0, 1, 2, 0, 2, 3};

    uint64_t indexDataSize = sizeof(uint16_t) * indexData.size();
    tr_create_index_buffer(m_renderer, indexDataSize, true, tr_index_type_uint16,
                           &m_rect_index_buffer);
    memcpy(m_rect_index_buffer->cpu_mapped_address, indexData.data(), indexDataSize);

    int image_channels = 0;
    int required_channels = 4;
    unsigned char* image_data = stbi_load((k_asset_dir + "box_panel.jpg").c_str(), &m_image_width,
                                          &m_image_height, &image_channels, required_channels);
    assert(NULL != image_data);
    m_image_row_stride = m_image_width * required_channels;
    uint64_t buffer_size = m_image_row_stride * m_image_height;
    uint64_t element_count = m_image_width * m_image_height;
    uint64_t struct_stride = required_channels;
    // Consume buffer
    tr_create_rw_structured_buffer(m_renderer, buffer_size, 0, element_count, struct_stride, false,
                                   &m_compute_src_counter_buffer, &m_compute_src_buffer);
    tr_queue_update_buffer(m_renderer->graphics_queue, buffer_size, image_data,
                          m_compute_src_buffer);
    tr_queue_set_storage_buffer_count(m_renderer->graphics_queue, 0, (uint32_t)element_count,
                                     m_compute_src_counter_buffer);
    stbi_image_free(image_data);
    // Append buffer
    tr_create_rw_structured_buffer(m_renderer, buffer_size, 0, element_count, struct_stride, false,
                                   &m_compute_dst_counter_buffer, &m_compute_dst_buffer);
    tr_queue_set_storage_buffer_count(m_renderer->graphics_queue, 0, 0,
                                     m_compute_dst_counter_buffer);
    tr_queue_transition_buffer(m_renderer->graphics_queue, m_compute_dst_buffer,
                              tr_buffer_usage_storage_uav, tr_buffer_usage_transfer_src);

    tr_create_texture_2d(m_renderer, m_image_width, m_image_height, tr_sample_count_1,
                         tr_format_r8g8b8a8_unorm, 1, NULL, false, tr_texture_usage_sampled_image,
                         &m_texture);
    tr_queue_transition_image(m_renderer->graphics_queue, m_texture, tr_texture_usage_undefined,
                             tr_texture_usage_sampled_image);

    tr_create_sampler(m_renderer, &m_sampler);

    m_desc_set->descriptors[0].textures[0] = m_texture;
    m_desc_set->descriptors[1].samplers[0] = m_sampler;
    tr_update_descriptor_set(m_renderer, m_desc_set);

#if defined(TINY_RENDERER_VK)
    m_compute_desc_set->descriptors[0].buffers[0] = m_compute_src_buffer;
    m_compute_desc_set->descriptors[1].buffers[0] = m_compute_src_counter_buffer;
    m_compute_desc_set->descriptors[2].buffers[0] = m_compute_dst_buffer;
    m_compute_desc_set->descriptors[3].buffers[0] = m_compute_dst_counter_buffer;
#elif defined(TINY_RENDERER_DX)
    m_compute_desc_set->descriptors[0].buffers[0] = m_compute_src_buffer;
    m_compute_desc_set->descriptors[1].buffers[0] = m_compute_dst_buffer;
#endif
    tr_update_descriptor_set(m_renderer, m_compute_desc_set);
}

void destroy_tiny_renderer() { tr_destroy_renderer(m_renderer); }

void draw_frame()
{
    uint32_t frameIdx = s_frame_count % m_renderer->settings.swapchain.image_count;

    tr_fence* image_acquired_fence = m_renderer->image_acquired_fences[frameIdx];
    tr_semaphore* image_acquired_semaphore = m_renderer->image_acquired_semaphores[frameIdx];
    tr_semaphore* render_complete_semaphores = m_renderer->render_complete_semaphores[frameIdx];

    tr_acquire_next_image(m_renderer, image_acquired_semaphore, image_acquired_fence);

    uint32_t swapchain_image_index = m_renderer->swapchain_image_index;
    tr_render_target* render_target = m_renderer->swapchain_render_targets[swapchain_image_index];

    tr_cmd* cmd = m_cmds[frameIdx];

    tr_begin_cmd(cmd);

    // Use compute to swizzle RGB -> BRG in buffer
    tr_cmd_buffer_transition(cmd, m_compute_dst_buffer, tr_buffer_usage_transfer_src,
                             tr_buffer_usage_storage_uav);
    tr_cmd_bind_pipeline(cmd, m_compute_pipeline);
    tr_cmd_bind_descriptor_sets(cmd, m_compute_pipeline, m_compute_desc_set);
    tr_cmd_dispatch(cmd, m_image_width, m_image_height, 1);
    tr_cmd_buffer_transition(cmd, m_compute_dst_buffer, tr_buffer_usage_storage_uav,
                             tr_buffer_usage_transfer_src);
    // Copy compute output buffer to texture
    tr_cmd_image_transition(cmd, m_texture, tr_texture_usage_sampled_image,
                            tr_texture_usage_transfer_dst);
    tr_cmd_copy_buffer_to_texture2d(cmd, m_image_width, m_image_height, m_image_row_stride, 0, 0,
                                    m_compute_dst_buffer, m_texture);
    tr_cmd_image_transition(cmd, m_texture, tr_texture_usage_transfer_dst,
                            tr_texture_usage_sampled_image);
    // Draw compute result to screen - pixels will be out of order because of append/consume
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_present,
                                    tr_texture_usage_color_attachment);
    tr_cmd_set_viewport(cmd, 0, 0, (float)s_window_width, (float)s_window_height, 0.0f, 1.0f);
    tr_cmd_set_scissor(cmd, 0, 0, s_window_width, s_window_height);
    tr_cmd_begin_render(cmd, render_target);
    tr_clear_value clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    tr_cmd_clear_color_attachment(cmd, 0, &clear_value);
    tr_cmd_bind_pipeline(cmd, m_pipeline);
    tr_cmd_bind_index_buffer(cmd, m_rect_index_buffer);
    tr_cmd_bind_vertex_buffers(cmd, 1, &m_rect_vertex_buffer);
    tr_cmd_bind_descriptor_sets(cmd, m_pipeline, m_desc_set);
    tr_cmd_draw_indexed(cmd, 6, 0);
    tr_cmd_end_render(cmd);
    tr_cmd_render_target_transition(cmd, render_target, tr_texture_usage_color_attachment,
                                    tr_texture_usage_present);
    tr_end_cmd(cmd);

    tr_queue_submit(m_renderer->graphics_queue, 1, &cmd, 1, &image_acquired_semaphore, 1,
                    &render_complete_semaphores);
    tr_queue_present(m_renderer->present_queue, 1, &render_complete_semaphores);

    tr_queue_wait_idle(m_renderer->graphics_queue);
}

int main(int argc, char** argv)
{
    glfwSetErrorCallback(app_glfw_error);
    if (!glfwInit())
    {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(640, 480, k_app_name, NULL, NULL);
    init_tiny_renderer(window);

    while (!glfwWindowShouldClose(window))
    {
        draw_frame();
        glfwPollEvents();
    }

    destroy_tiny_renderer();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
