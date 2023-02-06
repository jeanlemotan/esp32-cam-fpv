// ImGui Renderer for: OpenGL3 (modern OpenGL with shaders / programmatic pipeline)
// This needs to be used along with a Platform Binding (e.g. GLFW, SDL, Win32, custom..)
// (Note: We are using GL3W as a helper library to access OpenGL functions since there is no standard header to access modern OpenGL functions easily. Alternatives are GLEW, Glad, etc..)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GLuint' OpenGL texture identifier as void*/ImTextureID. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

// CHANGELOG 
// (minor and older changes stripped away, please see git history for details)
//  2018-07-10: OpenGL: Support for more GLSL versions (based on the GLSL version string). Added error output when shaders fail to compile/link.
//  2018-06-08: Misc: Extracted imgui_impl_opengl3.cpp/.h away from the old combined GLFW/SDL+OpenGL3 examples.
//  2018-06-08: OpenGL: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-05-25: OpenGL: Removed unnecessary backup/restore of GL_ELEMENT_ARRAY_BUFFER_BINDING since this is part of the VAO state.
//  2018-05-14: OpenGL: Making the call to glBindSampler() optional so 3.2 context won't fail if the function is a NULL pointer.
//  2018-03-06: OpenGL: Added const char* glsl_version parameter to ImGui_ImplOpenGL3_Init() so user can override the GLSL version e.g. "#version 150".
//  2018-02-23: OpenGL: Create the VAO in the render function so the setup can more easily be used with multiple shared GL context.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplSdlGL3_RenderDrawData() in the .h file so you can call it yourself.
//  2018-01-07: OpenGL: Changed GLSL shader version from 330 to 150.
//  2017-09-01: OpenGL: Save and restore current bound sampler. Save and restore current polygon mode.
//  2017-05-01: OpenGL: Fixed save and restore of current blend func state.
//  2017-05-01: OpenGL: Fixed save and restore of current GL_ACTIVE_TEXTURE.
//  2016-09-05: OpenGL: Fixed save and restore of current scissor rectangle.
//  2016-07-29: OpenGL: Explicitly setting GL_UNPACK_ROW_LENGTH to reduce issues because SDL changes it. (#752)

//----------------------------------------
// OpenGL    GLSL      GLSL
// version   version   string
//----------------------------------------
//  2.0       110       "#version 110"
//  2.1       120
//  3.0       130
//  3.1       140
//  3.2       150       "#version 150"
//  3.3       330
//  4.0       400
//  4.1       410
//  4.2       420
//  4.3       430
//  ES 2.0    100       "#version 100"
//  ES 3.0    300       "#version 300 es"
//----------------------------------------

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>     // intptr_t
#else
#include <stdint.h>     // intptr_t
#endif

#include <algorithm>

//#include <GL/gl3w.h>    // This example is using gl3w to access OpenGL functions. You may use another OpenGL loader/header such as: glew, glext, glad, glLoadGen, etc.
//#include <glew.h>
//#include <glext.h>
//#include <glad/glad.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include "Log.h"
#include "main.h"

// OpenGL Data
static char         g_GlslVersionString[32] = "";
static GLuint       g_FontTexture = 0;
static GLuint       g_VideoTextureChannels[3];
void ImGui_SetVideoTextureChannel(unsigned int channel, unsigned int id)
{
    IM_ASSERT(channel < 3);
    g_VideoTextureChannels[channel] = id;   
}

struct ShaderData
{
    GLuint      ShaderHandle = 0;
    GLuint      VertHandle = 0;
    GLuint      FragHandle = 0;
    int         AttribLocationTex = 0;
    int         AttribLocationTexY = 0;
    int         AttribLocationTexU = 0;
    int         AttribLocationTexV = 0;
    int         AttribLocationProjMtx = 0;
    int         AttribLocationPosition = 0;
    int         AttribLocationUV = 0;
    int         AttribLocationColor = 0;
};

ShaderData g_ShaderData;
ShaderData g_ShaderDataVideo;

static unsigned int g_VboHandle = 0, g_ElementsHandle = 0;

// Functions
bool    ImGui_ImplOpenGL3_Init(const char* glsl_version)
{
    // Store GLSL version string so we can refer to it later in case we recreate shaders. Note: GLSL version is NOT the same as GL version. Leave this to NULL if unsure.
#ifndef RASPBERRY_PI
    if (glsl_version == NULL)
        glsl_version = "#version 130";
    IM_ASSERT((int)strlen(glsl_version) + 2 < IM_ARRAYSIZE(g_GlslVersionString));
    strcpy(g_GlslVersionString, glsl_version);
    strcat(g_GlslVersionString, "\n");
#endif
    return true;
}

void    ImGui_ImplOpenGL3_Shutdown()
{
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
}

void    ImGui_ImplOpenGL3_NewFrame()
{
    if (!g_FontTexture)
        ImGui_ImplOpenGL3_CreateDeviceObjects();
}

void    ImGui_BindShaderData(ShaderData& shaderData, float projection[4][4])
{
    GLCHK(glUseProgram(shaderData.ShaderHandle));
    GLCHK(glUniform1i(shaderData.AttribLocationTex, 0));
    if (shaderData.AttribLocationTexY > 0)
        GLCHK(glUniform1i(shaderData.AttribLocationTexY, 0));
    if (shaderData.AttribLocationTexU > 0)
        GLCHK(glUniform1i(shaderData.AttribLocationTexU, 1));
    if (shaderData.AttribLocationTexV > 0)
        GLCHK(glUniform1i(shaderData.AttribLocationTexV, 2));
    GLCHK(glUniformMatrix4fv(shaderData.AttribLocationProjMtx, 1, GL_FALSE, &projection[0][0]));
    GLCHK(glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle));
    GLCHK(glEnableVertexAttribArray(shaderData.AttribLocationPosition));
    GLCHK(glEnableVertexAttribArray(shaderData.AttribLocationUV));
    GLCHK(glEnableVertexAttribArray(shaderData.AttribLocationColor));
    GLCHK(glVertexAttribPointer(shaderData.AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos)));
    GLCHK(glVertexAttribPointer(shaderData.AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv)));
    GLCHK(glVertexAttribPointer(shaderData.AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col)));
}

// OpenGL3 Render function.
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
// Note that this implementation is little overcomplicated because we are saving/setting up/restoring every OpenGL state explicitly, in order to be able to run within any OpenGL engine that doesn't do so. 
void    ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data, bool rotate)
{
    if (!draw_data)
    {
        return;
    }
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // Backup GL state
    GLCHK(glActiveTexture(GL_TEXTURE0));

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    GLCHK(glEnable(GL_BLEND));
    GLCHK(glBlendEquation(GL_FUNC_ADD));
    GLCHK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GLCHK(glDisable(GL_CULL_FACE));
    GLCHK(glDisable(GL_DEPTH_TEST));
    GLCHK(glEnable(GL_SCISSOR_TEST));
    //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
    if (rotate)
        glViewport(0, 0, (GLsizei)fb_height, (GLsizei)fb_width);
    else
        glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    if (rotate)
    {
        std::swap(L, T);
        std::swap(R, B);
        std::swap(L, R);
    }
    float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };
    if (rotate)
    {
        std::swap(ortho_projection[0][0], ortho_projection[1][0]);
        std::swap(ortho_projection[0][1], ortho_projection[1][1]);
    }

    ImGui_BindShaderData(g_ShaderData, ortho_projection);

    GLCHK(glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle));
    GLCHK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle));

    int prev_cx = -1;
    int prev_cy = -1;
    int prev_cw = -1;
    int prev_ch = -1;
    intptr_t prev_texId = -1;

    // Draw
    ImVec2 pos = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx* idx_buffer_offset = 0;

        GLCHK(glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW));
        GLCHK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback (registered via ImDrawList::AddCallback)
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                ImVec4 clip_rect = ImVec4(pcmd->ClipRect.x - pos.x, pcmd->ClipRect.y - pos.y, pcmd->ClipRect.z - pos.x, pcmd->ClipRect.w - pos.y);
                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    int cx1 = (int)clip_rect.x;
                    int cx2 = (int)clip_rect.z;
                    int cy1 = (int)clip_rect.y;
                    int cy2 = (int)clip_rect.w;
                    int cx, cy, cw, ch;

                    if (rotate)
                    {
                        cx = (int)(fb_height - cy2);
                        cy = (int)(fb_width - cx2);
                        cw = (int)(cx2 - cx1);
                        ch = (int)(cy2 - cy1);
                        std::swap(cw, ch);
                    }
                    else
                    {
                        cx = (int)cx1;
                        cy = (int)(fb_height - cy2);
                        cw = (int)(cx2 - cx1);
                        ch = (int)(cy2 - cy1);
                    }

                    if (cx != prev_cx || cy != prev_cy || cw != prev_cw || ch != prev_ch)
                    {
                        GLCHK(glScissor(cx, cy, cw, ch));
                        prev_cx = cx;
                        prev_cy = cy;
                        prev_ch = ch;
                        prev_cw = cw;
                    }

                    // Bind texture, Draw
                    intptr_t texId = (intptr_t)pcmd->TextureId;
                    if (texId & 0x80000000)
                    {
                        prev_texId = -1;
                        ImGui_BindShaderData(g_ShaderDataVideo, ortho_projection);

                        GLCHK(glDisable(GL_BLEND));
                        GLCHK(glActiveTexture(GL_TEXTURE0));
                        GLCHK(glBindTexture(GL_TEXTURE_2D, g_VideoTextureChannels[0]));
                        GLCHK(glActiveTexture(GL_TEXTURE1));
                        GLCHK(glBindTexture(GL_TEXTURE_2D, g_VideoTextureChannels[1]));
                        GLCHK(glActiveTexture(GL_TEXTURE2));
                        GLCHK(glBindTexture(GL_TEXTURE_2D, g_VideoTextureChannels[2]));

                        GLCHK(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset));

                        ImGui_BindShaderData(g_ShaderData, ortho_projection);
                        GLCHK(glActiveTexture(GL_TEXTURE0));
                        GLCHK(glEnable(GL_BLEND));
                    }
                    else
                    {
                        if (prev_texId != texId)
                            GLCHK(glBindTexture(GL_TEXTURE_2D, texId));
                        prev_texId = texId;
                        GLCHK(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset));
                    }
                }
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }
    //glDeleteVertexArrays(1, &vao_handle);
}

bool ImGui_ImplOpenGL3_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Upload texture to graphics system
    GLCHK(glGenTextures(1, &g_FontTexture));
    GLCHK(glBindTexture(GL_TEXTURE_2D, g_FontTexture));
    GLCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    //glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    GLCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    io.Fonts->TexID = (void *)(intptr_t)g_FontTexture;

    return true;
}

void ImGui_ImplOpenGL3_DestroyFontsTexture()
{
    if (g_FontTexture)
    {
        ImGuiIO& io = ImGui::GetIO();
        GLCHK(glDeleteTextures(1, &g_FontTexture));
        io.Fonts->TexID = 0;
        g_FontTexture = 0;
    }
}

// If you get an error please report on github. You may try different GL context version or GLSL version.
static bool CheckShader(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    GLCHK(glGetShaderiv(handle, GL_COMPILE_STATUS, &status));
    GLCHK(glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length));
    if (status == GL_FALSE)
        fprintf(stderr, "ERROR: ImGui_ImplOpenGL3_CreateDeviceObjects: failed to compile %s!\n", desc);
    if (log_length > 0)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        GLCHK(glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.begin()));
        fprintf(stderr, "%s\n", buf.begin());
    }
    return status == GL_TRUE;
}

// If you get an error please report on github. You may try different GL context version or GLSL version.
static bool CheckProgram(GLuint handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    GLCHK(glGetProgramiv(handle, GL_LINK_STATUS, &status));
    GLCHK(glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length));
    if (status == GL_FALSE)
        fprintf(stderr, "ERROR: ImGui_ImplOpenGL3_CreateDeviceObjects: failed to link %s!\n", desc);
    if (log_length > 0)
    {
        ImVector<char> buf;
        buf.resize((int)(log_length + 1));
        GLCHK(glGetProgramInfoLog(handle, log_length, NULL, (GLchar*)buf.begin()));
        fprintf(stderr, "%s\n", buf.begin());
    }
    return status == GL_TRUE;
}

bool    ImGui_SetupShaderData(ShaderData& shaderData, const GLchar* vertex_shader, const GLchar* fragment_shader)
{
    // Create shaders
    const GLchar* vertex_shader_with_version[2] = { g_GlslVersionString, vertex_shader };
    shaderData.VertHandle = glCreateShader(GL_VERTEX_SHADER);
    GLCHK(glShaderSource(shaderData.VertHandle, 2, vertex_shader_with_version, NULL));
    GLCHK(glCompileShader(shaderData.VertHandle));
    CheckShader(shaderData.VertHandle, "vertex shader");

    const GLchar* fragment_shader_with_version[2] = { g_GlslVersionString, fragment_shader };
    shaderData.FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
    GLCHK(glShaderSource(shaderData.FragHandle, 2, fragment_shader_with_version, NULL));
    GLCHK(glCompileShader(shaderData.FragHandle));
    CheckShader(shaderData.FragHandle, "fragment shader");

    shaderData.ShaderHandle = glCreateProgram();
    GLCHK(glAttachShader(shaderData.ShaderHandle, shaderData.VertHandle));
    GLCHK(glAttachShader(shaderData.ShaderHandle, shaderData.FragHandle));
    GLCHK(glLinkProgram(shaderData.ShaderHandle));
    CheckProgram(shaderData.ShaderHandle, "shader program");

    shaderData.AttribLocationTex = glGetUniformLocation(shaderData.ShaderHandle, "Texture");
    shaderData.AttribLocationTexY = glGetUniformLocation(shaderData.ShaderHandle, "TextureY");
    shaderData.AttribLocationTexU = glGetUniformLocation(shaderData.ShaderHandle, "TextureU");
    shaderData.AttribLocationTexV = glGetUniformLocation(shaderData.ShaderHandle, "TextureV");
    shaderData.AttribLocationProjMtx = glGetUniformLocation(shaderData.ShaderHandle, "ProjMtx");
    shaderData.AttribLocationPosition = glGetAttribLocation(shaderData.ShaderHandle, "Position");
    shaderData.AttribLocationUV = glGetAttribLocation(shaderData.ShaderHandle, "UV");
    shaderData.AttribLocationColor = glGetAttribLocation(shaderData.ShaderHandle, "Color");
    return true;
}

bool    ImGui_ImplOpenGL3_CreateDeviceObjects()
{
    {
        const GLchar* vertex_shader =
                "uniform highp mat4 ProjMtx;\n" 
                "attribute highp vec2 Position;\n"
                "attribute highp vec2 UV;\n"
                "attribute lowp vec4 Color;\n"
                "varying highp vec2 Frag_UV;\n"
                "varying lowp vec4 Frag_Color;\n"
                "void main()\n"
                "{\n"
                "    Frag_UV = UV;\n"
                "    Frag_Color = Color;\n"
                "    highp vec4 p = ProjMtx * vec4(Position.xy, 0.0, 1.0);\n"
                "    gl_Position = p;\n"
                "}\n";

        const GLchar* fragment_shader =
                "uniform lowp sampler2D Texture;\n"
                "varying highp vec2 Frag_UV;\n"
                "varying lowp vec4 Frag_Color;\n"
                "void main()\n"
                "{\n"
                "    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV);\n"
                "}\n";

        ImGui_SetupShaderData(g_ShaderData, vertex_shader, fragment_shader);
    }
    {
        const GLchar* vertex_shader =
                "uniform highp mat4 ProjMtx;\n" 
                "attribute highp vec2 Position;\n"
                "attribute highp vec2 UV;\n"
                "attribute lowp vec4 Color;\n"
                "varying highp vec2 Frag_UV;\n"
                "varying lowp vec4 Frag_Color;\n"
                "void main()\n"
                "{\n"
                "    Frag_UV = UV;\n"
                "    Frag_Color = Color;\n"
                "    highp vec4 p = ProjMtx * vec4(Position.xy, 0.0, 1.0);\n"
                "    gl_Position = p;\n"
                "}\n";

        const GLchar* fragment_shader =
                "uniform lowp sampler2D TextureY;\n"
                "uniform lowp sampler2D TextureU;\n"
                "uniform lowp sampler2D TextureV;\n"
                "varying highp vec2 Frag_UV;\n"
                "varying lowp vec4 Frag_Color;\n"
                "void main()\n"
                "{\n"
                "   lowp float y = texture2D(TextureY, Frag_UV).r;\n"
                "   lowp float u = texture2D(TextureU, Frag_UV).r;\n"
                "   lowp float v = texture2D(TextureV, Frag_UV).r;\n"
                "   u = u - 0.5;\n"
                "   v = v - 0.5;\n"
                "   lowp float r = y + v * 1.4;\n"
                "   lowp float g = y + u * -0.343 + v * -0.711;\n"
                "   lowp float b = y + u * 1.765;\n"
                "   gl_FragColor = Frag_Color * vec4(r, g, b, 1.0);\n"
                "}\n";

        ImGui_SetupShaderData(g_ShaderDataVideo, vertex_shader, fragment_shader);
    }

    // Create buffers
    GLCHK(glGenBuffers(1, &g_VboHandle));
    GLCHK(glGenBuffers(1, &g_ElementsHandle));

    ImGui_ImplOpenGL3_CreateFontsTexture();

    return true;
}

void    ImGui_DestroyShaderData(ShaderData& shaderData)
{
    if (shaderData.ShaderHandle && shaderData.VertHandle) glDetachShader(shaderData.ShaderHandle, shaderData.VertHandle);
    if (shaderData.VertHandle) glDeleteShader(shaderData.VertHandle);
    shaderData.VertHandle = 0;

    if (shaderData.ShaderHandle && shaderData.FragHandle) glDetachShader(shaderData.ShaderHandle, shaderData.FragHandle);
    if (shaderData.FragHandle) glDeleteShader(shaderData.FragHandle);
    shaderData.FragHandle = 0;

    if (shaderData.ShaderHandle) glDeleteProgram(shaderData.ShaderHandle);
    shaderData.ShaderHandle = 0;
}

void    ImGui_ImplOpenGL3_DestroyDeviceObjects()
{
    if (g_VboHandle) glDeleteBuffers(1, &g_VboHandle);
    if (g_ElementsHandle) glDeleteBuffers(1, &g_ElementsHandle);
    g_VboHandle = g_ElementsHandle = 0;

    ImGui_DestroyShaderData(g_ShaderData);
    ImGui_DestroyShaderData(g_ShaderDataVideo);

    ImGui_ImplOpenGL3_DestroyFontsTexture();
}
