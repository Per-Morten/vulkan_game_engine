#include <vge_gfx_manager.h>
#include <vge_gfx_gl.h>
#include <vge_debug.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vge_array.h>

#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <stb_image.h>

/////////////////////////////////////////////////
/// Mesh Related
/////////////////////////////////////////////////
struct mesh_gl_data
{
    GLuint VAO;
    GLuint VBO;
    GLuint EBO;
    GLuint uv0TBO;
    GLuint uv1TBO;
};

// TODO: Need a more efficient way to store this data, the meta data is only
// needed for imgui, so no need to store it with the rest.
// Similarly, only the VAO is needed when drawing, so shouldn't store that
// together with the VBO, EBO etc either.
struct mesh_info
{
    VGE::MeshHandle handle;
    VGE::MeshData mesh_data;
    mesh_gl_data gl_data;
};

static VGE::Array<mesh_info> g_mesh_table;
static VGE::MeshHandle g_new_mesh_handle;

VGE::MeshHandle
VGE::GFXManager::CreateMesh()
{
    auto NewPair = mesh_info();
    NewPair.handle = g_new_mesh_handle++;
    g_mesh_table.PushBack(NewPair);
    return NewPair.handle;
}

void
VGE::GFXManager::SetMesh(MeshHandle handle,
                         MeshData data)
{
    auto itr = std::find_if(g_mesh_table.Begin(), g_mesh_table.End(),
                            [=](const auto& item)
                            { return item.handle == handle; });

    if (itr == g_mesh_table.End())
    {
        VGE_WARN("Handle %d not found", handle);
        return;
    }

    if (!itr->mesh_data.name.empty())
    {
        // Destroy OpenGL stuff
    }

    itr->mesh_data = data;
    mesh_gl_data new_data;

    glGenVertexArrays(1, &new_data.VAO);
    glBindVertexArray(new_data.VAO); // Bind VAO to start "recording" binding calls

    glGenBuffers(1, &new_data.EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, new_data.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * data.triangle_count, data.triangles, GL_STATIC_DRAW);

    glGenBuffers(1, &new_data.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, new_data.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * data.vertex_count, data.vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &new_data.uv0TBO);
    glBindBuffer(GL_ARRAY_BUFFER, new_data.uv0TBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec2) * data.vertex_count, data.uv0, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0); // Unbind VAO to avoid messing it up by binding other stuff.

    itr->gl_data = new_data;
}

// TODO: This should be assumed to be async.
void
VGE::GFXManager::DrawMesh(VGE::MeshHandle handle)
{
    auto itr = std::find_if(g_mesh_table.Begin(), g_mesh_table.End(),
                            [=](const auto& item)
                            { return item.handle == handle; });

    glBindVertexArray(itr->gl_data.VAO);
    glDrawElements(GL_TRIANGLES, itr->mesh_data.triangle_count, GL_UNSIGNED_INT, 0);
}

///////////////////////////////////////////////////////////
/// Texture Related
///////////////////////////////////////////////////////////
struct texture_info
{
    VGE::TextureHandle handle;
    VGE::TextureID texture_id;

    // TODO: Move this extended information into different places to make better use of cache etc.
    std::string filepath;

    int width;
    int height;
};

static VGE::TextureHandle g_new_texture_handle;
static VGE::Array<texture_info> g_texture_table;

namespace local::texture
{
    texture_info*
    get_texture(VGE::TextureHandle handle)
    {
        auto itr = std::find_if(g_texture_table.Begin(),
                                g_texture_table.End(),
                                [=](const auto& item)
                                { return item.handle == handle; });

        return (itr != g_texture_table.End())
                   ? &(*itr)
                   : nullptr;
    }
}

VGE::TextureHandle
VGE::GFXManager::CreateTexture()
{
    g_texture_table.PushBack({});
    auto& texture = g_texture_table.Back();
    //auto& texture = g_texture_table.emplace_back();
    texture.handle = g_new_texture_handle++;
    return texture.handle;
}

void
VGE::GFXManager::LoadTexture(VGE::TextureHandle handle,
                               const char* filepath)
{
    auto texture = local::texture::get_texture(handle);

    int width;
    int height;
    int channels;

    stbi_set_flip_vertically_on_load(true);
    auto data = stbi_load(filepath, &width, &height, &channels, 0);
    if (!data)
        VGE_ERROR("Could not load image: %s, %s", filepath, stbi_failure_reason());

    texture->filepath = filepath;
    texture->width = width;
    texture->height = height;

    glGenTextures(1, &texture->texture_id);
    glBindTexture(GL_TEXTURE_2D, texture->texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const auto fmt = (channels < 4) ? GL_RGB : GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
}

VGE::TextureID
VGE::GFXManager::GetTextureID(VGE::TextureHandle handle)
{
    return local::texture::get_texture(handle)->texture_id;
}

///////////////////////////////////////////////////////////
/// Shader Related
///////////////////////////////////////////////////////////
struct program
{
    VGE::ShaderHandle handle;
    VGE::ProgramID program_id;
};

struct shader_source
{
    GLuint shader_id;

    GLenum type;
    char file[128];
    char source[1024 * 16];
};

static VGE::Array<program> g_program_table;
static VGE::ShaderHandle g_new_program_handle;

static VGE::Array<shader_source> g_shader_source_table;

namespace local::shader
{
    std::string
    load_source(const char* shader_path)
    {
        std::ifstream file(shader_path);
        std::stringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    void
    load_source(const char* shader_path, char* buffer, int buff_size)
    {
        VGE_ASSERT(shader_path && buffer && buff_size > 0, "Invalid parameters");

        errno = 0;
        FILE* file = std::fopen(shader_path, "r");
        if (!file || errno != 0)
            VGE_ERROR("Could not open shader file: %s, error: %s", shader_path, strerror(errno));

        std::fseek(file, 0L, SEEK_END);
        auto file_len = std::ftell(file) + 1;
        std::rewind(file);

        VGE_ASSERT(file_len <= buff_size, "Shader is too large for buffer: %s", shader_path);

        buffer[file_len - 1] = '\0';

        if (auto res = (int)std::fread(buffer, sizeof(char), file_len, file); res < file_len - 1)
            VGE_WARN("Could not read entire shader, res: %d vs %d", res, file_len);

        std::fclose(file);
    }

    GLuint
    compile_shader(const char* src, GLenum type)
    {
        auto shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);

        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar info_log[512];
            glGetShaderInfoLog(shader, 512, NULL, info_log);
            VGE_WARN("%s", info_log);
        }
        return shader;
    }

    program*
    get_shader(VGE::ShaderHandle handle)
    {
        auto itr = std::find_if(g_program_table.Begin(), g_program_table.End(),
                                [=](const auto& item)
                                { return item.handle == handle; });

        return (itr != g_program_table.End())
                    ? &(*itr)
                    : nullptr;
    }

    shader_source*
    get_shader_source(GLuint shader_id)
    {
        auto itr = std::find_if(g_shader_source_table.Begin(), g_shader_source_table.End(),
                                [=](const auto& item)
                                { return item.shader_id == shader_id; });

        return (itr != g_shader_source_table.End())
                    ? &(*itr)
                    : nullptr;
    }
} // namespace local::shader

VGE::ShaderHandle
VGE::GFXManager::CreateShader()
{
    auto new_data = program();
    new_data.handle = g_new_program_handle++;
    new_data.program_id = glCreateProgram();
    g_program_table.PushBack(new_data);

    return new_data.handle;
}

void
VGE::GFXManager::AttachShader(VGE::ShaderHandle handle,
                                const char* filepath,
                                GLenum type)
{
    VGE_ASSERT(filepath, "filepath cannot be null");

    shader_source tmp;
    tmp.type = type;
    std::strcpy(tmp.file, filepath);
    local::shader::load_source(filepath, tmp.source, sizeof(tmp.source));
    tmp.shader_id = local::shader::compile_shader(tmp.source, type);
    g_shader_source_table.PushBack(tmp);

    auto program = local::shader::get_shader(handle);
    VGE_ASSERT(program, "Did not find program with handle: %d", handle);

    glAttachShader(program->program_id, tmp.shader_id);
}

void
VGE::GFXManager::CompileAndLinkShader(ShaderHandle handle)
{
    auto program = local::shader::get_shader(handle);
    VGE_ASSERT(program, "Did not find program with handle: %d", handle);
    glLinkProgram(program->program_id);

    int success;
    glGetProgramiv(program->program_id, GL_LINK_STATUS, &success);
    if (!success)
    {
        GLchar info_log[512];
        glGetProgramInfoLog(program->program_id, 512, nullptr, info_log);
        VGE_WARN("%s", info_log);
    }
}

VGE::ProgramID
VGE::GFXManager::GetShaderID(VGE::ShaderHandle handle)
{
    auto program = local::shader::get_shader(handle);
    VGE_ASSERT(program, "Did not find program with handle: %d", handle);
    return program->program_id;
}

///////////////////////////////////////////////////////////
/// New and Dynamic Drawing
///////////////////////////////////////////////////////////
void
VGE::GFXManager::Init()
{
    glGenVertexArrays(1, &mDynamicVAO);
    glBindVertexArray(mDynamicVAO);

    glCreateBuffers(1, &mDynamicVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mDynamicVBO);
    glBufferData(GL_ARRAY_BUFFER, DynamicVBOSize, nullptr, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void
VGE::GFXManager::DrawLine(glm::vec3 begin, glm::vec3 end, Color color)
{
    VGE_ASSERT(mDynamicVerticesCount + 2 < (int)MaxDynamicVertices, "Trying to add to many vertices");
    mDynamicVertices[mDynamicVerticesCount].position = begin;
    mDynamicVertices[mDynamicVerticesCount].color = color.raw;
    mDynamicVerticesCount++;
    mDynamicVertices[mDynamicVerticesCount].position = end;
    mDynamicVertices[mDynamicVerticesCount].color = color.raw;
    mDynamicVerticesCount++;
}

void
VGE::GFXManager::RenderImmediate()
{
    auto buffer = glMapNamedBuffer(mDynamicVBO, GL_WRITE_ONLY);
    std::memcpy(buffer, mDynamicVertices, mDynamicVerticesCount * sizeof(Vertex));
    glUnmapNamedBuffer(mDynamicVBO);
    glBindVertexArray(mDynamicVAO);
    glDrawArrays(GL_LINES, 0, mDynamicVerticesCount);
    glBindVertexArray(0);

    mDynamicVerticesCount = 0;
}

void
VGE::GFXManager::SubmitStaticDrawCommand(const StaticDrawCommand& command)
{
    VGE_ASSERT(mStaticCommandsCount < (int)MaxStaticDrawCommands, "Trying to add to many static draw commands");
    //mStaticCommands[mStaticCommandsCount++] = command;
}

void
VGE::GFXManager::RenderStatic()
{

}

///////////////////////////////////////////////////////////
/// Introspection related
///////////////////////////////////////////////////////////
// TODO: Based on the introspection stuff I found when developing gfx.h
//       I should remove unneeded meta information in this file.
///      Can at least do this for uniforms!
namespace local::introspection
{
    #define VARIABLE_RENDER_GENERATOR(cputype, count, gltype, glread, glwrite, imguifunc)  \
    {                                                                                      \
        ImGui::Text(#gltype" %s", name);                                                   \
        cputype value[count];                                                              \
        glread(program, location, &value[0]);                                              \
        if (imguifunc("", &value[0]))                                                      \
            glwrite(program, location, 1, &value[0]);                                      \
    }

    static void
    render_uniform_variable(GLuint program, GLenum type, const char* name, GLint location)
    {
        switch (type)
        {
            case GL_FLOAT:
                VARIABLE_RENDER_GENERATOR(GLfloat, 1, GL_FLOAT, glGetUniformfv, glProgramUniform1fv, ImGui::DragFloat);
                break;

            case GL_FLOAT_VEC2:
                VARIABLE_RENDER_GENERATOR(GLfloat, 2, GL_FLOAT_VEC2, glGetUniformfv, glProgramUniform2fv, ImGui::DragFloat2);
                break;

            case GL_FLOAT_VEC3:
            {
                static bool is_color = false;
                ImGui::Checkbox("##is_color", &is_color); ImGui::SameLine();
                ImGui::Text("GL_FLOAT_VEC3 %s", name); ImGui::SameLine();
                float value[3];
                glGetUniformfv(program, location, &value[0]);
                if ((!is_color && ImGui::DragFloat3("", &value[0])) || (is_color && ImGui::ColorEdit3("Color", &value[0], ImGuiColorEditFlags_NoLabel)))
                    glProgramUniform3fv(program, location, 1, &value[0]);
            }
                break;

            case GL_FLOAT_VEC4:
            {
                static bool is_color = false;
                ImGui::Checkbox("##is_color", &is_color); ImGui::SameLine();
                ImGui::Text("GL_FLOAT_VEC4 %s", name); ImGui::SameLine();
                float value[4];
                glGetUniformfv(program, location, &value[0]);
                if ((!is_color && ImGui::DragFloat4("", &value[0])) || (is_color && ImGui::ColorEdit4("Color", &value[0], ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)))
                    glProgramUniform4fv(program, location, 1, &value[0]);
            }
                break;

            case GL_INT:
                VARIABLE_RENDER_GENERATOR(GLint, 1, GL_INT, glGetUniformiv, glProgramUniform1iv, ImGui::DragInt);
                break;

            case GL_INT_VEC2:
                VARIABLE_RENDER_GENERATOR(GLint, 2, GL_INT, glGetUniformiv, glProgramUniform2iv, ImGui::DragInt2);
                break;

            case GL_INT_VEC3:
                VARIABLE_RENDER_GENERATOR(GLint, 3, GL_INT, glGetUniformiv, glProgramUniform3iv, ImGui::DragInt3);
                break;

            case GL_INT_VEC4:
                VARIABLE_RENDER_GENERATOR(GLint, 4, GL_INT, glGetUniformiv, glProgramUniform4iv, ImGui::DragInt4);
                break;

            case GL_SAMPLER_2D:
                VARIABLE_RENDER_GENERATOR(GLint, 1, GL_SAMPLER_2D, glGetUniformiv, glProgramUniform1iv, ImGui::DragInt);
                break;

            case GL_FLOAT_MAT3:
            {
                ImGui::Text("GL_FLOAT_MAT3 %s:", name);
                GLfloat value[3 * 3];
                glGetUniformfv(program, location, value);
                for (int i = 0; i < 9; i += 3)
                {
                    ImGui::PushID(i);
                    ImGui::DragFloat3("", &value[i], 0.25f);
                    ImGui::PopID();
                }
                glProgramUniformMatrix3fv(program, location, 1, GL_FALSE, value);
            }
                break;

            case GL_FLOAT_MAT4:
            {
                ImGui::Text("GL_FLOAT_MAT4 %s:", name);
                GLfloat value[4 * 4];
                glGetUniformfv(program, location, value);
                for (int i = 0; i < 16; i += 4)
                {
                    ImGui::PushID(i);
                    ImGui::DragFloat4("", &value[i], 0.25f);
                    ImGui::PopID();
                }
                glProgramUniformMatrix4fv(program, location, 1, GL_FALSE, value);
            }
                break;

            default:
                ImGui::Text("%s has type %s, which isn't supported yet!", name, VGE::GFX::GLEnumToString(type));
                break;
        }
    }

    #undef VARIABLE_RENDER_GENERATOR
}

void
VGE::GFXManager::DrawDebug()
{
    if (ImGui::BeginTabBar("GraphicsTab"))
    {
        if (ImGui::BeginTabItem("Meshes"))
        {
            for (int i = 0; i < g_mesh_table.Size(); i++)
            {
                const auto& mesh = g_mesh_table[i];
                char buffer[32];
                std::sprintf(buffer, "Handle: %d", mesh.handle);
                ImGui::PushID(buffer);
                if (ImGui::CollapsingHeader(buffer))
                {
                    ImGui::Text("name: %s", mesh.mesh_data.name.c_str());
                    ImGui::Text("vertex_count: %d", mesh.mesh_data.vertex_count);
                    ImGui::Text("triangle_count: %d", mesh.mesh_data.triangle_count);

                    if (ImGui::TreeNode("GLData"))
                    {
                        ImGui::Text("VAO: %u", mesh.gl_data.VAO);
                        ImGui::Text("VBO: %u", mesh.gl_data.VBO);
                        ImGui::Text("EBO: %u", mesh.gl_data.EBO);
                        ImGui::Text("uv0TBO: %u", mesh.gl_data.uv0TBO);
                        ImGui::Text("uv0TB1: %u", mesh.gl_data.uv1TBO);

                        ImGui::TreePop();
                    }

                    if (ImGui::TreeNode("Data"))
                    {
                        // TODO: Fix logic here, so we don't always use 200(?)
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
                        ImGui::BeginChild("child", ImVec2(0, 200), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                        ImGui::Columns(4);
                        ImGui::Text("triangles");
                        ImGui::NextColumn();
                        ImGui::Text("vertices x");
                        ImGui::NextColumn();
                        ImGui::Text("vertices y");
                        ImGui::NextColumn();
                        ImGui::Text("vertices z");
                        ImGui::NextColumn();

                        ImGui::Spacing();
                        ImGui::Separator();

                        int max = std::max(mesh.mesh_data.vertex_count,
                                           mesh.mesh_data.triangle_count);

                        for (int i = 0; i < max; i++)
                        {
                            if (i < mesh.mesh_data.triangle_count)
                            {
                                ImGui::Text("%d", mesh.mesh_data.triangles[i]);
                            }
                            ImGui::NextColumn();

                            // TODO: Really want to indexes together with the vertex corresponding with that index
                            if (i < mesh.mesh_data.vertex_count)
                            {
                                for (int j = 0; j < 3; j++)
                                {
                                    ImGui::Text("% f", *(float*)(&mesh.mesh_data.vertices[i].x + j));
                                    ImGui::NextColumn();
                                }
                            }

                            while (ImGui::GetColumnIndex() > 0 && ImGui::GetColumnIndex() < ImGui::GetColumnsCount())
                            {
                                ImGui::NextColumn();
                            }

                            ImGui::Separator();
                        }
                        ImGui::EndChild();
                        ImGui::TreePop();
                        ImGui::PopStyleVar();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shader Programs"))
        {
            for (int i = 0; i < g_program_table.Size(); i++)
            {
                const auto& program = g_program_table[i];
                char buffer[32];
                std::sprintf(buffer, "Handle: %d", program.handle);
                if (ImGui::CollapsingHeader(buffer))
                {
                    ImGui::Indent();
                    if (ImGui::CollapsingHeader("Uniforms", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        GLint uniform_count;
                        glGetProgramiv(program.program_id, GL_ACTIVE_UNIFORMS, &uniform_count);

                        // Read the length of the longest active uniform.
                        GLint max_name_length;
                        glGetProgramiv(program.program_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name_length);

                        VGE_ASSERT(max_name_length < 32, "Currently don't support uniform names of more than 32 characters, it was %d", max_name_length);
                        char name[32];

                        for (int j = 0; j < uniform_count; j++)
                        {
                            GLint ignored;
                            GLenum type;
                            glGetActiveUniform(program.program_id, j, max_name_length, nullptr, &ignored, &type, name);

                            const auto location = glGetUniformLocation(program.program_id, name);
                            ImGui::Indent();
                            ImGui::PushID(j);
                            ImGui::PushItemWidth(-1.0f);
                            local::introspection::render_uniform_variable(program.program_id, type, name, location);
                            ImGui::PopItemWidth();
                            ImGui::PopID();
                            ImGui::Unindent();
                        }
                    }
                    ImGui::Unindent();

                    ImGui::Indent();
                    if (ImGui::CollapsingHeader("Shaders"))
                    {
                        GLint shader_count;
                        glGetProgramiv(program.program_id, GL_ATTACHED_SHADERS, &shader_count);

                        VGE_ASSERT(shader_count <= 2, "We are currently not expecting more than 2 shaders per shader program, count: %d", shader_count);

                        GLuint attached_shaders[2];
                        glGetAttachedShaders(program.program_id, shader_count, nullptr, attached_shaders);

                        for (const auto& shader_id : attached_shaders)
                        {
                            auto shader = local::shader::get_shader_source(shader_id);
                            VGE_ASSERT(shader, "Source for shader: %u is nullptr", shader_id);

                            ImGui::Indent();
                            auto string_type = GFX::GLEnumToString(shader->type);
                            ImGui::PushID(string_type);
                            if (ImGui::CollapsingHeader(string_type))
                            {
                                auto y_size = std::min(ImGui::CalcTextSize(shader->source).y, ImGui::GetTextLineHeightWithSpacing() * 16);

                                if (ImGui::Button("Compile"))
                                {
                                    const char* ptr = shader->source;
                                    glShaderSource(shader_id, 1, &ptr, NULL);
                                    glCompileShader(shader_id);

                                    int success = 0;
                                    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &success);
                                    if (success)
                                    {
                                        glLinkProgram(program.program_id);
                                        glGetProgramiv(program.program_id, GL_LINK_STATUS, &success);
                                        if (!success)
                                        {
                                            // Should probably also get the log length here, so I know how much space I need
                                            // Need to get link status in here as well.
                                            GLchar info_log[512];
                                            glGetProgramInfoLog(program.program_id, sizeof(info_log), nullptr, info_log);
                                            VGE_WARN("Linking error while live editing shader %s", info_log);
                                        }
                                    }
                                }

                                ImGui::InputTextMultiline("##shader", shader->source, sizeof(shader->source), ImVec2(-1.0f, y_size));

                                int success = 0;
                                glGetShaderiv(shader_id, GL_COMPILE_STATUS, &success);
                                ImGui::Text("Compilation status: %s", (success) ? "Compiled" : "Failed");

                                GLchar info_log[512];
                                GLsizei length = 0;
                                glGetShaderInfoLog(shader_id, sizeof(info_log), &length, info_log);
                                if (length)
                                    ImGui::InputTextMultiline("##output", info_log, sizeof(info_log), ImVec2(-1.0f, 2 * ImGui::GetTextLineHeightWithSpacing()), ImGuiInputTextFlags_ReadOnly);


                            }
                            ImGui::PopID();
                            ImGui::Unindent();
                        }
                    }
                    ImGui::Unindent();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures"))
        {
            for (int i = 0; i < g_texture_table.Size(); i++)
            {
                const auto& texture = g_texture_table[i];
                char buffer[128];
                std::sprintf(buffer, "Handle: %d", texture.handle);
                if (ImGui::CollapsingHeader(buffer))
                {
                    std::sprintf(buffer, "Texture id: %u, width: %d, height: %d, filepath: %s",
                                 texture.texture_id, texture.width, texture.height, texture.filepath.c_str());

                    if (ImGui::TreeNode(buffer))
                    {
                        // Need to find a good way to flip the image since ImGUI does not take in an image in bytes.
                        ImGui::Image((void*)texture.texture_id, ImVec2(texture.width / 4, texture.height / 4));
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings"))
        {
            static bool mode = false;
            if (ImGui::Checkbox("Draw wireframe mode", &mode))
                glPolygonMode(GL_FRONT_AND_BACK, (mode) ? GL_LINE : GL_FILL);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}


