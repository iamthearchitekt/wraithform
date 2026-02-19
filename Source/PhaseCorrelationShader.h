#pragma once

namespace PhaseCorrelationShader {

const char *vertexShader = R"(
    attribute vec2 position; // Input: L (x) and R (y) audio samples [-1, 1]
    uniform float scale; // Scaling factor (zoom)
    uniform vec2 u_resolution;
    
    void main() {
        // Standard Goniometer Rotation (45 degrees)
        // M = L + R (Mid/Vertical)
        // S = L - R (Side/Horizontal)
        
        // We want Mid to be Y axis, Side to be X axis.
        
        // Correcting presumed 0..1 range to -0.5..0.5 (effectively -1..1 logic)
        float L = position.x - 0.5;
        float R = position.y - 0.5;
        
        float mid = (L + R) * 0.7071 * scale; // Y
        float side = (L - R) * 0.7071 * scale; // X
        
        // Aspect ratio correction? 
        // We usually want a square aspect for the goniometer regardless of window shape
        // checking resolution...
        float aspect = u_resolution.x / u_resolution.y;
        
        vec2 normPos;
        // Keeping it simple for now (1:1 aspect on data)
        // If we want square logic for non-square windows:
        // if (aspect > 1.0) normPos = vec2(side / aspect, mid);
        // else normPos = vec2(side, mid * aspect);
        
        normPos = vec2(side, mid);

        // Position is now centered at 0,0 locally
        gl_Position = vec4(normPos.x, normPos.y, 0.0, 1.0);
        gl_PointSize = 4.0;
    }
)";

const char *fragmentShader = R"(
    uniform vec4 color;
    
    void main() {
        // Driver issue: gl_PointCoord seems to fail on this hardware (likely 0,0), causing discard.
        // Reverting to solid squares to ensure visibility.
        // vec2 coord = gl_PointCoord - vec2(0.5);
        // if(dot(coord, coord) > 0.25) discard;
        
        gl_FragColor = color;
    }
)";

} // namespace PhaseCorrelationShader
