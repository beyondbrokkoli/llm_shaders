#version 460
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vGlow;
layout(location = 0) out vec4 fragColor;

void main() {
    // gl_PointCoord goes from 0.0 to 1.0 across the point
    vec2 uv = gl_PointCoord - vec2(0.5);
    float dist = length(uv);

    // Soft discard to keep the edges clean
    if (dist > 0.5) discard;

    // Exponential Glow: 
    // Core is at dist=0, Edge is at dist=0.5
    // We want the intensity to drop off fast at first, then linger
    float intensity = exp(-dist * 6.0); 
    
    // Add a secondary "Halo" for that majestic feel
    float halo = pow(max(0.0, 1.0 - dist * 2.0), 4.0) * 0.4;

    vec3 finalRGB = vColor * (intensity + halo);
    
    // Additive-style alpha: brighter centers are more opaque
    float alpha = (intensity + halo) * vGlow;

    fragColor = vec4(finalRGB * 2.0, alpha); // Multiply by 2 for HDR-ish pop
}
