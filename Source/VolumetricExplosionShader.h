#pragma once

/**
    Volumetric Explosion Shader
    Ported from ISF for WraithForm
    Original by Duke, modified for theme-reactive colors.
*/

const static char *volumetricExplosionFragmentShader = R"glsl(
    #ifdef GL_ES
    precision highp float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform vec2 u_mouse;
    uniform float u_audioEnergy; // Bass energy
    uniform float u_kickEnergy;  // Transients
    uniform vec3 u_glowColor;

    varying vec2 v_uv;

    // --- True Volumetric Ethereal Fire ---
    
    #define pi 3.14159265
    
    mat2 rot(float a) {
        float c = cos(a), s = sin(a);
        return mat2(c, s, -s, c);
    }
    
    // High-quality, continuous 3D noise (Simplex-ish) to avoid checkerboarding
    vec3 hash33(vec3 p) { 
        p = fract(p * vec3(443.8975,397.2973, 491.1871));
        p += dot(p.zxy, p.yxz+19.27);
        return fract(vec3(p.x * p.y, p.z*p.x, p.y*p.z)) - 0.5;
    }
    
    float smoothNoise(vec3 p) {
        vec3 i = floor(p);
        vec3 f = fract(p);
        vec3 u = f*f*(3.0-2.0*f);
        
        return mix(mix(mix(dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0)), 
                           dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                       mix(dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0)), 
                           dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
                   mix(mix(dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1)), 
                           dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                       mix(dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1)), 
                           dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z) + 0.5;
    }

    // Fractal Brownian Motion for ghostly, organic fire wisps
    float fbm(vec3 p) {
        float f = 0.0;
        float amp = 0.5;
        // 5 octaves of high quality noise for smooth fractal detail
        for (int i = 0; i < 5; i++) {
            f += amp * smoothNoise(p);
            p *= 2.0;
            // PERFECTLY SMOOTH ANIMATION:
            // Only time drives the spatial flow. No audio energy is added
            // here, to prevent the coordinates from "jumping" during transients.
            p.y -= u_time * 0.8; 
            amp *= 0.5;
        }
        return f;
    }

    float map(vec3 p) {
        // Mouse rotation
        p.xz *= rot(u_mouse.x * pi + u_time * 0.05);
        p.yz *= rot(u_mouse.y * pi * 0.5);
        
        // Base distance to center (sphere)
        float d = length(p);
        
        // Vastly increased base size for a large screen-filling cloud.
        // It's muted/dark, but takes up space.
        float radius = 1.8 + u_audioEnergy * 0.4 + u_kickEnergy * 0.8;
        
        // Safe, smooth coordinate morphing (NO u_kickEnergy in coordinates!)
        vec3 q = p * vec3(1.4, 0.9, 1.4) - vec3(0.0, u_time * 1.0, 0.0);
        
        // Audio reaction is ONLY applied to the amplitude (volume) of the noise,
        // which pushes the cloud outwards smoothly rather than teleporting its texture.
        float noiseDisp = fbm(q) * (1.6 + u_audioEnergy * 1.5 + u_kickEnergy * 1.0);
        
        // Density is highest at center, distorted by noise
        return d - radius + noiseDisp;
    }

    bool raySphereInter(vec3 ro, vec3 rd, float r, out float tnear, out float tfar) {
        float b = dot(ro, rd);
        float c = dot(ro, ro) - r*r;
        float h = b*b - c;
        if(h < 0.0) return false;
        h = sqrt(h);
        tnear = -b - h;
        tfar = -b + h;
        return true;
    }

    void main() {
        vec2 uv = (gl_FragCoord.xy / u_resolution.xy) - 0.5;
        uv.x *= u_resolution.x / u_resolution.y;
        
        // Camera close to make it feel massive
        vec3 ro = vec3(0.0, 0.0, -2.2);
        vec3 rd = normalize(vec3(uv, 1.0));
        
        vec3 finalColor = vec3(0.0);
        
        float tnear, tfar;
        // Massive bounding sphere
        if (raySphereInter(ro, rd, 4.5, tnear, tfar)) {
            
            float t = max(tnear, 0.0);
            float stepSize = 0.06; // Keep performance reasonable for large vol
            float densityAcc = 0.0;
            
            for (int i = 0; i < 110; i++) { 
                if (t > tfar || densityAcc > 0.99) break;
                
                vec3 p = ro + rd * t;
                float d = map(p);
                
                // Soft density curve. 
                float localDensity = smoothstep(0.1, -0.4, d);
                
                if (localDensity > 0.01) {
                    // Accumulate alpha with higher contrast (pow 1.3) for sharper interior wisps
                    float alpha = pow(localDensity, 1.3) * 0.1;
                    densityAcc += alpha * (1.0 - densityAcc);
                    
                    // Depth brightness
                    float depth = length(p);
                    
                    // Higher contrast brightness curve for more visual pop
                    float brightness = smoothstep(3.8, 0.0, depth);
                    brightness = pow(brightness, 1.5) * (0.6 + u_kickEnergy * 0.5);
                    
                    // Dual-tone Depth Lighting:
                    // 1. Subtle Cyan Rim-Light on the thin outer wisps
                    vec3 cyan = vec3(0.05, 0.7, 1.0);
                    float edgeFactor = smoothstep(0.6, 0.0, localDensity) * 0.45; // Up to 45% cyan on extreme ghostly edges
                    vec3 emitCol = mix(u_glowColor, cyan, edgeFactor);
                    
                    // 2. Subtle Core Pop (slight white additive at the absolute densest, nearest center)
                    float corePop = smoothstep(0.8, 1.0, localDensity) * smoothstep(1.0, 0.0, depth) * 0.25;
                    emitCol += vec3(corePop);
                    
                    // Strictly tint with the new dynamic emitCol. Lower base multiplier.
                    vec3 col = emitCol * brightness * 1.3;
                    
                    // Add to final color, weighted by accumulated transparency
                    finalColor += col * alpha * (1.0 - densityAcc);
                }
                t += stepSize;
            }
        }
        
        // Pure theme-colored vignette mask (wide)
        float vignette = smoothstep(2.0, 0.0, length(uv));
        finalColor *= vignette;
        
        // Tone mapping to keep saturation but prevent channel clipping
        finalColor = 1.0 - exp(-finalColor * 2.5);

        gl_FragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
    }
)glsl";
