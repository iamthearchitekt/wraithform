#pragma once

const static char *visualizerFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform sampler2D u_audioData; // 1D texture containing history
    
    // Constants - SMOOTH (Restored)
    const float thickness = 0.012;
    const float gain = 2.0;
    const float lowpass = 0.75;
    const float glow = 0.6;
    const float glowSize = 3.0;
    const float window = 0.018;

    // Ice Blue brand color (#D5FFFF)
    const vec3 glowColor = vec3(0.835, 1.0, 1.0); 

    float sampleWave(float x) {
        // Texture is 1D: (x, 0). 
        // Assuming texture coordinates 0..1 map to the audio history.
        return texture2D(u_audioData, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
    }

    varying vec2 v_uv;

    void main()
    {
        vec2 uv = v_uv;
        
        // --- low-frequency smoothing ---
        float w0 = sampleWave(uv.x);

        float dx = window;
        float wSm =
            sampleWave(uv.x - 4.0*dx) +
            sampleWave(uv.x - 3.0*dx) +
            sampleWave(uv.x - 2.0*dx) +
            sampleWave(uv.x - 1.0*dx) +
            sampleWave(uv.x) +
            sampleWave(uv.x + 1.0*dx) +
            sampleWave(uv.x + 2.0*dx) +
            sampleWave(uv.x + 3.0*dx) +
            sampleWave(uv.x + 4.0*dx);

        wSm /= 9.0;

        float w = mix(w0, wSm, lowpass);

        float amp = (w - 0.5) * 2.0;
        amp *= gain;

        float y = 0.5 + amp * 0.40;

        float energy = clamp(abs(amp), 0.0, 1.0);
        float thick = thickness * (1.0 + energy * 1.5);

        float d = abs(uv.y - y);

        // Core line
        float line = 1.0 - smoothstep(0.0, thick, d);

        // Glow layers
        float innerGlow = 1.0 - smoothstep(0.0, thick * (1.8 * glowSize), d);
        float outerGlow = 1.0 - smoothstep(0.0, thick * (3.2 * glowSize), d);

        float g = glow * (0.6 + 0.8 * energy);

        float intensity = line;
        intensity += innerGlow * (0.25 * g);
        intensity += outerGlow * (0.10 * g);

        intensity = clamp(intensity, 0.0, 1.0);

        // Background Ghostly Glow (Center Pulsing)
        // Calculate average energy from a few points to drive the pulse
        float pulseEnergy = (sampleWave(0.2) + sampleWave(0.5) + sampleWave(0.8)) / 3.0;
        pulseEnergy = clamp(abs((pulseEnergy - 0.5) * 2.0), 0.0, 1.0);
        
        // Vertical distance from center (y=0.5)
        float dCenter = abs(uv.y - 0.5);
        
        // Soft, wide glow
        float bgGlow = 1.0 - smoothstep(0.0, 0.5 + 0.2 * pulseEnergy, dCenter);
        // Make it subtle and ghostly
        bgGlow *= 0.15 * (0.5 + 0.5 * pulseEnergy); 
        
        vec3 finalColor = glowColor * intensity + glowColor * bgGlow;

        gl_FragColor = vec4(finalColor, 1.0);
    }
)glsl";
