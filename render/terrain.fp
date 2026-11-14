#version 140
in vec4 var_color;
in vec3 var_position;
out vec4 out_fragColor;

float hash(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float noise(vec2 p) {
    vec2 cell = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1.0, 0.0)), f.x),
               mix(hash(cell + vec2(0.0, 1.0)), hash(cell + 1.0), f.x), f.y);
}

void main() {
    vec3 color = var_color.rgb;
    vec2 p = var_position.xz;
    float footprint = max(fwidth(p.x), fwidth(p.y));
    float fine = 1.0 - smoothstep(0.25, 1.5, footprint);
    float grain = mix(0.5, hash(floor(p * 5.0)), fine);
    color *= 0.83 + noise(p * 0.16) * 0.23 + grain * 0.12;
    if (var_position.y < -0.9) {
        float grass = smoothstep(0.015, 0.12, var_color.g - var_color.r);
        vec2 cell = floor(p * 0.55);
        vec2 local = fract(p * 0.55) - vec2(0.15 + hash(cell) * 0.7, 0.15 + hash(cell + 17.0) * 0.7);
        float blade = (1.0 - smoothstep(0.015, 0.09, abs(local.x + local.y * 0.32)))
                    * (1.0 - smoothstep(0.12, 0.4, abs(local.y)));
        color += vec3(0.055, 0.095, 0.025) * blade * grass * fine;
        float pebble = (1.0 - smoothstep(0.06, 0.14, length(local))) * step(0.72, hash(cell + 5.0));
        color -= vec3(0.12, 0.10, 0.07) * pebble * (1.0 - grass) * fine;
    }
    out_fragColor = vec4(color, var_color.a);
}
