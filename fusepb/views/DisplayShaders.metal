/* DisplayShaders.metal: Passthrough vertex and fragment shaders for the
   Spectrum display and its overlay icons.

   Copyright (c) 2026 The FuseX authors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include <metal_stdlib>

using namespace metal;

struct DisplayVertex {
  float2 position;
  float2 tex_coord;
};

struct VertexOut {
  float4 position [[position]];
  float2 tex_coord;
};

vertex VertexOut
display_vertex( uint vid [[vertex_id]],
                constant DisplayVertex *vertices [[buffer(0)]] )
{
  VertexOut out;
  out.position = float4( vertices[vid].position, 0.0, 1.0 );
  out.tex_coord = vertices[vid].tex_coord;
  return out;
}

fragment float4
display_fragment( VertexOut in [[stage_in]],
                  texture2d<float> tex [[texture(0)]] )
{
  constexpr sampler s( mag_filter::nearest, min_filter::nearest,
                       address::clamp_to_edge );
  return tex.sample( s, in.tex_coord );
}
