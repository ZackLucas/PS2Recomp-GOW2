// Helpers shared by gs_pixel.glsl that do not need any buffer declaration.
uint gsRgba5551ToRgba8888(uint c)
{
    return (((c >> 0u) & 0x1Fu) << 3u) | ((((c >> 5u) & 0x1Fu) << 3u) << 8u) |
           ((((c >> 10u) & 0x1Fu) << 3u) << 16u) | ((((c >> 15u) & 0x01u) << 7u) << 24u);
}
