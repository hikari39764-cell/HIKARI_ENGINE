#pragma once

#include <cstdint>

#if defined(_WIN32)
#define HIKARI_COACD_CALL __cdecl
#else
#define HIKARI_COACD_CALL
#endif

extern "C" {

    struct CoACD_Mesh {
        double* vertices_ptr;
        uint64_t vertices_count;
        int* triangles_ptr;
        uint64_t triangles_count;
    };

    struct CoACD_MeshArray {
        CoACD_Mesh* meshes_ptr;
        uint64_t meshes_count;
    };

    using CoACD_RunFn = CoACD_MeshArray(HIKARI_COACD_CALL*)(
        const CoACD_Mesh& input,
        double threshold,
        int maxConvexHull,
        int preprocessMode,
        int preprocessResolution,
        int sampleResolution,
        int mctsNodes,
        int mctsIterations,
        int mctsMaxDepth,
        bool pca,
        bool merge,
        bool decimate,
        int maxConvexHullVertices,
        bool extrude,
        double extrudeMargin,
        int approximationMode,
        unsigned int seed,
        bool realMetric);

    using CoACD_FreeMeshArrayFn = void(HIKARI_COACD_CALL*)(
        CoACD_MeshArray meshes);
    using CoACD_SetLogLevelFn = void(HIKARI_COACD_CALL*)(
        const char* level);

} // extern "C"
