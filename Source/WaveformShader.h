#pragma once

const static char *seratoFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    varying vec2 v_uv;
    uniform sampler2D u_history; // R: Amp, G: Bass, B: Mid, A: High
    uniform float u_time;
    uniform vec2 u_resolution;

    void main()
    {
        vec2 uv = v_uv;
        
        // Horizontal scan (history index)
        vec4 data = texture2D(u_history, vec2(uv.x, 0.5));
        float amp = data.r;
        float bass = data.g;
        float mid = data.b;
        float high = data.a;

        // Mirroring and Scaling
        float dist = abs(uv.y - 0.5) * 2.0;

        // Apply a gain boost for visual punch
        float scaledAmp = clamp(amp * 1.5, 0.0, 1.0);
        
        if (dist > scaledAmp) {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        // Sophisticated Dark Pale Cyan Mapping (Vibrant & Desaturated)
        vec3 bassColor = vec3(0.3, 0.5, 0.5);   // Desaturated Dark Cyan for Bass
        vec3 midColor  = vec3(0.5, 0.75, 0.75); // Pale Steel Cyan for Mid
        vec3 highColor = vec3(0.835, 1.0, 1.0);  // Brand Ice Blue (#D5FFFF)
        
        float total = bass + mid + high + 0.0001;
        vec3 spectralColor = (bass * bassColor + mid * midColor + high * highColor) / total;
        
        // Intensity mapping: Inner core is brighter and punchier
        float intensity = 1.0 - (dist / (scaledAmp + 0.0001));
        vec3 finalColor = spectralColor * (0.4 + 0.6 * pow(intensity, 0.5));
        
        // Add a subtle electric glow to the highest peaks
        if (amp > 0.8) {
            finalColor += vec3(0.1, 0.2, 0.2) * sin(u_time * 10.0);
        }

        gl_FragColor = vec4(finalColor, 1.0);
    }
)glsl";
