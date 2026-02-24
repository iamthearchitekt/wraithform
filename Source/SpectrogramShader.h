#pragma once

const static char *spectrogramFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform sampler2D u_texture;
    uniform vec3 u_tintColor;
    varying vec2 v_uv;

    void main()
    {
        vec4 col = texture2D(u_texture, v_uv);
        // Tint: preserve brightness, shift hue toward active theme color
        float luma = dot(col.rgb, vec3(0.299, 0.587, 0.114));
        vec3 tinted = mix(col.rgb, u_tintColor * luma * 1.5, 0.55);
        gl_FragColor = vec4(tinted, col.a);
    }
)glsl";
