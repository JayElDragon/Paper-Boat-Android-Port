@prism(type='fragment', name='Story Book Post Pass', version='2.0.0', description='Painted storybook look tuned from the Story Book GShade preset', author='PaperBoat Android')

@{GLSL_VERSION}

// The @prism header is split on commas and closes at the first ')': keep
// commas and parentheses out of its values.
//
// Look: smooth painted colour on fine embossed canvas with soft ink outlines.
// Version 1 sharpened and added pencil hatch, which amplified the N64's pixel
// grain into noise; this one smooths first and keeps the canvas fine.

@if(VERTEX_SHADER)
    @include("shaders/opengl/include/fast3d_vs.glsli")

@else
    @if(core_opengl || opengles)
    out vec4 vOutColor;
    #define SB_OUT vOutColor
    @else
    #define SB_OUT gl_FragColor
    @end

    @if(o_textures[0])
        @{attr} vec2 vTexCoord0;
        uniform sampler2D uTex0;
    @end
    // Must match fast3d_vs.glsli exactly (size and, on GLES, highp).
    @if(opengles)
    uniform highp vec4 uCustom[32];
    @else
    uniform vec4 uCustom[32];
    @end

    @setting(var='SMOOTH', name='Paint smoothing', default=1.0, min=0.0, max=1.0)
    @setting(var='EDGE', name='Ink outlines', default=0.45, min=0.0, max=1.0)
    @setting(var='EDGE_SLOPE', name='Outline slope', default=1.5, min=0.5, max=4.0)
    @setting(var='CANVAS', name='Canvas relief', default=0.5, min=0.0, max=1.5)
    @setting(var='CANVAS_SCALE', name='Canvas scale', default=1.0, min=0.5, max=3.0)
    @setting(var='PAPER', name='Paper tint', default=0.06, min=0.0, max=0.5)
    @setting(var='SATURATION', name='Saturation', default=0.4, min=-1.0, max=1.0)
    @setting(var='CONTRAST', name='Contrast', default=0.08, min=-0.5, max=0.5)
    @setting(var='EXPOSURE', name='Exposure', default=0.0, min=-1.0, max=1.0)

    const float kSmooth = float(@{SMOOTH});
    const float kEdge = float(@{EDGE});
    const float kEdgeSlope = float(@{EDGE_SLOPE});
    const float kCanvas = float(@{CANVAS});
    const float kCanvasScale = float(@{CANVAS_SCALE});
    const float kPaper = float(@{PAPER});
    const float kSaturation = float(@{SATURATION});
    const float kContrast = float(@{CONTRAST});
    const float kExposure = float(@{EXPOSURE});

    vec3 SbFetch(highp vec2 uv) {
        @if(o_textures[0])
            return @{texture}(uTex0, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
        @else
            return vec3(0.0);
        @end
    }

    float SbLuma(vec3 c) {
        return dot(c, vec3(0.299, 0.587, 0.114));
    }

    highp float SbHash(highp vec2 p) {
        p = fract(p * vec2(123.34, 456.21));
        p += dot(p, p + 45.32);
        return fract(p.x * p.y);
    }

    highp float SbNoise(highp vec2 p) {
        highp vec2 i = floor(p);
        highp vec2 f = fract(p);
        f = f * f * (3.0 - 2.0 * f);
        float a = SbHash(i);
        float b = SbHash(i + vec2(1.0, 0.0));
        float c = SbHash(i + vec2(0.0, 1.0));
        float d = SbHash(i + vec2(1.0, 1.0));
        return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    }

    // Canvas height: fine fibres stretched along two diagonals, like the
    // grain of painted cloth. Small scale so it reads as texture, not stripes.
    highp float SbCanvas(highp vec2 p) {
        highp vec2 d1 = vec2(p.x + p.y, p.x - p.y) * 0.7071;
        float fibreA = SbNoise(vec2(d1.x * 0.35, d1.y * 2.2));
        float fibreB = SbNoise(vec2(d1.x * 2.2, d1.y * 0.35) + 31.0);
        return fibreA * 0.4 + fibreB * 0.4 + SbNoise(p * 1.7 + 7.0) * 0.2;
    }

    // Bilateral weight: neighbours of similar colour blend, edges stay.
    float SbWeight(vec3 a, vec3 b) {
        vec3 d = a - b;
        return exp(-dot(d, d) * 40.0);
    }

    void main() {
        highp vec2 res = uCustom[1].xy;
        highp vec2 texel = uCustom[1].zw;
        highp vec2 uv = gl_FragCoord.xy * texel;

        // Sized for a 480-line image so the look holds at any resolution.
        float unit = max(res.y / 480.0, 1.0);
        highp vec2 o = texel * unit;

        vec3 c  = SbFetch(uv);
        vec3 n  = SbFetch(uv + vec2(0.0, o.y));
        vec3 s  = SbFetch(uv - vec2(0.0, o.y));
        vec3 e  = SbFetch(uv + vec2(o.x, 0.0));
        vec3 w  = SbFetch(uv - vec2(o.x, 0.0));
        vec3 ne = SbFetch(uv + o);
        vec3 sw = SbFetch(uv - o);
        vec3 nw = SbFetch(uv + vec2(-o.x, o.y));
        vec3 se = SbFetch(uv + vec2(o.x, -o.y));
        highp vec2 o2 = o * 2.0;
        vec3 n2 = SbFetch(uv + vec2(0.0, o2.y));
        vec3 s2 = SbFetch(uv - vec2(0.0, o2.y));
        vec3 e2 = SbFetch(uv + vec2(o2.x, 0.0));
        vec3 w2 = SbFetch(uv - vec2(o2.x, 0.0));

        // Paint smoothing.
        vec3 sum = c;
        float total = 1.0;
        float wt;
        wt = SbWeight(c, n);  sum += n * wt;  total += wt;
        wt = SbWeight(c, s);  sum += s * wt;  total += wt;
        wt = SbWeight(c, e);  sum += e * wt;  total += wt;
        wt = SbWeight(c, w);  sum += w * wt;  total += wt;
        wt = SbWeight(c, ne) * 0.7; sum += ne * wt; total += wt;
        wt = SbWeight(c, sw) * 0.7; sum += sw * wt; total += wt;
        wt = SbWeight(c, nw) * 0.7; sum += nw * wt; total += wt;
        wt = SbWeight(c, se) * 0.7; sum += se * wt; total += wt;
        wt = SbWeight(c, n2) * 0.5; sum += n2 * wt; total += wt;
        wt = SbWeight(c, s2) * 0.5; sum += s2 * wt; total += wt;
        wt = SbWeight(c, e2) * 0.5; sum += e2 * wt; total += wt;
        wt = SbWeight(c, w2) * 0.5; sum += w2 * wt; total += wt;
        vec3 color = mix(c, sum / total, kSmooth);

        // Ink outlines on strong edges only, so flat areas stay clean.
        float lne = SbLuma(ne), lsw = SbLuma(sw), lnw = SbLuma(nw), lse = SbLuma(se);
        float ln = SbLuma(n), ls = SbLuma(s), le = SbLuma(e), lw = SbLuma(w);
        float gx = (lne + 2.0 * le + lse) - (lnw + 2.0 * lw + lsw);
        float gy = (lnw + 2.0 * ln + lne) - (lsw + 2.0 * ls + lse);
        float edge = smoothstep(0.25, 0.25 + 1.0 / kEdgeSlope, length(vec2(gx, gy)));
        color *= 1.0 - edge * kEdge;

        // Canvas relief lit from the top left.
        highp vec2 cp = gl_FragCoord.xy / (unit * 0.9 * kCanvasScale);
        float h  = SbCanvas(cp);
        float hx = SbCanvas(cp + vec2(0.5, 0.0)) - h;
        float hy = SbCanvas(cp + vec2(0.0, 0.5)) - h;
        float relief = (hy - hx) * 0.7071;
        color *= 1.0 + relief * kCanvas * (1.0 - edge);

        // Warm paper and colour grade.
        color = mix(color, color * vec3(1.0, 0.97, 0.9) + vec3(0.03, 0.025, 0.01), kPaper);
        color = max(color, vec3(0.0)) * exp2(kExposure);
        color = (color - 0.5) * (1.0 + kContrast) + 0.5;
        float l = SbLuma(color);
        color = mix(vec3(l), color, 1.0 + kSaturation * 0.5);

        // Ordered dither to hide banding from the grade.
        highp vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
        float bayer = mod(cell.x * 2.0 + cell.y * 3.0, 4.0) / 4.0 - 0.375;
        color += vec3(bayer / 255.0);

        SB_OUT = vec4(clamp(color, 0.0, 1.0), 1.0);
    }
@end
