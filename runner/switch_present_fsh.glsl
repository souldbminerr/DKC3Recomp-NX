#version 460

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

// The texture image is RGBA8 with a Blue/Green/Red/One view swizzle, so
// the SNES BGRX memory bytes arrive here as already-correct RGB.
layout (binding = 0) uniform sampler2D tex;

void main()
{
    outColor = texture(tex, inUV);
}
