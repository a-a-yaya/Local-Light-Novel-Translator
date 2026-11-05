#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 sampleRect;
    vec2 size;
    vec2 backdropSize;
    float cornerRadius;
    float obscurity;
    float darkMode;
};
layout(binding = 1) uniform sampler2D backdrop;

void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * size;
    vec2 q = abs(p) - size * 0.5 + cornerRadius;
    float d = length(max(q, vec2(0))) + min(max(q.x, q.y), 0.0) - cornerRadius;
    float mask = 1.0 - smoothstep(-0.7, 0.7, d);
    // 圆角内缘的法线控制采样偏移，形成玻璃边缘放大 / 折射感。
    vec2 n = normalize(max(q, vec2(0.001)) * sign(p));
    if (q.x < 0.0 || q.y < 0.0)
        n = q.x > q.y ? vec2(sign(p.x), 0) : vec2(0, sign(p.y));
    float edge = exp(-max(-d, 0.0) / 6.0);
    vec2 uv = sampleRect.xy + qt_TexCoord0 * sampleRect.zw;
    uv += n * edge * 3.5 / backdropSize;
    // 九点柔化；共享一张背景纹理，小组件不再各自建立整窗模糊缓冲。
    vec2 px = vec2(3.2) / backdropSize;
    vec4 bg = texture(backdrop, uv) * 0.28;
    bg += (texture(backdrop, uv + vec2(px.x, 0)) + texture(backdrop, uv - vec2(px.x, 0))
         + texture(backdrop, uv + vec2(0, px.y)) + texture(backdrop, uv - vec2(0, px.y))) * 0.12;
    bg += (texture(backdrop, uv + px) + texture(backdrop, uv - px)
         + texture(backdrop, uv + vec2(px.x, -px.y)) + texture(backdrop, uv + vec2(-px.x, px.y))) * 0.06;
    vec3 base = mix(vec3(0.98, 0.985, 0.99), vec3(0.07, 0.09, 0.12), darkMode);
    // 深色模式增加烟色吸收，避免浅色壁纸让白色文字失去对比。
    vec3 color = mix(bg.rgb, base, mix(mix(0.20, 0.68, darkMode), 1.0, obscurity));
    float rim = exp(-abs(d + 1.0) * 1.8);
    float light = 0.5 + 0.5 * dot(n, normalize(vec2(-0.55, -1.0)));
    color += rim * light * mix(0.25, 0.12, darkMode);
    color -= edge * (1.0 - light) * 0.035;
    fragColor = vec4(color * mask, mask) * qt_Opacity;
}
