// Minimal physics abstraction layer.
// If Jolt is added later, implement the functions here to forward to Jolt.

#include "../include/Physics/PhysicsSystem.hpp"
#include <iostream>
#include <vector>
#include <mutex>
#include <raymath.h>
#include <cfloat>

// Simple in-process physics backend: store per-handle per-mesh AABBs (world-space)
struct StaticMeshEntry {
    int handle;
    std::vector<BoundingBox> boxes;
    struct Tri { Vector3 a,b,c; };
    std::vector<Tri> triangles;
};

static std::vector<StaticMeshEntry> g_staticMeshes;
static int g_nextHandle = 1;
static std::mutex g_staticMeshesMutex;

namespace Hotones { namespace Physics {

bool InitPhysics() {
    // No-op stub for now. Replace with Jolt initialization when available.
    std::cerr << "Physics: InitPhysics() - stub (no backend)" << std::endl;
    return true;
}

void ShutdownPhysics() {
    std::cerr << "Physics: ShutdownPhysics() - stub" << std::endl;
}

int RegisterStaticMeshFromModel(const Model& model, const Vector3& position) {
    // Build per-mesh bounding boxes and store them in world-space (apply position offset)
    StaticMeshEntry entry;
    entry.handle = 0;

    if (model.meshCount > 0 && model.meshes != NULL) {
        entry.boxes.reserve(model.meshCount);
        for (int i = 0; i < model.meshCount; ++i) {
            BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
            std::cerr << "Physics: mesh[" << i << "] vertexCount=" << model.meshes[i].vertexCount
                      << " triangleCount=" << model.meshes[i].triangleCount << " bbox(min="
                      << mb.min.x << "," << mb.min.y << "," << mb.min.z << " max="
                      << mb.max.x << "," << mb.max.y << "," << mb.max.z << ")\n";
            // Compute bbox from triangles (if any) to compare against GetMeshBoundingBox
            Vector3 triMin = { FLT_MAX, FLT_MAX, FLT_MAX };
            Vector3 triMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            int triStartIndex = (int)entry.triangles.size();
            // note: triangles for this mesh will have been appended below; we'll compute after
            entry.boxes.push_back(mb);
            // extract triangles from mesh indices/vertices
            Mesh m = model.meshes[i];
                if (m.triangleCount > 0 && m.indices != NULL && m.vertices != NULL) {
                int triCount = m.triangleCount;
                for (int t = 0; t < triCount; ++t) {
                    unsigned int i0 = m.indices[t*3 + 0];
                    unsigned int i1 = m.indices[t*3 + 1];
                    unsigned int i2 = m.indices[t*3 + 2];
                    Vector3 va = (Vector3){ m.vertices[i0*3+0], m.vertices[i0*3+1], m.vertices[i0*3+2] };
                    Vector3 vb = (Vector3){ m.vertices[i1*3+0], m.vertices[i1*3+1], m.vertices[i1*3+2] };
                    Vector3 vc = (Vector3){ m.vertices[i2*3+0], m.vertices[i2*3+1], m.vertices[i2*3+2] };
                    // transform to world-space (apply model position)
                    va = Vector3Add(va, position);
                    vb = Vector3Add(vb, position);
                    vc = Vector3Add(vc, position);
                    entry.triangles.push_back({va,vb,vc});
                }
            } else if (m.vertexCount > 0 && m.vertices != NULL) {
                // fallback: vertices arranged as triangle list
                int vcount = m.vertexCount;
                int triCount = vcount / 3;
                for (int t = 0; t < triCount; ++t) {
                    Vector3 va = (Vector3){ m.vertices[(t*3+0)*3+0], m.vertices[(t*3+0)*3+1], m.vertices[(t*3+0)*3+2] };
                    Vector3 vb = (Vector3){ m.vertices[(t*3+1)*3+0], m.vertices[(t*3+1)*3+1], m.vertices[(t*3+1)*3+2] };
                    Vector3 vc = (Vector3){ m.vertices[(t*3+2)*3+0], m.vertices[(t*3+2)*3+1], m.vertices[(t*3+2)*3+2] };
                    va = Vector3Add(va, position);
                    vb = Vector3Add(vb, position);
                    vc = Vector3Add(vc, position);
                    entry.triangles.push_back({va,vb,vc});
                }
                // After adding triangles for this mesh, compute triangle bbox
                for (size_t ti = triStartIndex; ti < entry.triangles.size(); ++ti) {
                    const auto &T = entry.triangles[ti];
                    triMin.x = fminf(triMin.x, fminf(T.a.x, fminf(T.b.x, T.c.x)));
                    triMin.y = fminf(triMin.y, fminf(T.a.y, fminf(T.b.y, T.c.y)));
                    triMin.z = fminf(triMin.z, fminf(T.a.z, fminf(T.b.z, T.c.z)));
                    triMax.x = fmaxf(triMax.x, fmaxf(T.a.x, fmaxf(T.b.x, T.c.x)));
                    triMax.y = fmaxf(triMax.y, fmaxf(T.a.y, fmaxf(T.b.y, T.c.y)));
                    triMax.z = fmaxf(triMax.z, fmaxf(T.a.z, fmaxf(T.b.z, T.c.z)));
                }
                if (triStartIndex < (int)entry.triangles.size()) {
                    // transform by model position to match mb above
                    triMin = Vector3Add(triMin, position);
                    triMax = Vector3Add(triMax, position);
                    std::cerr << "Physics: mesh[" << i << "] triBBox(min=" << triMin.x << "," << triMin.y << "," << triMin.z
                              << " max=" << triMax.x << "," << triMax.y << "," << triMax.z << ")\n";
                }
            }
        }
    }

    std::lock_guard<std::mutex> lk(g_staticMeshesMutex);
    entry.handle = g_nextHandle++;
    g_staticMeshes.push_back(std::move(entry));
    return g_staticMeshes.back().handle;
}

void UnregisterStaticMesh(int handle) {
    std::lock_guard<std::mutex> lk(g_staticMeshesMutex);
    for (auto it = g_staticMeshes.begin(); it != g_staticMeshes.end(); ++it) {
        if (it->handle == handle) {
            g_staticMeshes.erase(it);
            return;
        }
    }
}

bool SweepSphereAgainstStatic(int handle, const Vector3& start, const Vector3& end, float radius, Vector3& hitPos, Vector3& hitNormal, float& t) {
    // Find entry
    StaticMeshEntry entry;
    {
        std::lock_guard<std::mutex> lk(g_staticMeshesMutex);
        bool found = false;
        for (const auto &e : g_staticMeshes) {
            if (e.handle == handle) { entry = e; found = true; break; }
        }
        if (!found) return false;
    }

    // Sampled swept-sphere against triangles approach (approximate but robust):
    Vector3 d = Vector3Subtract(end, start);
    float segLen = Vector3Length(d);
    if (segLen <= 1e-8f) return false;

    const int SAMPLES = 24; // configurable sampling resolution
    bool found = false;
    float bestU = 1.0f + 1e-6f;
    Vector3 bestHitPos = {0,0,0};
    Vector3 bestHitNormal = {0,0,0};

    auto ClosestPointOnTriangle = [](const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c) {
        // from Real-Time Collision Detection (Ericson)
        Vector3 ab = Vector3Subtract(b,a);
        Vector3 ac = Vector3Subtract(c,a);
        Vector3 ap = Vector3Subtract(p,a);
        float d1 = Vector3DotProduct(ab, ap);
        float d2 = Vector3DotProduct(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        Vector3 bp = Vector3Subtract(p,b);
        float d3 = Vector3DotProduct(ab, bp);
        float d4 = Vector3DotProduct(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;

        float vc = d1*d4 - d3*d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float v = d1 / (d1 - d3);
            return Vector3Add(a, Vector3Scale(ab, v));
        }

        Vector3 cp = Vector3Subtract(p,c);
        float d5 = Vector3DotProduct(ab, cp);
        float d6 = Vector3DotProduct(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        float vb = d5*d2 - d1*d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            float w = d2 / (d2 - d6);
            return Vector3Add(a, Vector3Scale(ac, w));
        }

        float va = d3*d6 - d5*d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return Vector3Add(b, Vector3Scale(Vector3Subtract(c,b), w));
        }

        // inside face region. Project p onto plane of triangle
        Vector3 denom = Vector3CrossProduct(ab, ac);
        Vector3 n = Vector3Normalize(denom);
        float dist = Vector3DotProduct(ap, n);
        Vector3 proj = Vector3Subtract(p, Vector3Scale(n, dist));
        return proj;
    };

    for (const auto &tri : entry.triangles) {
        // sample along path
        float prevDist2 = FLT_MAX;
        Vector3 prevClosest = {0,0,0};
        for (int s = 0; s <= SAMPLES; ++s) {
            float u = (float)s / (float)SAMPLES;
            Vector3 center = Vector3Add(start, Vector3Scale(d, u));
            Vector3 closest = ClosestPointOnTriangle(center, tri.a, tri.b, tri.c);
            Vector3 diff = Vector3Subtract(center, closest);
            float dist2 = Vector3DotProduct(diff, diff);
            if (dist2 <= radius*radius) {
                // found intersection between u_prev and u; refine by binary search
                float low = fmaxf(0.0f, u - 1.0f / SAMPLES);
                float high = u;
                for (int iter = 0; iter < 6; ++iter) {
                    float mid = 0.5f * (low + high);
                    Vector3 cmid = Vector3Add(start, Vector3Scale(d, mid));
                    Vector3 clos = ClosestPointOnTriangle(cmid, tri.a, tri.b, tri.c);
                    Vector3 ddv = Vector3Subtract(cmid, clos);
                    float d2 = Vector3DotProduct(ddv, ddv);
                    if (d2 <= radius*radius) high = mid; else low = mid;
                }
                float hitU = 0.5f * (low + high);
                if (hitU < bestU) {
                    bestU = hitU;
                    Vector3 hitCenter = Vector3Add(start, Vector3Scale(d, bestU));
                    Vector3 clos = ClosestPointOnTriangle(hitCenter, tri.a, tri.b, tri.c);
                    Vector3 n = Vector3Subtract(hitCenter, clos);
                    float nlen = Vector3Length(n);
                    if (nlen > 1e-6f) bestHitNormal = Vector3Scale(n, 1.0f / nlen);
                    else {
                        // use triangle normal
                        Vector3 nface = Vector3CrossProduct(Vector3Subtract(tri.b, tri.a), Vector3Subtract(tri.c, tri.a));
                        bestHitNormal = Vector3Normalize(nface);
                    }
                    bestHitPos = clos;
                    found = true;
                }
                break; // stop sampling this triangle
            }
            prevDist2 = dist2;
            prevClosest = prevClosest; // no-op but keeps previous in scope
        }
    }

    if (!found) return false;

    hitPos = bestHitPos;
    hitNormal = bestHitNormal;
    t = bestU;
    return true;
}

}} // namespace Hotones::Physics
