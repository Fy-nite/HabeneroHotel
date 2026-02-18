#include <GFX/CollidableModel.hpp>
#include "AssetPath.hpp"
#include <raylib.h>
#include <raymath.h>
#include <GFX/bsp.hpp>
#if defined(_WIN32)
#include <crtdbg.h>
#endif
#include <Physics/PhysicsSystem.hpp>

namespace Hotones {

CollidableModel::CollidableModel(const std::string& path, Vector3 position)
    : position(position)
{
    std::string resolved = ResolveAssetPath(path);
    const char* loadPath = (!resolved.empty() ? resolved.c_str() : path.c_str());
    TraceLog(LOG_INFO, "CollidableModel: loading model '%s'", loadPath);
    // Check file exists before calling into raylib's loaders (can crash if file missing/corrupt)
    FILE *f = fopen(loadPath, "rb");
    if (!f) {
        TraceLog(LOG_ERROR, "CollidableModel: model file not found: %s", loadPath);
        model = {0};
    } else {
        fclose(f);
        // If the file appears to be a BSP, attempt to load via the BSP importer
        try {
            std::filesystem::path p(loadPath);
            std::string ext = p.extension().string();
            for (auto &c : ext) c = (char)tolower(c);
            // if (ext == ".bsp") {
            //     auto models = LoadModelsFromBSPFile(p);
            //     if (!models.empty()) {
            //         // Use the first generated model and unload any extras
            //         model = std::move(models[0]);
            //         for (size_t i = 1; i < models.size(); ++i) {
            //             UnloadModel(models[i]);
            //         }
            //     } else {
            //         TraceLog(LOG_ERROR, "CollidableModel: failed to import BSP: %s", loadPath);
            //         model = {0};
            //     }
            // } else {
            model = LoadModel(loadPath);
            
            
        } catch (const std::exception &e) {
            TraceLog(LOG_ERROR, "CollidableModel: exception while loading model: %s: %s", loadPath, e.what());
            model = {0};
        } catch (...) {
            TraceLog(LOG_ERROR, "CollidableModel: unknown exception while loading model: %s", loadPath);
            model = {0};
        }
    }
    if (model.meshCount <= 0 || model.meshes == NULL) {
        TraceLog(LOG_WARNING, "CollidableModel: loaded model has no meshes or failed to load meshes (meshes=%p, meshCount=%d)", (const void*)model.meshes, model.meshCount);
    }
    UpdateBoundingBox();

    // Register this model's geometry with the physics system (best-effort).
    physicsHandle = -1;
    physicsHandle = Hotones::Physics::RegisterStaticMeshFromModel(model, position);
}

CollidableModel::~CollidableModel() {
    TraceLog(LOG_INFO, "CollidableModel: unloading model (meshes=%p, meshCount=%d, materials=%p, materialCount=%d)",
             (const void*)model.meshes, model.meshCount, (const void*)model.materials, model.materialCount);
    // Only call UnloadModel if there is something to unload or pointers are non-null
    if (model.meshCount > 0 || model.materialCount > 0 || model.meshes != NULL || model.materials != NULL) {
#if defined(_WIN32)
        // Check CRT heap for corruption before unload (debug builds)
        _CrtCheckMemory();
#endif
        UnloadModel(model);
#if defined(_WIN32)
        // Check CRT heap after unload to catch overruns early
        _CrtCheckMemory();
#endif
        // Clear the model struct so dangling pointers are not reused
        model = {0};
    }
    // Unregister from physics system if needed
    if (physicsHandle != -1) {
        Hotones::Physics::UnregisterStaticMesh(physicsHandle);
        physicsHandle = -1;
    }
}

void CollidableModel::Draw() {
    DrawModel(model, position, 1.0f, WHITE);
}

void CollidableModel::SetPosition(Vector3 pos) {
    position = pos;
    UpdateBoundingBox();
}

Vector3 CollidableModel::GetPosition() const {
    return position;
}

BoundingBox CollidableModel::GetBoundingBox() const {
    return bbox;
}
bool CollidableModel::CheckCollision(const BoundingBox& other) const {
    return CheckCollisionBoxes(bbox, other);
}

bool CollidableModel::CheckCollision(Vector3 point) const {
    return (point.x >= bbox.min.x && point.x <= bbox.max.x) &&
           (point.y >= bbox.min.y && point.y <= bbox.max.y) &&
           (point.z >= bbox.min.z && point.z <= bbox.max.z);
}

// Clamp point to box
static Vector3 ClampPointToBox(const Vector3 &p, const BoundingBox &b) {
    Vector3 out;
    out.x = fmaxf(b.min.x, fminf(p.x, b.max.x));
    out.y = fmaxf(b.min.y, fminf(p.y, b.max.y));
    out.z = fmaxf(b.min.z, fminf(p.z, b.max.z));
    return out;
}

bool CollidableModel::ResolveSphereCollision(Vector3 &center, float radius) {
    bool collided = false;
    if (model.meshCount <= 0 || model.meshes == NULL) return false;

    for (int i = 0; i < model.meshCount; i++) {
        BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
        // offset by model position
        mb.min = Vector3Add(mb.min, position);
        mb.max = Vector3Add(mb.max, position);

        // find closest point on box to sphere center
        Vector3 closest = ClampPointToBox(center, mb);
        Vector3 diff = Vector3Subtract(center, closest);
        float dist2 = Vector3DotProduct(diff, diff);
        float r2 = radius * radius;
        if (dist2 < r2) {
            float dist = sqrtf(dist2);
            Vector3 push;
            if (dist > 0.0001f) {
                push = Vector3Scale(diff, (radius - dist) / dist);
            } else {
                // center is inside the box. Compute shortest axis to push out along
                // using distances to each face, like SweepSphere's normal selection.
                float dxMin = fabsf(center.x - mb.min.x);
                float dxMax = fabsf(center.x - mb.max.x);
                float dyMin = fabsf(center.y - mb.min.y);
                float dyMax = fabsf(center.y - mb.max.y);
                float dzMin = fabsf(center.z - mb.min.z);
                float dzMax = fabsf(center.z - mb.max.z);
                float minDist = dxMin; push = (Vector3){ -1,0,0 };
                if (dxMax < minDist) { minDist = dxMax; push = (Vector3){ 1,0,0 }; }
                if (dyMin < minDist) { minDist = dyMin; push = (Vector3){ 0,-1,0 }; }
                if (dyMax < minDist) { minDist = dyMax; push = (Vector3){ 0,1,0 }; }
                if (dzMin < minDist) { minDist = dzMin; push = (Vector3){ 0,0,-1 }; }
                if (dzMax < minDist) { minDist = dzMax; push = (Vector3){ 0,0,1 }; }
                // push out by radius plus small epsilon to avoid re-penetration
                push = Vector3Scale(push, radius + 0.001f);
            }
            center = Vector3Add(center, push);
            collided = true;
        }
    }
    return collided;
}

// Ray (segment) vs AABB slab intersection. Returns true if ray origin + d*t intersects box within t in [0,1].
static bool RayAABBIntersect(const Vector3 &o, const Vector3 &d, const BoundingBox &b, float &tOut, int &hitAxis) {
    const float EPS = 1e-8f;
    float tmin = -INFINITY, tmax = INFINITY;
    float t1, t2;
    float ta[3], tb[3];
    float od[3] = { o.x, o.y, o.z };
    float dd[3] = { d.x, d.y, d.z };
    float bmin[3] = { b.min.x, b.min.y, b.min.z };
    float bmax[3] = { b.max.x, b.max.y, b.max.z };
    for (int i = 0; i < 3; ++i) {
        if (fabsf(dd[i]) < EPS) {
            if (od[i] < bmin[i] || od[i] > bmax[i]) return false;
            ta[i] = -INFINITY; tb[i] = INFINITY;
        } else {
            t1 = (bmin[i] - od[i]) / dd[i];
            t2 = (bmax[i] - od[i]) / dd[i];
            ta[i] = fminf(t1, t2);
            tb[i] = fmaxf(t1, t2);
        }
    }
    tmin = fmaxf(fmaxf(ta[0], ta[1]), ta[2]);
    tmax = fminf(fminf(tb[0], tb[1]), tb[2]);
    if (tmax < 0.0f) return false; // box is behind
    if (tmin > tmax) return false;
    // choose tmin as hit
    float t = tmin;
    // determine which axis caused tmin (closest)
    hitAxis = 0;
    float best = fabsf(t - ta[0]);
    for (int i = 1; i < 3; ++i) {
        float diff = fabsf(t - ta[i]);
        if (diff < best) { best = diff; hitAxis = i; }
    }
    tOut = t;
    return true;
}

bool Hotones::CollidableModel::SweepSphere(const Vector3 &start, const Vector3 &end, float radius, Vector3 &hitPos, Vector3 &hitNormal, float &t) {
    TraceLog(LOG_INFO, "CollidableModel::SweepSphere start=(%f,%f,%f) end=(%f,%f,%f) radius=%f",
             start.x, start.y, start.z, end.x, end.y, end.z, radius);
    Vector3 d = Vector3Subtract(end, start);
    float segLen = Vector3Length(d);
    if (segLen <= 1e-8f) return false;
    // Direction for ray-AABB test
    Vector3 dir = d;

    bool found = false;
    float bestT = 1.0f + 1e-6f;
    BoundingBox bestBox;
    int bestAxis = 0;

    if (model.meshCount <= 0 || model.meshes == NULL) {
        lastSweepHit = false;
        lastSweepStart = start;
        lastSweepEnd = end;
        lastSweepT = 0.0f;
        return false;
    }

    for (int i = 0; i < model.meshCount; i++) {
        BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
        // offset by model position
        mb.min = Vector3Add(mb.min, position);
        mb.max = Vector3Add(mb.max, position);
        BoundingBox orig = mb; // original (offset)
        // expand by radius
        mb.min.x -= radius; mb.min.y -= radius; mb.min.z -= radius;
        mb.max.x += radius; mb.max.y += radius; mb.max.z += radius;

        float localT;
        int axis;
        if (!RayAABBIntersect(start, dir, mb, localT, axis)) continue;
        // RayAABBIntersect returns t in the same parameterization as 'dir' (point = start + dir * t),
        // so when 'dir' is the full segment (end-start) the returned t is already normalized [0,1].
        float tNorm = localT;
        if (tNorm >= 0.0f && tNorm <= 1.0f) {
            if (tNorm < bestT) {
                bestT = tNorm;
                found = true;
                bestBox = orig;
                bestAxis = axis;
            }
        }
    }

    if (!found) {
        TraceLog(LOG_INFO, "CollidableModel::SweepSphere no AABB hit for segment");
        // store last sweep
        lastSweepHit = false;
        lastSweepStart = start;
        lastSweepEnd = end;
        lastSweepT = 0.0f;
        return false;
    }

    // compute hit position
    hitPos = Vector3Add(start, Vector3Scale(d, bestT));
    // compute hit normal by comparing hitPos to box faces
    const BoundingBox &b = bestBox;
    const float EPSN = 1e-3f;
    // check which face is closest
    float dxMin = fabsf(hitPos.x - b.min.x);
    float dxMax = fabsf(hitPos.x - b.max.x);
    float dyMin = fabsf(hitPos.y - b.min.y);
    float dyMax = fabsf(hitPos.y - b.max.y);
    float dzMin = fabsf(hitPos.z - b.min.z);
    float dzMax = fabsf(hitPos.z - b.max.z);
    // pick minimum distance
    float minDist = dxMin; hitNormal = (Vector3){ -1,0,0 };
    if (dxMax < minDist) { minDist = dxMax; hitNormal = (Vector3){ 1,0,0 }; }
    if (dyMin < minDist) { minDist = dyMin; hitNormal = (Vector3){ 0,-1,0 }; }
    if (dyMax < minDist) { minDist = dyMax; hitNormal = (Vector3){ 0,1,0 }; }
    if (dzMin < minDist) { minDist = dzMin; hitNormal = (Vector3){ 0,0,-1 }; }
    if (dzMax < minDist) { minDist = dzMax; hitNormal = (Vector3){ 0,0,1 }; }

    t = bestT;
    TraceLog(LOG_INFO, "CollidableModel::SweepSphere hit t=%f pos=(%f,%f,%f) normal=(%f,%f,%f)",
             t, hitPos.x, hitPos.y, hitPos.z, hitNormal.x, hitNormal.y, hitNormal.z);

    // store last sweep
    lastSweepHit = true;
    lastSweepStart = start;
    lastSweepEnd = end;
    lastSweepHitPos = hitPos;
    lastSweepHitNormal = hitNormal;
    lastSweepT = bestT;

    return true;
}

void Hotones::CollidableModel::DrawDebug() const {
    // draw per-mesh AABBs
    if (model.meshCount > 0 && model.meshes != NULL) {
        for (int i = 0; i < model.meshCount; ++i) {
            BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
            mb.min = Vector3Add(mb.min, position);
            mb.max = Vector3Add(mb.max, position);
            DrawBoundingBox(mb, Fade(YELLOW, 0.5f));
            DrawCubeWires((Vector3){ (mb.min.x+mb.max.x)/2.0f, (mb.min.y+mb.max.y)/2.0f, (mb.min.z+mb.max.z)/2.0f },
                          mb.max.x - mb.min.x, mb.max.y - mb.min.y, mb.max.z - mb.min.z, ORANGE);
        }
    }

    if (lastSweepHit) {
        // Draw sweep line
        DrawLine3D(lastSweepStart, lastSweepEnd, YELLOW);
        // Draw hit position and normal
        DrawSphere(lastSweepHitPos, 0.1f, GREEN);
        DrawLine3D(lastSweepHitPos, Vector3Add(lastSweepHitPos, Vector3Scale(lastSweepHitNormal, 0.5f)), BLUE);
    } else {
        // draw attempted sweep line
        DrawLine3D(lastSweepStart, lastSweepEnd, Fade(YELLOW, 0.25f));
    }
}

void Hotones::CollidableModel::DrawMeshBoundingBoxes(Color color) const {
    if (model.meshCount > 0 && model.meshes != NULL) {
        for (int i = 0; i < model.meshCount; ++i) {
            BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
            mb.min = Vector3Add(mb.min, position);
            mb.max = Vector3Add(mb.max, position);
            DrawBoundingBox(mb, Fade(color, 0.9f));
            DrawCubeWires((Vector3){ (mb.min.x+mb.max.x)/2.0f, (mb.min.y+mb.max.y)/2.0f, (mb.min.z+mb.max.z)/2.0f },
                          mb.max.x - mb.min.x, mb.max.y - mb.min.y, mb.max.z - mb.min.z, color);
        }
    }
}

void CollidableModel::UpdateBoundingBox() {
    // Compute union of all mesh bounding boxes and offset by model position
    if (model.meshCount > 0 && model.meshes != NULL) {
        BoundingBox accum = GetMeshBoundingBox(model.meshes[0]);
        for (int i = 1; i < model.meshCount; ++i) {
            BoundingBox mb = GetMeshBoundingBox(model.meshes[i]);
            // expand accum to include mb
            accum.min.x = fminf(accum.min.x, mb.min.x);
            accum.min.y = fminf(accum.min.y, mb.min.y);
            accum.min.z = fminf(accum.min.z, mb.min.z);
            accum.max.x = fmaxf(accum.max.x, mb.max.x);
            accum.max.y = fmaxf(accum.max.y, mb.max.y);
            accum.max.z = fmaxf(accum.max.z, mb.max.z);
        }
        // apply model position offset
        Vector3 offset = position;
        // Debug: print meshCount and computed accum bbox
        TraceLog(LOG_INFO, "CollidableModel: UpdateBoundingBox meshes=%d accum.min=(%f,%f,%f) accum.max=(%f,%f,%f)",
                 model.meshCount, accum.min.x, accum.min.y, accum.min.z, accum.max.x, accum.max.y, accum.max.z);
        accum.min = Vector3Add(accum.min, offset);
        accum.max = Vector3Add(accum.max, offset);
        bbox = accum;
    }
    else {
        // Fallback: empty bounding box at model position
        bbox.min = position;
        bbox.max = position;
    }
}

} // namespace Hotones
