#pragma once

const static char *spectrogramFragmentShader = R"glsl(
    #version 130
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform sampler2D u_texture;
    uniform vec3 u_tintColor;
    in vec2 v_uv;
    out vec4 fragColor;

    void main()
    {
        vec2 flippedUV = vec2(v_uv.x, 1.0 - v_uv.y);
        vec4 col = texture(u_texture, flippedUV);
        
        // Extract intensity (luminance) from the CPU-rendered texture
        float val = dot(col.rgb, vec3(0.299, 0.587, 0.114));
        
        // Small gamma bump for midrange visibility
        val = pow(val, 0.78);
        
        // Create mid and peak colors dynamically from the tint
        vec3 midCol = u_tintColor * 0.8; // Rich base color
        vec3 peakCol = clamp(mix(u_tintColor * 1.8, vec3(1.0), 0.20), 0.0, 1.0); // Vivid highlights
        
        vec3 finalCol = vec3(0.0);
        if (val > 0.01) {
            if (val < 0.4) {
                finalCol = mix(vec3(0.0), midCol, val * 2.5);
            } else {
                finalCol = mix(midCol, peakCol, (val - 0.4) * 1.66);
            }
        }
        
        // Final punchy exposure
        fragColor = vec4(clamp(finalCol * 1.2, 0.0, 1.0), col.a);
    }
)glsl";
