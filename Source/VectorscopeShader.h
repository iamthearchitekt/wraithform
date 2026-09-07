#pragma once

namespace VectorscopeShader {

const char *vertexShader = R"(
    #version 130
    in vec2 position;
    uniform float scale;
    uniform vec2 u_resolution;
    uniform int u_plotMode; // 0 = Goniometer M/S, 1 = Classic Lissajous L/R
    uniform float u_bassEnergy;
    uniform float u_kickEnergy;
    
    void main() {
        float L = position.x * scale;
        float R = position.y * scale;
        
        // Dynamic Bass Reactivity:
        // On kick/sub hits, bassPump expands the entire orbital field outward!
        float bassPump = 0.85 + u_bassEnergy * 0.45 + u_kickEnergy * 0.30;
        
        vec2 normPos;
        if (u_plotMode == 1) {
            // Classic Dual-Channel Lissajous X-Y Scope (Unrotated, completely isotropic):
            // Horizontal (X) = Left Channel, Vertical (Y) = Right Channel.
            // Mono (L = R) forms a balanced +45 deg diagonal.
            // Stereo blooms into an expansive, 360-degree orbital ellipse / cloud with no vertical mono bias.
            normPos = vec2(L, R) * 0.75 * bassPump;
        } else {
            // Standard 45-degree Goniometer with Stereo Field Expansion:
            // Mid (Mono) along vertical Y, Side (Stereo) along horizontal X.
            float mid  = (L + R) * 0.55 * bassPump;
            // 2.2x Stereo Expansion with dynamic bass response:
            float side = (R - L) * 0.55 * (1.8 + u_bassEnergy * 0.8) * bassPump;
            normPos = vec2(side, mid) * 0.75;
        }
        
        gl_Position = vec4(normPos, 0.0, 1.0);
    }
)";

const char *fragmentShader = R"(
    #version 130
    out vec4 FragColor;
    uniform vec4 color;
    
    void main() {
        FragColor = color;
    }
)";

const char *glowVertexShader = R"(
    #version 130
    in vec2 position;
    out vec2 v_uv;
    void main() {
        v_uv = position * 0.5 + 0.5; // Map from [-1, 1] to [0, 1]
        gl_Position = vec4(position, 0.0, 1.0);
    }
)";

const char *glowFragmentShader = R"(
    #version 130
    in vec2 v_uv;
    out vec4 FragColor;
    uniform vec3 u_glowColor;
    uniform float u_audioEnergy;

    void main() {
        vec2 centered = v_uv - 0.5;
        float d = length(centered) * 2.0; // 0 at center, 1.0 at edge of inscribed circle
        
        // High-contrast, authentic CRT oscilloscope phosphor faceplate
        vec3 crtGlass = vec3(0.010, 0.012, 0.015);
        
        // Subtle circular vignette (darkens towards the CRT bezel)
        float vignette = smoothstep(1.0, 0.25, d);
        
        // Very subtle phosphor background warmth (1.5% to 3% theme color, breathes gently with audio)
        vec3 phosphorTint = u_glowColor * (0.012 + u_audioEnergy * 0.02) * vignette;
        
        // Outer bezel rim shadow (fades to black at viewport edge)
        float bezel = smoothstep(1.0, 0.90, d);
        
        vec3 col = (crtGlass + phosphorTint) * (1.0 - bezel * 0.75);
        
        FragColor = vec4(col, 1.0);
    }
)";

} // namespace VectorscopeShader
