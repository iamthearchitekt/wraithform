#pragma once

namespace PhaseCorrelationShader {

const char *vertexShader = R"(
    #version 130
    in vec2 position;
    uniform float scale;
    uniform vec2 u_resolution;
    uniform vec2 u_offset;
    
    void main() {
        float L = position.x;
        float R = position.y;
        
        float mid = (L + R) * 0.7071 * scale;
        
        // Use (R - L) so Left audio points left (negative X) and Right audio points right.
        // Multiply by an aesthetic stereo booster (1.6x) so the horizontal dynamics are much more visible!
        float side = (R - L) * 0.7071 * scale * 1.6;
        
        // Scale factor (1.0x) so it perfectly fits the scope bounds without spilling over into other UI elements
        vec2 normPos = vec2(side, mid) * 1.0;

        gl_Position = vec4((normPos * 0.95) + u_offset, 0.0, 1.0);
        gl_PointSize = 12.0;
    }
)";

const char *fragmentShader = R"(
    #version 130
    out vec4 FragColor;
    uniform vec4 color;
    
    void main() {
        vec2 coord = gl_PointCoord - vec2(0.5);
        float dist = length(coord);
        
        float coreDist = dist * 2.0; 
        float coreAlpha = smoothstep(0.5, 0.0, coreDist);
        coreAlpha = pow(coreAlpha, 0.3); // More solid core
        
        float glowAlpha = smoothstep(0.5, 0.0, dist);
        glowAlpha = pow(glowAlpha, 1.2) * 1.5; // Stronger glow
        
        float alpha = min(coreAlpha + glowAlpha, 1.0);
        FragColor = vec4(color.rgb, color.a * alpha * 1.5); // Boost total opacity
    }
)";

} // namespace PhaseCorrelationShader
