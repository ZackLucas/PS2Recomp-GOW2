// One instance per primitive; the quad covers the primitive's bounding box. Coverage is decided
// per fragment (gs_draw.frag), so the GL rasterization rules never influence the result.
flat out uint vPrim;
uniform uint uBaseInstance;   // first primitive of this draw (draws are split when the target mapping changes)

void main()
{
    const uint id = uint(gl_InstanceID) + uBaseInstance;
    const ivec4 bb = gsPrimBounds(id);
    vPrim = id;
    if (bb.z < bb.x || bb.w < bb.y)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);    // outside the clip volume: culled
        return;
    }
    const int px = ((gl_VertexID & 1) != 0) ? bb.z + 1 : bb.x;
    const int py = ((gl_VertexID & 2) != 0) ? bb.w + 1 : bb.y;
    gl_Position = vec4(float(px) / 1024.0 - 1.0, float(py) / 1024.0 - 1.0, 0.0, 1.0);   // 2048x2048 target
}
