#pragma once

const static char *seratoFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    varying vec2 v_uv;
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
        vec4 data = texture2D(u_history, vec2(zoomIdx, 0.5));
        
        float amp  = data.r;
        float bass = data.g;
        float mid  = data.b;
        float high = data.a;

        float dist      = abs(uv.y - 0.5) * 2.0;
        float scaledAmp = clamp(amp * 1.5, 0.0, 1.0);

        // Outer halo extends past the waveform edge
        float haloReach = scaledAmp + 0.18 * amp;

        if (dist > haloReach) {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Spectral gradient derived from theme color
        vec3 highColor = u_glowColor;
        vec3 midColor  = mix(vec3(0.0), u_glowColor, 0.6);
        vec3 bassColor = mix(vec3(0.0), u_glowColor, 0.3);

        float total       = bass + mid + high + 0.0001;
        vec3 spectralColor = (bass * bassColor + mid * midColor + high * highColor) / total;

        // Core Rendering
        vec3 finalColor;
        if (dist <= scaledAmp) {
            float intensity = 1.0 - (dist / (scaledAmp + 0.0001));
            float bright    = 0.7 + 0.8 * pow(intensity, 0.35); 
            finalColor = spectralColor * bright;
            finalColor += u_glowColor * pow(intensity, 4.0) * 0.5;
        } else {
            // Halo zone
            float t = 1.0 - clamp((dist - scaledAmp) / (haloReach - scaledAmp + 0.0001), 0.0, 1.0);
            finalColor = u_glowColor * pow(t, 2.0) * 0.4 * amp;
        }

        // Electric flicker on loud transients
        finalColor += u_glowColor * max(0.0, amp - 0.6) * 0.3
                    * (0.5 + 0.5 * sin(u_time * 15.0 + amp * 10.0));

        gl_FragColor = vec4(finalColor, 1.0);
    }
)glsl";
