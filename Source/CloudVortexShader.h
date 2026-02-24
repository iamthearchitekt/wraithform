#pragma once

// CloudVortex by mojovideotech (based on Protean Clouds by nimitz)
// Ported from ISF to GLSL for JUCE/OpenGL
// Fixed for smooth, jitter-free, slow-swirling motion.

const static char *cloudVortexFragmentShader = R"glsl(
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform float u_time;
    uniform vec2  u_resolution;
    uniform float u_audioEnergy; // 0..1 bass energy
    uniform vec3  u_glowColor;   // Active theme color
    uniform float u_cloudT;      // Accumulated tunnel time (speed-integrated in C++)

    // Runtime globals set in main() before any function call
    float T;
    float prm1;

    // Pre-multiplied m3 rotation matrix
    const mat3 m3 = mat3(
         0.643723,  1.081456, -1.386068,
        -1.696419,  0.630164, -0.295733,
         0.292627,  1.343103,  1.183983
    );

    mat2 rot(in float a) {
        float c = cos(a), s = sin(a);
        return mat2(c, s, -s, c);
    }
    float mag2(vec2 p)  { return dot(p, p); }
    float linstep(in float mn, in float mx, in float x) {
        return clamp((x - mn) / (mx - mn), 0.0, 1.0);
    }

    // Slow, large-scale displacement curve
    vec2 disp(float t) {
        return vec2(sin(t * 0.12), cos(t * 0.09)) * 2.0;
    }

    // cloudMap: 5 iterations for smooth seamless noise
    vec2 cloudMap(vec3 p) {
        vec3 p2 = p;
        p2.xy -= disp(p.z).xy;
        // Very slow rotation — key fix for jitter: use tiny coefficients
        p.xy  *= rot(sin(p.z * 0.3 + T * 0.05) * (0.1 + prm1 * 0.04) + T * 0.02);
        float cl = mag2(p2.xy);
        float dspAmp = 0.08 + prm1 * 0.15;
        float d = 0.0, z = 1.0, trk = 1.6;
        p *= 0.55;
        // 5 octaves — smooth noise, no blocky artifacts
        for (int i = 0; i < 5; i++) {
            // Fluid animation at consistent speed
            p += sin(p.zxy * 0.75 * trk + T * 0.30) * dspAmp;
            d -= abs(dot(cos(p), sin(p.yzx)) * z);
            z   *= 0.57;
            trk *= 1.3;
            p    = p * m3;
        }
        d = abs(d + prm1 * 3.0) + prm1 * 0.3 - 2.5;
        return vec2(d + cl * 0.2 + 0.25, cl);
    }

    // cloudRender: 32 steps, small minimum step for smooth density gradient
    vec4 cloudRender(in vec3 ro, in vec3 rd, float _T) {
        vec4 rez  = vec4(0.0);
        float t   = 1.5, fogT = 0.0;
        for (int i = 0; i < 48; i++) {
            if (rez.a > 0.99) break;
            vec4 col = vec4(0.0);
            vec3 pos = ro + t * rd;
            vec2 mpv = cloudMap(pos);
            float den = clamp(mpv.x - 0.3, 0.0, 1.0) * 1.12;
            float dn  = clamp(mpv.x + 2.0, 0.0, 3.0);
            if (mpv.x > 0.6) {
                col = vec4(
                    sin(vec3(5.0, 0.4, 0.2)
                        + mpv.y * 0.1
                        + sin(pos.z * 0.4) * 0.5
                        + 1.8) * 0.5 + 0.5,
                    0.08
                );
                col     *= den * den * den;
                col.rgb *= linstep(4.0, -2.5, mpv.x) * 2.3;
                float dif = 0.45;
                col.xyz *= den * (vec3(0.005, 0.045, 0.075)
                                + 1.5 * vec3(0.033, 0.07, 0.03) * dif);
            }
            float fogC = exp(t * 0.2 - 2.2);
            col.rgba  += vec4(0.06, 0.11, 0.11, 0.1) * clamp(fogC - fogT, 0.0, 1.0);
            fogT = fogC;
            rez  = rez + col * (1.0 - rez.a);
            // Smaller minimum step = smoother density transitions
            t   += clamp(0.5 - dn * dn * 0.05, 0.03, 0.22);
        }
        return clamp(rez, 0.0, 1.0);
    }

    varying vec2 v_uv;

    void main() {
        // T comes from C++ accumulator (speed = f(RMS)), not raw time
        T    = u_cloudT;

        // prm1 is purely time-driven — cloud motion is independent of audio
        prm1 = smoothstep(-0.4, 0.4, sin(u_time * 0.18));

        vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

        // Nudge center to the left to compensate for sidebar
        // 0.10 is roughly 5% shift, ideal for the sidebar footprint
        p.x += 0.10 * (u_resolution.x / u_resolution.y);

        // Camera path — very slow lateral drift, no fast sin sway
        vec3 ro = vec3(0.0, 0.0, T);
        ro.xy += disp(ro.z) * 0.7;

        float tgtDst = 3.5;
        vec3 target   = normalize(ro - vec3(disp(T + tgtDst) * 0.7, T + tgtDst));
        // Removed biased ro.x offset to keep vanishing point stable
        
        vec3 rightdir = normalize(cross(target, vec3(0.0, 1.0, 0.0)));
        vec3 updir    = normalize(cross(rightdir, target));
        rightdir      = normalize(cross(updir, target));
        vec3 rd       = normalize((p.x * rightdir + p.y * updir) - target);
        // Very slow roll
        rd.xy        *= rot(-disp(T + 3.5).x * 0.08 + 0.05);

        vec4 scn = cloudRender(ro, rd, T);
        vec3 col = scn.rgb;

        // Tint toward active theme color
        float luma = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(col, u_glowColor * luma * 2.2, 0.65);

        // Dark atmospheric background
        col *= 0.20;
        
        // --- Center Void Vignette ---
        // Make the center of the tunnel (p=0 after nudge) the darkest portion
        float radial = length(p);
        float vignette = smoothstep(0.0, 0.8, radial);
        // Deepen the center void, but keep some faint cloud detail
        col *= (0.3 + 0.7 * vignette);

        gl_FragColor = vec4(col, 1.0);
    }
)glsl";
