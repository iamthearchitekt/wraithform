#pragma once

const static char *spectrogramFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform sampler2D u_texture;
    varying vec2 v_uv;

    void main()
    {
        // Simple texture lookup. No logic. No complex math.
        // We assume the texture is prepared on the CPU/Host side.
        gl_FragColor = texture2D(u_texture, v_uv);
    }
)glsl";
