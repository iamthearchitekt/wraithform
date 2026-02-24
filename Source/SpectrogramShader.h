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
        // Tint: Preserve original details but wash with high-gain theme color
        float luma = dot(col.rgb, vec3(0.299, 0.587, 0.114));
        
        // HIGH-GAIN ADDITIVE: Create a vibrant "glow" pass using the theme color
        vec3 glow = u_tintColor * luma * 2.5;
        
        // Combine: preserve source colors but push highlights into the theme
        vec3 finalCol = mix(col.rgb, glow, 0.45);
        finalCol += glow * 0.35; // Additive boost for "brightness"
        
        // Final exposure pop
        gl_FragColor = vec4(finalCol * 1.15, col.a);
    }
)glsl";
