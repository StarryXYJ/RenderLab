struct EnvironmentFrame
{
    float nx, ny, nz, tx, ty, tz, bx, by, bz;
};
struct EnvironmentDirection
{
    float x, y, z;
};

static inline float Fetch(uniform const uint8 rgba[], uniform const float linear[],
                          uniform int width, uniform int height, int x, int y, uniform int channel)
{
    // atan2 maps u to [0,1], so bilinear taps are only in [-1,width].
    x = x < 0 ? x + width : x;
    x = x >= width ? x - width : x;
    y = min(max(y, 0), height - 1);
    return linear[rgba[(y * width + x) * 4 + channel]];
}

static inline float Sample(uniform const uint8 rgba[], uniform const float linear[],
                           uniform int width, uniform int height, float lx, float ly, float lz,
                           uniform int channel)
{
    uniform const float pi = 3.14159265358979323846f;
    float u = atan2(lz, lx) / (2 * pi) + .5f;
    float v = acos(min(max(ly, -1.f), 1.f)) / pi;
    float x = u * width - .5f, y = v * height - .5f;
    int ix = (int)floor(x), iy = (int)floor(y);
    float fx = x - floor(x), fy = y - floor(y);
    return (Fetch(rgba, linear, width, height, ix, iy, channel) * (1 - fx) +
            Fetch(rgba, linear, width, height, ix + 1, iy, channel) * fx) *
               (1 - fy) +
           (Fetch(rgba, linear, width, height, ix, iy + 1, channel) * (1 - fx) +
            Fetch(rgba, linear, width, height, ix + 1, iy + 1, channel) * fx) *
               fy;
}

// One output pixel per lane; the sample sum retains the scalar iteration order.
export void FilterEnvironment(uniform const uint8 rgba[], uniform int width, uniform int height,
                              uniform const float linear[], uniform const EnvironmentFrame frames[],
                              uniform const EnvironmentDirection directions[], uniform int begin,
                              uniform int end, uniform float output[])
{
    for (uniform int tile = 0; tile < 7; ++tile)
    {
        uniform int first = max(begin, tile * 2048);
        uniform int last = min(end, (tile + 1) * 2048);
        if (first >= last)
            continue;
        foreach (pixel = first... last)
        {
            int p = pixel - tile * 2048;
            EnvironmentFrame frame = frames[p];
            float red = 0, green = 0, blue = 0, weight = 0;
            if (tile == 0)
            {
                red = Sample(rgba, linear, width, height, frame.nx, frame.ny, frame.nz, 0);
                green = Sample(rgba, linear, width, height, frame.nx, frame.ny, frame.nz, 1);
                blue = Sample(rgba, linear, width, height, frame.nx, frame.ny, frame.nz, 2);
                weight = 1;
            }
            else
            {
                for (uniform int i = 0; i < 128; ++i)
                {
                    uniform EnvironmentDirection d = directions[tile * 128 + i];
                    float hx = frame.tx * d.x + frame.bx * d.y + frame.nx * d.z;
                    float hy = frame.ty * d.x + frame.by * d.y + frame.ny * d.z;
                    float hz = frame.tz * d.x + frame.bz * d.y + frame.nz * d.z;
                    float lx = hx, ly = hy, lz = hz, w = 1;
                    if (tile != 6)
                    {
                        float nh2 = 2 * (frame.nx * hx + frame.ny * hy + frame.nz * hz);
                        lx = hx * nh2 - frame.nx;
                        ly = hy * nh2 - frame.ny;
                        lz = hz * nh2 - frame.nz;
                        w = max(0.f, frame.nx * lx + frame.ny * ly + frame.nz * lz);
                    }
                    red += Sample(rgba, linear, width, height, lx, ly, lz, 0) * w;
                    green += Sample(rgba, linear, width, height, lx, ly, lz, 1) * w;
                    blue += Sample(rgba, linear, width, height, lx, ly, lz, 2) * w;
                    weight += w;
                }
            }
            float inverseWeight = 1 / max(weight, 1e-6f);
            output[pixel * 4] = red * inverseWeight;
            output[pixel * 4 + 1] = green * inverseWeight;
            output[pixel * 4 + 2] = blue * inverseWeight;
            output[pixel * 4 + 3] = 1;
        }
    }
}
