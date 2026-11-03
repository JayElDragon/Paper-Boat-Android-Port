@prism(type='fragment', name='CRT Post Pass', version='1.0.0', description='CRT look filter', author='@Lywx')

@if(BACKEND_VULKAN)
#version 450

@include("shaders/vulkan/include/common.glsli")

@if(VERTEX_SHADER)
    @include("shaders/vulkan/include/fast3d_vs.glsli")

@else
    layout(location = 0) out vec4 vOutColor;
    @if(o_textures[0])
        layout(location = 0) in vec2 vTexCoord0;
        layout(set = 1, binding = 0) uniform sampler2D uTex0;
    @end

    #define CRT_SAMPLE(uv) texture(uTex0, uv)
    #define CRT_OUT vOutColor
    #define CRT_BODY 1
    @include("shaders/post/crt_body.glsli")
@end

@else
@{GLSL_VERSION}

@if(VERTEX_SHADER)
    @include("shaders/opengl/include/fast3d_vs.glsli")

@else
    @if(core_opengl || opengles)
    out vec4 vOutColor;
    #define CRT_OUT vOutColor
    @else
    #define CRT_OUT gl_FragColor
    @end

    @if(o_textures[0])
        @{attr} vec2 vTexCoord0;
        uniform sampler2D uTex0;
    @end
    // Must match fast3d_vs.glsli exactly (size and, on GLES, highp), or the
    // program fails to link and the pass draws nothing.
    @if(opengles)
    uniform highp vec4 uCustom[32];
    @else
    uniform vec4 uCustom[32];
    @end

    #define CRT_SAMPLE(uv) @{texture}(uTex0, uv)
    #define CRT_BODY 1
    @include("shaders/post/crt_body.glsli")
@end
@end
