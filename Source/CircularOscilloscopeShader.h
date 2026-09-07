#pragma once

const static char *circularOscilloscopeFragmentShader = R"glsl(
    #version 130
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform sampler2D u_audioData; 

    // Constants
    const float thickness = 0.008;
    const float gain = 6.0;
    const float lowpass = 0.55;  // Higher = more low-pass (bass focus)
    const float glow = 0.5;
    const float glowSize = 2.0;
    const float window = 0.025; // Wider window = smoother = more low-freq look

    // Theme color and bass energy driven by C++ uniforms
    uniform vec3 u_glowColor;
    uniform float u_bassEnergy; // 0..1 smooth bass level (bins 1-11)

    const float PI = 3.14159265359;
    const float TWO_PI = 6.28318530718;

    float diffSlope = 0.0;

    float sampleWave(float x) {
        float fx = fract(x);
        float val = texture(u_audioData, vec2(fx, 0.5)).r;
        return val + (diffSlope * fx);
    }

    in vec2 v_uv;
    out vec4 fragColor;

    void main()
    {
        // Compute tilt slope once per fragment
        float startVal = texture(u_audioData, vec2(0.0, 0.5)).r;
        float endVal   = texture(u_audioData, vec2(0.999, 0.5)).r;
        diffSlope = startVal - endVal;

        vec2 uv = v_uv;
        
        // Remap UV to centered coordinates -1..1
        vec2 p = uv * 2.0 - 1.0;
        
        // Correct aspect ratio so the circle is perfect
        if (u_resolution.x > u_resolution.y) {
            p.x *= u_resolution.x / u_resolution.y;
        } else {
            p.y *= u_resolution.y / u_resolution.x;
        }

        // Polar coordinates
        float r = length(p);
        float a = atan(p.y, p.x); // -PI to PI
        
        // Rotate the anchor point continuously using u_time so the seam becomes a dynamic part of the animation
        a -= (PI / 2.0) - (u_time * 2.25);

        // Map angle to 0..1 for texture sampling
        // We revert back to a standard continuous wrap to preserve the concept of a single continuous circular wave
        float angleNorm = fract((a + PI) / TWO_PI);

        // Read raw audio wave
        float w0 = sampleWave(angleNorm);

        // Calculate a heavily smoothed version of the wave to find the "lopsided" low frequency bulge
        float dx = window * 2.0; // Use a wider window to deeply capture the macro shape
        float wSm = sampleWave(angleNorm - 4.0*dx) + sampleWave(angleNorm - 3.0*dx) +
                    sampleWave(angleNorm - 2.0*dx) + sampleWave(angleNorm - 1.0*dx) +
                    sampleWave(angleNorm) + sampleWave(angleNorm + 1.0*dx) +
                    sampleWave(angleNorm + 2.0*dx) + sampleWave(angleNorm + 3.0*dx) +
                    sampleWave(angleNorm + 4.0*dx);
        wSm /= 9.0;

        // HIGH-PASS FILTER: Subtract the slow, lopsided bulge from the raw wave.
        // This leaves beautifully balanced high-frequency ripples that center perfectly on the circle line!
        float highPass = w0 - wSm;

        // Calculate the amplitude of the ripples
        float amp = highPass * gain * 0.40; // Scaled up slightly to compensate for removed low-end

        // UNIFORM AUDIO PRESSURE (Normalized Sub-Bass)
        // Instead of the sub-frequencies bulging one side, they expand the entire circle equally!
        // This keeps the circle perfectly round while still reacting massively to the beat.
        float uniformBassExpansion = u_bassEnergy * 0.15;

        // Base radius for the circle starts a bit smaller so it has room to expand outward
        float baseRadius = 0.40 + uniformBassExpansion;
        
        // On a bass hit: ring breathes outward more dramatically
        float radiusScale = 0.18 + 0.12 * u_bassEnergy;
        float targetRadius = baseRadius + amp * radiusScale;

        // --- Rendering ---

        float energy = abs(amp);
        float thick = thickness * (1.0 + energy * 1.5);

        // Distance field to the target radius ring
        float d = abs(r - targetRadius);

        // Core line
        float line = 1.0 - smoothstep(0.0, thick, d);

        // Glow layers
        float innerGlow = 1.0 - smoothstep(0.0, thick * (1.8 * glowSize), d);
        float outerGlow = 1.0 - smoothstep(0.0, thick * (3.2 * glowSize), d);

        float g = glow * (0.6 + 0.8 * energy);

        float intensity = line;
        intensity += innerGlow * (0.40 * g);
        intensity += outerGlow * (0.18 * g);

        intensity = clamp(intensity, 0.0, 1.0);

        intensity = clamp(intensity, 0.0, 1.0);
 
        // Background Ghostly Glow (Center Pulsing)
        // Calculate average energy
        float pulseEnergy = (sampleWave(0.1) + sampleWave(0.5) + sampleWave(0.9)) / 3.0;
        pulseEnergy = clamp(abs((pulseEnergy - 0.5) * 2.0), 0.0, 1.0);
        
        // Background glow: pulses harder with bass energy
        float bgGlow = 1.0 - smoothstep(0.0, 0.55 + 0.25 * u_bassEnergy, r);
        bgGlow *= 0.40 * (0.5 + 0.5 * u_bassEnergy);
        
        vec3 finalColor = u_glowColor * intensity + u_glowColor * bgGlow;

        fragColor = vec4(finalColor, 1.0);
    }
)glsl";

const static char *fireBallFragmentShader = R"glsl(
    #version 130
    #ifdef GL_ES
    precision highp float;
    #else
    #define highp
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform float u_audioEnergy; // 0..1, driven by RMS from C++
    uniform vec3  u_glowColor;   // Active theme color from C++

    // Hardcoded motion parameters for "GreatBallOfFire"
    const vec2 offset = vec2(0.0, 0.0);
    const float rotation = 0.0;
    const float size = 3.5;
    const float depth = 0.1;
    const float rateX = -1.5;
    const float rateY = -0.3;
    const float rateZ = 1.5;

    #define saturate(oo) clamp(oo, 0.0, 1.0)
    #define MarchSteps 30
    #define Radius 0.8
    #define NoiseSteps 6

    vec3 mod196(vec3 x) { return x - floor(x * (1.0 / 196.0)) * 196.0; }
    vec4 mod196(vec4 x) { return x - floor(x * (1.0 / 196.0)) * 196.0; }
    vec4 permute(vec4 x) { return mod196(((x*56.0)+1.0)*x); }
    vec4 taylorInvSqrt(vec4 r){ return 1.79284291400159 - 0.85373472095314 * r; }

    float snoise(vec3 v)
    {
        const vec2  C = vec2(1.0/6.0, 1.0/3.0);
        const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
        vec3 i  = floor(v + dot(v, C.yyy));
        vec3 x0 = v - i + dot(i, C.xxx);
        vec3 g = step(x0.yzx, x0.xyz);
        vec3 l = 1.0 - g;
        vec3 i1 = min(g.xyz, l.zxy);
        vec3 i2 = max(g.xyz, l.zxy);
        vec3 x1 = x0 - i1 + C.xxx;
        vec3 x2 = x0 - i2 + C.yyy; 
        vec3 x3 = x0 - D.yyy;      
        i = mod196(i);
        vec4 p = permute( permute( permute( i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y + vec4(0.0, i1.y, i2.y, 1.0 )) + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
        float n_ = 0.142857142857;
        vec3  ns = n_ * D.wyz - D.xzx;
        vec4 j = p - 49.0 * floor(p * ns.z * ns.z);  
        vec4 x_ = floor(j * ns.z);
        vec4 y_ = floor(j - 7.0 * x_);    
        vec4 x = x_ *ns.x + ns.yyyy;
        vec4 y = y_ *ns.x + ns.yyyy;
        vec4 h = 1.0 - abs(x) - abs(y);
        vec4 b0 = vec4(x.xy, y.xy);
        vec4 b1 = vec4(x.zw, y.zw);
        vec4 s0 = floor(b0) * 2.0 + 1.0;
        vec4 s1 = floor(b1) * 2.0 + 1.0;
        vec4 sh = -step(h, vec4(0.0));
        vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
        vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
        vec3 p0 = vec3(a0.xy, h.x);
        vec3 p1 = vec3(a0.zw, h.y);
        vec3 p2 = vec3(a1.xy, h.z);
        vec3 p3 = vec3(a1.zw, h.w);
        vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
        p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
        vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
        m = m * m;
        return 35.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
    }

    float Turbulence(vec3 position, float minFreq, float maxFreq, float qWidth)
    {
        float value = 0.0;
        float cutoff = clamp(0.5/qWidth, 0.0, maxFreq);
        float fade;
        float fOut = minFreq;
        for(int i=NoiseSteps ; i>=0 ; i--)
        {
            if(fOut >= 0.5 * cutoff) break;
            fOut *= 2.0;
            value += abs(snoise(position * fOut))/fOut;
        }
        fade = clamp(2.0 * (cutoff-fOut)/cutoff, 0.0, 1.0);
        value += fade * abs(snoise(position * fOut))/fOut;
        return 1.0-value;
    }

    float SphereDist(vec3 position) { return length(position) - Radius; }

    // Theme-driven fireball palette derived from u_glowColor uniform
    vec4 Shade(float distance)
    {
        float c1 = saturate(distance * 5.0 + 0.5);
        float c2 = saturate(distance * 5.0);
        float c3 = saturate(distance * 3.4 - 0.5);
        // Hot core: white-hot blow-out
        vec4 Color1 = vec4(mix(vec3(1.3), u_glowColor, 0.15), 1.0);
        // Mid layer: vivid theme color, bright
        vec4 Color2 = vec4(u_glowColor * 1.6, 1.0);
        // Transition: mid-dim theme
        vec4 Color3 = vec4(u_glowColor * 0.45, 1.0);
        // Outer darkness: near-black (hint of color)
        vec4 Color4 = vec4(u_glowColor * 0.04, 1.0);
        vec4 a = mix(Color1, Color2, c1);
        vec4 b = mix(a,      Color3, c2);
        return       mix(b,  Color4, c3);
    }

    float RenderScene(vec3 position, out float distance)
    {
        // Density: quiet=1.5 (small, calm), bass-hit=5.0 (large, turbulent) -> Decreased to 1.5 for extra calmness
        float liveDensity = 1.5 + u_audioEnergy * 1.5;
        // Depth: quiet=0.04 (smooth ball), bass-hit=0.28 (spiky corona) -> Decreased to 0.08 for extremely smooth edges
        float liveDepth   = 0.025 + u_audioEnergy * 0.08;
        
        // Return to standard u_time (which is accumulated fluidly in C++)
        float noise = Turbulence(position * liveDensity + vec3(rateZ, rateX, rateY) * u_time, 0.1, 1.5, 0.03) * liveDepth;
        noise = saturate(abs(noise));
        distance = SphereDist(position) - noise;
        return noise;
    }

    vec4 March(vec3 rayOrigin, vec3 rayStep)
    {
        vec3 position = rayOrigin;
        float distance;
        float displacement;
        for(int step = 0; step < MarchSteps; ++step)
        {
            displacement = RenderScene(position, distance);
            if(distance < 0.05) break;
            position += rayStep * distance;
        }
        return mix(Shade(displacement), vec4(0.0, 0.0, 0.0, 0.0), float(distance >= 0.5));
    }

    bool IntersectSphere(vec3 ro, vec3 rd, vec3 pos, float radius, out vec3 intersectPoint)
    {
        vec3 relDistance = (ro - pos);
        float b = dot(relDistance, rd);
        float c = dot(relDistance, relDistance) - radius*radius;
        float d = b*b - c;
        if (d < 0.0) return false;
        intersectPoint = ro + rd*(-b - sqrt(d));
        return true;
    }

    in vec2 v_uv;
    out vec4 fragColor;

    void main(void)
    {
        // Use UV coordinates (0..1) directly
        // Map to -1..1
        vec2 p = v_uv * 2.0 - 1.0;

        // Correct aspect ratio using u_resolution
        if (u_resolution.x > u_resolution.y) {
            p.x *= u_resolution.x / u_resolution.y;
        } else {
            p.y *= u_resolution.y / u_resolution.x;
        }

        float rotx = rotation* 4.0;
        float roty = -rotation * 4.0;
        float zoom = 16.0-(size*3.);
        vec3 ro = zoom * normalize(vec3(cos(roty), cos(rotx), sin(roty)));
        vec3 ww = normalize(vec3(0.0, 0.0, 0.0) - ro);
        vec3 uu = normalize(cross( vec3(0.0, 1.0, 0.0), ww));
        vec3 vv = normalize(cross(ww, uu));
        vec3 rd = normalize(p.x*uu + p.y*vv + 1.5*ww);
        vec4 col = vec4(0.0);
        vec3 origin;
        if(IntersectSphere(ro, rd, vec3(0.0), Radius + depth*7.0, origin))
        {
            col = March(origin, rd);
        }
        fragColor = col;
    }
)glsl";
