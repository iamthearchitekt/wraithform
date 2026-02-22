#pragma once

const static char *circularOscilloscopeFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform sampler2D u_audioData; 

    // Constants matching VisualizerShader.h (Aggressive)
    const float thickness = 0.008;
    const float gain = 6.0;
    const float lowpass = 0.15;
    const float glow = 0.5;
    const float glowSize = 2.0;
    const float window = 0.018;

    // Cyan base color
    const vec3 glowColor = vec3(0.835, 1.0, 1.0); 

    const float PI = 3.14159265359;
    const float TWO_PI = 6.28318530718;

    float sampleWave(float x) {
        return texture2D(u_audioData, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
    }

    varying vec2 v_uv;

    void main()
    {
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

        // Map angle to 0..1 for texture sampling
        // We want the circle to close, so 0 and 1 should meet.
        // atan returns -PI..PI. Map to 0..1
        float angleNorm = (a + PI) / TWO_PI;

        // --- Low frequency smoothing (same as linear) ---
        // Note: For a faster implementation, we could sample less, but let's stick to the high quality one.
        // Wrap around logic is needed for perfect smoothing at the seam, 
        // but clamp/wrap on texture might handle it if set to GL_REPEAT (we used CLAMP_TO_EDGE).
        // Since we used CLAMP_TO_EDGE, we might see a seam. 
        // For now, let's just clamp via the sampleWave function logic or rely on the visual continuity.
        // Ideally we'd wrap 'x'.
        
        float w0 = sampleWave(angleNorm);

        float dx = window;
        float wSm = 0.0;
        
        // Simple manual unroll as in original
        // Ideally we wrap the index for smoothing across the seam: fract(angleNorm + offset)
        // But sampleWave clamps. Let's try to wrap manually for smoothness.
        
        for (int i = -4; i <= 4; i++) {
             float xOff = angleNorm + float(i) * dx;
             // Wrap 0..1
             xOff = xOff - floor(xOff); 
             wSm += sampleWave(xOff);
        }
        wSm /= 9.0;

        float w = mix(w0, wSm, lowpass);

        // Amplitude -1..1
        float amp = (w - 0.5) * 2.0; 
        amp *= gain;

        // Base radius for the circle
        float baseRadius = 0.5;
        
        // Modulate radius by amplitude
        float targetRadius = baseRadius + amp * 0.15; // Scale amplitude effect on radius

        // --- Rendering ---

        float energy = clamp(abs(amp), 0.0, 1.0);
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
        intensity += innerGlow * (0.15 * g);
        intensity += outerGlow * (0.05 * g);

        intensity = clamp(intensity, 0.0, 1.0);

        intensity = clamp(intensity, 0.0, 1.0);
 
        // Background Ghostly Glow (Center Pulsing)
        // Calculate average energy
        float pulseEnergy = (sampleWave(0.1) + sampleWave(0.5) + sampleWave(0.9)) / 3.0;
        pulseEnergy = clamp(abs((pulseEnergy - 0.5) * 2.0), 0.0, 1.0);
        
        // Radial distance from center (r)
        // Soft, wide glow from center
        float bgGlow = 1.0 - smoothstep(0.0, 0.6 + 0.2 * pulseEnergy, r);
        // Make it subtle
        bgGlow *= 0.2 * (0.5 + 0.5 * pulseEnergy);
        
        vec3 finalColor = glowColor * intensity + glowColor * bgGlow;

        gl_FragColor = vec4(finalColor, 1.0);
    }
)glsl";

const static char *fireBallFragmentShader = R"glsl(
    #ifdef GL_ES
    precision mediump float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;

    // Hardcoded defaults for "GreatBallOfFire"
    const vec2 offset = vec2(0.0, 0.0);
    const float rotation = 0.0;
    const float size = 3.5;
    const float depth = 0.1;
    const float density = 2.5;
    const float rateX = -1.5; // Slightly slower than original
    const float rateY = -0.3;
    const float rateZ = 1.5;

    #define saturate(oo) clamp(oo, 0.0, 1.0)
    #define MarchSteps 12 // Increased for better detail
    #define Radius 0.8
    #define NoiseSteps 4
    
    // Ice Blue Theme (#D5FFFF)
    #define Color1 vec4(0.835, 1.0, 1.0, 1.0) // Primary Ice Blue
    #define Color2 vec4(0.5, 0.85, 0.9, 1.0)  // Mid Cyan
    #define Color3 vec4(0.2, 0.5, 0.6, 1.0)  // Slate Blue-Cyan
    #define Color4 vec4(0.01, 0.05, 0.1, 1.0) // Deep Dark Base

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

    vec4 Shade(float distance)
    {
        float c1 = saturate(distance*5.0 + 0.5);
        float c2 = saturate(distance*5.0);
        float c3 = saturate(distance*3.4 - 0.5);
        vec4 a = mix(Color1,Color2, c1);
        vec4 b = mix(a,     Color3, c2);
        return 	 mix(b,     Color4, c3);
    }

    float RenderScene(vec3 position, out float distance)
    {
        float noise = Turbulence(position * density + vec3(rateZ, rateX, rateY)*u_time, 0.1, 1.5, 0.03) * depth;
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

    varying vec2 v_uv;

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
        gl_FragColor = col;
    }
)glsl";
