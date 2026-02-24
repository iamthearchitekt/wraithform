#pragma once

const static char *visualizerFragmentShader = R"glsl(
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform sampler2D u_audioData; // 1D texture containing history
    
    // Constants
    const float thickness = 0.008;
    const float gain = 2.25;      // More aggressive peaks
    const float lowpass = 0.75;
    const float glow = 1.8;      // Supercharged for electric energy
    const float glowSize = 1.6;
    const float window = 0.018;

    // Color palette driven by C++ uniform (ColorMode)
    uniform vec3 u_glowColor;

    float sampleWave(float x) {
        return texture2D(u_audioData, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
    }

    varying vec2 v_uv;

    // --- Fractal Noise for Saber-style distortion ---
    float hash(vec2 p) {
        return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
    }
    float noise(vec2 p) {
        vec2 i = floor(p); vec2 f = fract(p);
        f = f*f*(3.0-2.0*f);
        return mix(mix(hash(i + vec2(0,0)), hash(i + vec2(1,0)), f.x),
                   mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
    }
    float fbm(vec2 p) {
        float v = 0.0; float a = 0.5;
        for (int i = 0; i < 3; i++) {
            v += a * noise(p); p *= 2.0; a *= 0.5;
        }
        return v;
    }

    void main()
    {
        vec2 uv = v_uv;
        
        // --- low-frequency smoothing for the base wave ---
        float w0 = sampleWave(uv.x);
        float dx = window;
        float wSm = sampleWave(uv.x - 4.0*dx) + sampleWave(uv.x - 3.0*dx) +
                    sampleWave(uv.x - 2.0*dx) + sampleWave(uv.x - 1.0*dx) +
                    sampleWave(uv.x) + sampleWave(uv.x + 1.0*dx) +
                    sampleWave(uv.x + 2.0*dx) + sampleWave(uv.x + 3.0*dx) +
                    sampleWave(uv.x + 4.0*dx);
        wSm /= 9.0;
        float w = mix(w0, wSm, lowpass);

        float amp = (w - 0.5) * 2.0 * gain;
        float y = 0.5 + amp * 0.49;
        float energy = clamp(abs(amp), 0.0, 1.0);
        float thick = thickness * (1.0 + energy * 0.85);

        // --- Core Electric Bolt (Solid & Sharp) ---
        float dCore = abs(uv.y - y);
        float line = 1.0 - smoothstep(0.0, thick, dCore);
        float whiteCore = pow(line, 24.0); 

        // --- Saber-Style Turbulent Heat Distortion (Glow Only) ---
        // Scroll noise upwards/sideways to simulate rising heat
        vec2 noiseUV = uv * vec2(8.0, 4.0) + vec2(u_time * 1.5, -u_time * 4.0);
        float distortion = fbm(noiseUV) * 0.025 * (0.5 + 0.5 * energy);
        
        float dGlow = abs(uv.y - (y + distortion));

        // Glow layers (Theme colored, distorted)
        float innerGlow = 1.0 - smoothstep(0.0, thick * (1.8 * glowSize), dGlow);
        float outerGlow = 1.0 - smoothstep(0.0, thick * (3.5 * glowSize), dGlow);

        float g = glow * (0.6 + 0.8 * energy);
        float themeIntensity = clamp(innerGlow * (0.85 * g) + outerGlow * (0.12 * g), 0.0, 1.0);
        
        // --- Shading ---
        vec3 boltColor = mix(u_glowColor * line, vec3(1.0), whiteCore);
        
        // Background Glow
        float pulseEnergy = (sampleWave(0.2) + sampleWave(0.5) + sampleWave(0.8)) / 3.0;
        pulseEnergy = clamp(abs((pulseEnergy - 0.5) * 2.0), 0.0, 1.0);
        float bgGlow = (1.0 - smoothstep(0.0, 0.45 + 0.15*pulseEnergy, abs(uv.y - 0.5))) * 0.22 * (0.5 + 0.5*pulseEnergy);

        vec3 finalColor = boltColor + (u_glowColor * themeIntensity) + (u_glowColor * bgGlow);
        gl_FragColor = vec4(finalColor, 1.0);
    }
)glsl";
