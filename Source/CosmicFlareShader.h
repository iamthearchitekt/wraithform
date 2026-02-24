#pragma once

/**
    CosmicFlare by mojovideotech
    based on : Flaring by nimitz
    Ported to GLSL for WraithForm
*/

const static char *cosmicFlareFragmentShader = R"glsl(
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform float u_time;
    uniform vec2  u_resolution;
    uniform float u_audioEnergy;  // 0..1 bass energy
    uniform vec3  u_glowColor;    // Active theme color (Plasma, UV, etc.)

    // Parameters for Hyper-Contrast Wraith Aesthetic
    // HYPER-DRAMATIC RANGE: Stays dark until the burst hits
    float pulse = pow(u_audioEnergy, 2.5); 
    float core_burst = pow(u_audioEnergy, 1.4) * 38.0; 
    
    // Fill Screen & Visual balance
    float base_brightness = 0.05 + pulse * 2.2; 
    float ray_intensity = pulse * 1.8;         
    float ray_density = 7.5;
    float freq = 6.0;
    float curvature = 380.0;
    float spot_size = 0.1;                    

    float hash(float n) { return fract(sin(n) * 437.5453); }

    float noise(in vec2 x) {
        vec2 p = floor(x);
        vec2 f = fract(x);
        f = f * f * (3.0 - 2.0 * f);
        float n = p.x + p.y * 57.0;
        return mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
                   mix(hash(n + 57.0), hash(n + 58.0), f.x), f.y);
    }

    mat2 m2 = mat2(0.80, 0.60, -0.60, 0.80);

    float fbm(in vec2 p) {
        float f = 0.0;
        float amp = 0.5;
        for (int i = 0; i < 5; i++) {
            f += amp * (sin(noise(p) * freq) * 0.5 + 0.5);
            p = p * 2.1 * m2;
            amp *= 0.5;
        }
        return f;
    }

    void main() {
        // Correct aspect and center-aligned
        vec2 uv = (gl_FragCoord.xy / u_resolution.xy) - 0.5;
        uv.x *= u_resolution.x / u_resolution.y;
        
        // SIDEBAR OFFSET: Adjusted for right sidebar centering
        uv.x += 0.075; 

        uv *= (curvature * 0.05 + 0.0001);

        float r = length(uv);
        
        // SLOW MORPHOUS EDGES: Constant ethereal creep
        float slow_morph = -u_time * 0.25; 
        
        float x = dot(normalize(uv), vec2(0.5, 0.0)) + slow_morph;
        float y = dot(normalize(uv), vec2(0.0, 0.5)) + slow_morph;

        // Ethereal Morphing (Slow & Liquid)
        float wx = fbm(vec2(y * ray_density * 0.35, r + x * ray_density * 0.1));
        float wy = fbm(vec2(r + y * ray_density * 0.05, x * ray_density * 0.25));
        
        float val = fbm(vec2(r + wy * ray_density, r + wx * ray_density));
        
        // SHARP CONTRAST: High-threshold for liquid look
        val = smoothstep(0.5, 0.99 + pulse * 0.01, val);
        
        // Color Mapping: Reactive Theme Color
        float radial_falloff = smoothstep(10.0, 1.5, r); 
        vec3 baseTheme = u_glowColor; // Use theme color directly for variants
        
        vec3 col = val * baseTheme * ray_intensity * radial_falloff;
        
        // LAYERED CORE: White hot center with a "melting" Theme Halo
        float core_falloff = smoothstep(spot_size + pulse * 0.4, spot_size - 0.1, r / 15.0);
        float halo_falloff = smoothstep(spot_size * 2.5 + pulse * 0.6, spot_size - 0.2, r / 15.0);
        
        vec3 core_white = vec3(0.95, 1.0, 1.0) * core_falloff * (1.5 + core_burst); 
        vec3 core_halo  = baseTheme * halo_falloff * (1.0 + core_burst * 0.5); 
        
        // Composite
        col += core_halo * 0.8;  // Colored melt (follows theme)
        col += core_white * 1.0; // Sharp white pulse
        
        // Ambient Bloom
        col += baseTheme * 0.05 / (r + 0.8);
        
        // Final exposure control: Hyper-Dramatic Scaling
        col *= base_brightness;
        col = clamp(col, 0.0, 2.5); 
        
        gl_FragColor = vec4(sqrt(col), 1.0);
    }
)glsl";
