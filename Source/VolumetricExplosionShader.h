#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

const char* volumetricExplosionFragmentShader = R"glsl(
    #version 130
    #ifdef GL_ES
    precision highp float;
    #endif

    uniform float u_time;
    uniform vec2 u_resolution;
    uniform float u_audioEnergy; // Bass energy
    uniform float u_kickEnergy;  // Transients
    uniform float u_cloudT;      // Integrated audio velocity
    uniform vec3 u_glowColor;
    uniform vec2 u_centerOffset;

    in vec2 v_uv;
    out vec4 fragColor;

    // --- True Volumetric Ethereal Fire ---
    // Uses 3D smooth noise instead of basic 2D value noise for completely fluid,
    // organic 3D structure that doesn't look pixelated or grid-like.
    
    vec3 hash33(vec3 p) {
        p = vec3(dot(p,vec3(127.1,311.7, 74.7)),
                 dot(p,vec3(269.5,183.3,246.1)),
                 dot(p,vec3(113.5,271.9,124.6)));
        return -1.0 + 2.0*fract(sin(p)*43758.5453123);
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
        // 4 octaves is enough detail and vastly improves framerate
        for (int i = 0; i < 4; i++) {
            f += amp * smoothNoise(p);
            p *= 2.0;
            // PERFECTLY SMOOTH ANIMATION:
            // Only time drives the spatial flow. No audio energy is added
            // here, to prevent the coordinates from "jumping" during transients.
            p.y -= u_cloudT * 0.8; 
            amp *= 0.5;
        }
        return f;
    }

    mat2 rot(float a) {
        float c = cos(a), s = sin(a);
        return mat2(c, -s, s, c);
    }

    float map(vec3 p) {
        // Slow ethereal rotation driven by audio velocity
        p.xz *= rot(u_cloudT * 0.05);
        p.yz *= rot(u_cloudT * 0.02);
        
        vec3 centeredP = p - vec3(u_centerOffset.x * 2.0, u_centerOffset.y * 2.0, 0.0);
        float d = length(centeredP);
        
        float dynKick = pow(u_kickEnergy, 1.2) * 1.8;
        float dynBass = u_audioEnergy * 1.0;

        // Majestic base size, expanding smoothly on transients
        float radius = 0.95 + dynBass * 0.5 + dynKick * 0.35;
        
        // Morph coordinates smoothly
        vec3 q = centeredP * vec3(1.2, 0.8, 1.2) - vec3(0.0, u_cloudT * 1.2 + dynKick * 0.5, 0.0);
        
        // Soft, sweeping noise displacement
        float rawNoise = fbm(q) * 2.0 - 1.15; 
        
        // Organic volume expansion with defined wisps
        float noiseDisp = rawNoise * (1.6 + dynBass * 1.3 + dynKick * 2.4);
        
        return d - radius - noiseDisp;
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
        vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
        
        vec3 ro = vec3(0.0, 0.0, -2.8);
        vec3 rd = normalize(vec3(uv, 1.0));
        
        vec3 finalColor = vec3(0.0);
        
        float tnear, tfar;
        // Bounding sphere
        if (raySphereInter(ro, rd, 8.0, tnear, tfar)) { 
            
            float t = max(tnear, 0.0);
            float stepSize = 0.18;
            float densityAcc = 0.0;
            
            // Dither the starting position to eliminate banding
            float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
            t += dither * stepSize;
            
            float dynKick = pow(u_kickEnergy, 1.2) * 1.8;
            float dynBass = u_audioEnergy * 1.0;
            
            for (int i = 0; i < 45; i++) { 
                if (t > tfar || densityAcc > 0.99) break;
                
                vec3 p = ro + rd * t;
                float d = map(p);
                
                // Gently tightened density curve: trims the puffy marshmallow rim while keeping the full organic cloud body
                float localDensity = smoothstep(0.25, -0.45, d);
                
                if (localDensity > 0.01) {
                    float alpha = localDensity * 0.07;
                    densityAcc += alpha * (1.0 - densityAcc);
                    
                    float depth = length(p);
                    float depthFactor = smoothstep(4.0, 0.0, depth);
                    
                    // Controlled theme lighting without harsh white burnout
                    vec3 darkOuter = u_glowColor * 0.12; 
                    vec3 midCloud  = u_glowColor * 0.75;  
                    vec3 hotCore   = mix(u_glowColor * 1.8, vec3(1.0), 0.12); // Luminous without blowing out or desaturating
                    
                    vec3 emitCol = mix(darkOuter, midCloud, smoothstep(0.0, 0.5, depthFactor));
                    emitCol = mix(emitCol, hotCore, smoothstep(0.7, 1.0, depthFactor));
                    
                    // Explosive flash on kick drum
                    vec3 flashCol = u_glowColor * dynKick * 0.85;
                    emitCol += flashCol * depthFactor;
                    
                    // Tastefully dialed brightness: bright and energetic, but comfortable on the eyes
                    float brightness = 0.42 + dynBass * 0.5 + dynKick * 0.35;
                    vec3 col = emitCol * brightness;
                    
                    finalColor += col * alpha * (1.0 - densityAcc);
                }
                t += stepSize;
            }
        }
        
        // Smooth vignette
        float vignette = smoothstep(2.0, 0.0, length(uv));
        finalColor *= vignette;
        
        // Soft exponential tone mapping: avoids clipping
        finalColor = 1.0 - exp(-finalColor * 1.6);
        
        // Ethereal global fade-out when completely silent
        float opacityPulse = clamp(0.1 + u_audioEnergy * 2.0, 0.0, 1.0);
        finalColor *= opacityPulse;
        
        fragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
    }
)glsl";
