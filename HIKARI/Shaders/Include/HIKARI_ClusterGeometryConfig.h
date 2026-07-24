#ifndef HIKARI_CLUSTER_GEOMETRY_CONFIG_INCLUDED
#define HIKARI_CLUSTER_GEOMETRY_CONFIG_INCLUDED

// Shared cluster / meshlet limits used by the C++ cooker/runtime and HLSL
// shaders. Keep this file preprocessor-only so both sides can include it.
//
// Constraints:
// - Triangle count must fit the mesh shader primitive output array.
// - Vertex count must fit packed meshlet-local 8-bit primitive indices.
// - Mesh shader thread count must cover both triangle and vertex loops.
#define HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_TRIANGLES 124
#define HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_VERTICES 64
#define HIKARI_CLUSTER_GEOMETRY_CONFIG_MESHLET_MS_THREAD_COUNT 128

// Cluster payload count per amplification shader group. Shared with culling
// range splitting so the AS payload and emitted ranges stay in lockstep.
#define HIKARI_CLUSTER_GEOMETRY_CONFIG_AS_CLUSTER_PAYLOAD 64

#endif
