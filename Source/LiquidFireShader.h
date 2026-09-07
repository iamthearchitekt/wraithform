#pragma once

const static char *liquidFireFragmentShader = R"glsl(
    #version 130
    // LiquidFire by mojovideotech
    // based on :
    // glslsandbox.com/e#29962.1
    // by @301z

    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform vec3 u_glowColor;
    uniform float u_audioEnergy;
    uniform vec2 u_centerOffset;
    
    in vec2 v_uv;
    out vec4 fragColor;

    float rnd(vec2 n) { 
        return fract(cos(dot(n, vec2(5.14229, 433.494437))) * 2971.215073);
    }

    float noise(vec2 n) {
        const vec2 d = vec2(0.0, 1.0);
        vec2 b = floor(n), f = smoothstep(vec2(0.0), vec2(1.0), fract(n));
        return mix(mix(rnd(b), rnd(b + d.yx), f.x), mix(rnd(b + d.xy), rnd(b + d.yy), f.x), f.y);
    }

    float fbm(vec2 n) {
        float total = 0.0, amplitude = 1.0;
        for (int i = 0; i < 6; i++) {
            total += noise(n) * amplitude;
            n += n;
            amplitude *= 0.6;
        }
        return total;
    }

    void main() {
        vec3 uv_z = vec3(u_resolution.x, u_resolution.y, 100.0);
        vec2 center = 0.5 * u_resolution.xy + u_centerOffset * u_resolution.xy;
        vec2 adjustedCoord = gl_FragCoord.xy - center + (0.5 * u_resolution.xy);
        vec2 p = adjustedCoord * 8.0 / uv_z.xx;
        float rate = 2.0;
        float T = u_time * rate;
        
        // Match the user's color scheme purely using u_glowColor
        vec3 darkShade = u_glowColor * 0.05;
        vec3 midShade  = u_glowColor * 0.35;
        vec3 hotAccent = mix(u_glowColor * 1.3, vec3(1.0), 0.25);

        vec3 c1 = darkShade;
        vec3 c2 = midShade;
        vec3 c3 = darkShade * 0.5;
        vec3 c4 = mix(midShade, hotAccent, u_audioEnergy * 0.75);

        const vec3 c5 = vec3(0.03);
        const vec3 c6 = vec3(0.25);

        float q = fbm(p - T * 0.25); 
        vec2 r = vec2(fbm(p + q + log2(T * 0.618) - p.x - p.y), fbm(p + q - abs(log2(T * 3.142))));

        vec2 offset = vec2(0.0, -1.0); // Flame pushes from the bottom up mostly
        vec3 c = mix(c1, c2, fbm(p + r - offset.x)) + mix(c3, c4, r.x) - mix(c5, c6, r.y);

        // Add rippling theme accents based on audio
        float ripple = sin(length(p - vec2(0.0, 4.0)) * 8.0 - u_time * 6.0) * 0.5 + 0.5;
        c += hotAccent * ripple * u_audioEnergy * 0.35 * fbm(p * 3.0);

        // Alpha blend it slightly so it can sit gracefully in the back
        float alpha = clamp(length(c) * 0.5, 0.0, 1.0);
        
        // Balanced ambient liquid fluid background
        fragColor = vec4(c * 0.22, alpha * 0.10);
    }
)glsl";
