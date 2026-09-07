#pragma once

const static char *seratoFragmentShader = R"glsl(
    #version 130
    #ifdef GL_ES
    precision mediump float;
    #endif

    in vec2 v_uv;
    out vec4 fragColor;
    uniform sampler2D u_history; // R: Amp, G: Bass, B: Mid, A: High
    uniform float u_time;
    uniform vec2 u_resolution;
    uniform vec3 u_glowColor; 
    uniform float u_zoom;        // Horizontal zoom factor

    void main()
    {
        vec2 uv = v_uv;

        // Horizontal scan with zoom: focus on the latest data (right side)
        float zoomIdx = 1.0 - (1.0 - uv.x) / max(0.1, u_zoom);
        vec4 data = texture(u_history, vec2(zoomIdx, 0.5));
        
        float amp  = data.r;
        float bass = data.g;
        float mid  = data.b;
        float high = data.a;

        float dist      = abs(uv.y - 0.5) * 2.0;
        float scaledAmp = clamp(amp * 1.5, 0.0, 1.0);

        // Outer halo extends past the waveform edge
        float haloReach = scaledAmp + 0.18 * amp;

        if (dist > haloReach) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Horizontal smoothing (multi-tap average) to make the waveform shape much more coherent
        float dx = 1.5 / u_resolution.x; 
        float a1 = texture(u_history, vec2(zoomIdx - dx * 2.0, 0.5)).r;
        float a2 = texture(u_history, vec2(zoomIdx - dx, 0.5)).r;
        float a3 = amp;
        float a4 = texture(u_history, vec2(zoomIdx + dx, 0.5)).r;
        float a5 = texture(u_history, vec2(zoomIdx + dx * 2.0, 0.5)).r;
        float smoothAmp = (a1 + a2 * 2.0 + a3 * 3.0 + a4 * 2.0 + a5) / 9.0;

        // Monochromatic theming as requested, using the global tint
        vec3 baseColor = u_glowColor;

        // Core Rendering using smooth anti-aliased edges instead of hard clipping
        vec3 finalColor = vec3(0.0);
        
        // Soften and scale the amplitude so loud tracks don't clip into a solid block
        scaledAmp = clamp(pow(smoothAmp, 0.8) * 0.85, 0.0, 0.95);
        
        // Use smoothstep for anti-aliasing the edges of the waveform
        float waveformMask = smoothstep(scaledAmp + 0.02, scaledAmp - 0.02, dist);
        
        // 1. Solid base fill with a slight gradient towards the center
        float coreIntensity = 1.0 - (dist / (scaledAmp + 0.001));
        finalColor += baseColor * waveformMask * (0.5 + 0.5 * coreIntensity);
        
        // 2. Add a bright, crisp edge envelope to clearly define the dynamics/shape (thicker for coherence)
        float envelopeEdge = smoothstep(scaledAmp - 0.08, scaledAmp, dist) * smoothstep(scaledAmp + 0.08, scaledAmp, dist);
        finalColor += baseColor * envelopeEdge * 1.8;
        
        // 3. Bright centerline with extended CORE BLEED
        float centerLine = smoothstep(0.06, 0.0, dist);
        float coreBleed  = exp(-dist * 10.0); // Bleeds beautifully outwards
        finalColor += mix(baseColor, vec3(1.0), 0.8) * (centerLine * 1.2 + coreBleed * 0.6);

        // 4. Subtle outer glow so the waveform pops
        if (dist > scaledAmp) {
            float glow = exp(-(dist - scaledAmp) * 6.0) * 0.4 * smoothAmp;
            finalColor += baseColor * glow;
        }

        // Electric flicker on loud transients
        finalColor += baseColor * max(0.0, smoothAmp - 0.7) * 0.6
                    * (0.5 + 0.5 * sin(u_time * 20.0));

        fragColor = vec4(finalColor, 1.0);
    }
)glsl";
