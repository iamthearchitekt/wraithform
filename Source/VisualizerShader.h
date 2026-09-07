#pragma once

const static char *visualizerFragmentShader = R"glsl(
    #version 130

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform sampler2D u_audioData;
    uniform float u_audioEnergy;

    const float thickness = 0.012;
    const float gain      = 1.15;
    const float lowpass   = 0.75;
    const float glow      = 0.6;
    const float glowSize  = 3.0;
    const float window    = 0.018;

    uniform vec3 u_glowColor;
    in vec2 v_uv;
    out vec4 fragColor;

    float sampleWave(float x) {
        return texture(u_audioData, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
    }

    void main()
    {
        vec2 uv = v_uv;

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

        float w   = mix(w0, wSm, lowpass);
        float amp = (w - 0.5) * 2.0 * gain;
        float y   = 0.5 + amp * 0.40;

        float energy = clamp(abs(amp), 0.0, 1.0);
        float thick  = thickness * (1.0 + energy * 1.5);
        float d      = abs(uv.y - y);

        float line      = 1.0 - smoothstep(0.0, thick, d);
        float innerGlow = 1.0 - smoothstep(0.0, thick * (1.8 * glowSize), d);
        float outerGlow = 1.0 - smoothstep(0.0, thick * (3.2 * glowSize), d);

        float g         = glow * (0.6 + 0.8 * energy);
        float intensity = line
                        + innerGlow * (0.25 * g)
                        + outerGlow * (0.10 * g);
        intensity = clamp(intensity, 0.0, 1.0);

        float pulseEnergy = clamp(u_audioEnergy, 0.0, 1.0);
        float dCenter = abs(uv.y - 0.5);
        float bgGlow  = (1.0 - smoothstep(0.0, 0.5 + 0.2 * pulseEnergy, dCenter))
                      * 0.15 * (0.5 + 0.5 * pulseEnergy);

        vec3 finalColor = u_glowColor * intensity + u_glowColor * bgGlow;
        fragColor = vec4(finalColor, 1.0);
    }
)glsl";
