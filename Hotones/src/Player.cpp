#include <GFX/Player.hpp>
#include <GFX/CollidableModel.hpp>
#include <Input/Input.hpp>
#include <iostream>
#include <cmath>
#include <SFX/AudioSystem.hpp>

namespace Hotones {

Player::Player() {
    body = { 0 };
    lookRotation = { 0 };
    sensitivity = { 0.001f, 0.001f };
    headTimer = 0.0f;
    walkLerp = 0.0f;
    headLerp = STAND_HEIGHT;
    lean = { 0 };
    m_attachedCamera = nullptr;
}

void Player::AttachCamera(Camera3D* camera) {
    m_attachedCamera = camera;
}

void Player::AttachWorld(std::shared_ptr<CollidableModel> world) {
    m_worldModel = world;
}

void Player::Update() {
    if (!m_attachedCamera) return;

    Vector2 mouseDelta = Hotones::Input::GetMouseDelta();
    lookRotation.x -= mouseDelta.x * sensitivity.x;
    lookRotation.y += mouseDelta.y * sensitivity.y;

    char sideway = (char)(Hotones::Input::IsKeyDown(KEY_D) - Hotones::Input::IsKeyDown(KEY_A));
    char forward = (char)(Hotones::Input::IsKeyDown(KEY_W) - Hotones::Input::IsKeyDown(KEY_S));
    bool crouching = Hotones::Input::IsKeyDown(KEY_LEFT_CONTROL);
    bool jumpPressed = Hotones::Input::IsKeyDown(KEY_SPACE);

    TraceLog(LOG_INFO, "Player::UpdateBody input side=%d forward=%d jumpPressed=%d grounded=%d pos=(%f,%f,%f) vel=(%f,%f,%f)",
             sideway, forward, jumpPressed ? 1 : 0, body.isGrounded ? 1 : 0,
             body.position.x, body.position.y, body.position.z,
             body.velocity.x, body.velocity.y, body.velocity.z);

    UpdateBody(sideway, forward, jumpPressed, crouching);

    float delta = GetFrameTime();
    headLerp = Lerp(headLerp, (crouching ? CROUCH_HEIGHT : STAND_HEIGHT), 20.0f * delta);
    
    m_attachedCamera->position = (Vector3){
        body.position.x,
        body.position.y + (BOTTOM_HEIGHT + headLerp),
        body.position.z,
    };

    if (body.isGrounded && ((forward != 0) || (sideway != 0))) {
        headTimer += delta * 3.0f;
        walkLerp = Lerp(walkLerp, 1.0f, 10.0f * delta);
        m_attachedCamera->fovy = Lerp(m_attachedCamera->fovy, 55.0f, 5.0f * delta);
    } else {
        walkLerp = Lerp(walkLerp, 0.0f, 10.0f * delta);
        m_attachedCamera->fovy = Lerp(m_attachedCamera->fovy, 60.0f, 5.0f * delta);
    }

    // Footstep trigger: when head bob sine crosses from negative to positive
    float newHeadSin = sinf(headTimer * PI);
    if (body.isGrounded && ((forward != 0) || (sideway != 0))) {
        if (prevHeadSin <= 0.0f && newHeadSin > 0.0f && walkLerp > 0.1f) {
            Ho_tones::GetSoundBus().PlaySequentialAsync("footstep", 1.0f);
        }
    }
    prevHeadSin = newHeadSin;

    lean.x = Lerp(lean.x, (float)sideway * 0.02f, 10.0f * delta);
    lean.y = Lerp(lean.y, (float)forward * 0.015f, 10.0f * delta);

    UpdateCamera();
}

void Player::UpdateBody(char side, char forward, bool jumpPressed, bool crouchHold) {
    Vector2 input = (Vector2){ (float)side, (float)-forward };

    float delta = GetFrameTime();

    if (!body.isGrounded) body.velocity.y -= GRAVITY * delta;

    static bool wasJumpHeld = false;
    // if (enableSourceBhop) {
    //     // Source bhop bug: allow holding jump to jump on landing
    //     if (body.isGrounded && jumpPressed) {
    //         body.velocity.y = JUMP_FORCE;
    //         body.isGrounded = false;
    //     } else if (body.isGrounded && wasJumpHeld && IsKeyDown(KEY_SPACE)) {
    //         // If jump is still held from previous frame, auto-jump
    //         body.velocity.y = JUMP_FORCE;
    //         body.isGrounded = false;
    //     }
    //     wasJumpHeld = IsKeyDown(KEY_SPACE);
    // } else {
        if (body.isGrounded && jumpPressed) {
            TraceLog(LOG_INFO, "Player::UpdateBody jump triggered: setting velocity.y from %f to %f", body.velocity.y, JUMP_FORCE);
            body.velocity.y = JUMP_FORCE;
            body.isGrounded = false;
        } else if (jumpPressed && !body.isGrounded) {
            TraceLog(LOG_INFO, "Player::UpdateBody jump pressed but not grounded (ignored)");
        }
    // }

    Vector3 front = (Vector3){ sinf(lookRotation.x), 0.f, cosf(lookRotation.x) };
    Vector3 right = (Vector3){ cosf(-lookRotation.x), 0.f, sinf(-lookRotation.x) };

    Vector3 desiredDir = (Vector3){ input.x * right.x + input.y * front.x, 0.0f, input.x * right.z + input.y * front.z, };
    body.dir = Vector3Lerp(body.dir, desiredDir, CONTROL * delta);

    float decel = (body.isGrounded ? FRICTION : AIR_DRAG);
    Vector3 hvel = (Vector3){ body.velocity.x * decel, 0.0f, body.velocity.z * decel };

    float hvelLength = Vector3Length(hvel);
    if (hvelLength < (MAX_SPEED * 0.01f)) hvel = (Vector3){ 0 };

    float speed = Vector3DotProduct(hvel, body.dir);
    float maxSpeed = (crouchHold ? CROUCH_SPEED : MAX_SPEED);
    float accel;
    if (enableSourceBhop) {
        // Allow speed to build up beyond maxSpeed when bhopping
        accel = Clamp(MAX_ACCEL * 0.1f, 0.f, MAX_ACCEL * delta);
    } else {
        accel = Clamp(maxSpeed - speed, 0.f, MAX_ACCEL * delta);
    }
    hvel.x += body.dir.x * accel;
    hvel.z += body.dir.z * accel;

    body.velocity.x = hvel.x;
    body.velocity.z = hvel.z;

    Vector3 startPos = body.position;
    Vector3 remaining = Vector3{ body.velocity.x * delta, body.velocity.y * delta, body.velocity.z * delta };
    const float playerRadius = 0.5f;

    if (m_worldModel) {
        const int maxIters = 3;
        Vector3 curr = startPos;

        // Subdivide the total remaining travel into smaller steps to avoid tunneling
        float remLen = Vector3Length(remaining);
        float stepMax = fmaxf(playerRadius * 0.5f, 0.25f); // maximum allowed step length
        int steps = (int)ceilf(remLen / stepMax);
        steps = fmax(1, steps);
        // clamp to avoid excessive loops
        steps = (steps > 16) ? 16 : steps;

        Vector3 stepVec = Vector3Scale(remaining, 1.0f / (float)steps);

        for (int s = 0; s < steps; ++s) {
            Vector3 target = Vector3Add(curr, stepVec);
            Vector3 hitPos, hitNormal; float tHit;
            if (!m_worldModel->SweepSphere(curr, target, playerRadius, hitPos, hitNormal, tHit)) {
                // no hit: move to target and continue
                curr = target;
                continue;
            }

            // move to impact point
            curr = hitPos;

            // compute remaining after impact within this step
            Vector3 travel = Vector3Subtract(target, hitPos);
            float vn = Vector3DotProduct(travel, hitNormal);
            Vector3 proj = Vector3Scale(hitNormal, vn);
            Vector3 slide = Vector3Subtract(travel, proj);

            // small epsilon to avoid sticking
            curr = Vector3Add(curr, Vector3Scale(hitNormal, 0.001f));

            // apply slide as the remaining motion for the rest of this update
            remaining = Vector3Add(Vector3Scale(stepVec, (float)(steps - s - 1)), slide);

            // If hit normal is mostly up, consider grounded and zero vertical velocity
            if (hitNormal.y > 0.5f) {
                body.isGrounded = true;
                body.velocity.y = 0.0f;
                TraceLog(LOG_INFO, "Player::UpdateBody grounded via sweep hit (y=%f) at pos=(%f,%f,%f)", hitNormal.y, curr.x, curr.y, curr.z);
            }

            // attempt a few iterations of sliding resolution
            // similar to previous loop but limited
            for (int iter = 0; iter < maxIters; ++iter) {
                Vector3 nextTarget = Vector3Add(curr, remaining);
                if (!m_worldModel->SweepSphere(curr, nextTarget, playerRadius, hitPos, hitNormal, tHit)) {
                    curr = nextTarget;
                    break;
                }
                curr = hitPos;
                travel = Vector3Subtract(nextTarget, hitPos);
                vn = Vector3DotProduct(travel, hitNormal);
                proj = Vector3Scale(hitNormal, vn);
                slide = Vector3Subtract(travel, proj);
                curr = Vector3Add(curr, Vector3Scale(hitNormal, 0.001f));
                remaining = slide;
                if (hitNormal.y > 0.5f) {
                    body.isGrounded = true;
                    body.velocity.y = 0.0f;
                    break;
                }
            }
            // after resolving this impact, continue from current position with updated remaining
        }

        // final position is the swept result `curr`
        body.position = curr;
        // update velocity to match actual movement
        body.velocity = Vector3Scale(Vector3Subtract(body.position, startPos), 1.0f / delta);

        // Ensure we are not left overlapping geometry due to numeric issues by
        // resolving any residual sphere collisions against the world meshes.
        // This pushes the player out of intersecting AABBs and marks grounded
        // when appropriate to avoid walking through thin or missed collisions.
        bool pushed = m_worldModel->ResolveSphereCollision(body.position, playerRadius);
        if (pushed) {
            // If ResolveSphereCollision pushed us upwards, consider grounded.
            // Conservative: zero vertical velocity to avoid tunneling.
            TraceLog(LOG_INFO, "Player::UpdateBody ResolveSphereCollision pushed player to pos=(%f,%f,%f)", body.position.x, body.position.y, body.position.z);
            body.velocity.y = 0.0f;
            body.isGrounded = true;
        }
    } else {
        // no world: simple move
        body.position.x += remaining.x;
        body.position.y += remaining.y;
        body.position.z += remaining.z;
    }

    if (body.position.y <= 0.0f) {
        body.position.y = 0.0f;
        body.velocity.y = 0.0f;
        body.isGrounded = true;
            // Cap horizontal speed to MAX_SPEED when landing (default behavior)
            float hSpeed = sqrtf(body.velocity.x * body.velocity.x + body.velocity.z * body.velocity.z);
            if (hSpeed > MAX_SPEED) {
                float scale = MAX_SPEED / hSpeed;
                body.velocity.x *= scale;
                body.velocity.z *= scale;
            }
        
    }
}

void Player::UpdateCamera() {
    if (!m_attachedCamera) return;

    const Vector3 up = (Vector3){ 0.0f, 1.0f, 0.0f };
    const Vector3 targetOffset = (Vector3){ 0.0f, 0.0f, -1.0f };

    Vector3 yaw = Vector3RotateByAxisAngle(targetOffset, up, lookRotation.x);

    float maxAngleUp = Vector3Angle(up, yaw);
    maxAngleUp -= 0.001f;
    if (-(lookRotation.y) > maxAngleUp) { lookRotation.y = -maxAngleUp; }

    float maxAngleDown = Vector3Angle(Vector3Negate(up), yaw);
    maxAngleDown *= -1.0f;
    maxAngleDown += 0.001f;
    if (-(lookRotation.y) < maxAngleDown) { lookRotation.y = -maxAngleDown; }

    Vector3 right = Vector3Normalize(Vector3CrossProduct(yaw, up));

    float pitchAngle = -lookRotation.y - lean.y;
    pitchAngle = Clamp(pitchAngle, -PI / 2 + 0.0001f, PI / 2 - 0.0001f);
    Vector3 pitch = Vector3RotateByAxisAngle(yaw, right, pitchAngle);

    float headSin = sinf(headTimer * PI);
    float headCos = cosf(headTimer * PI);
    const float stepRotation = 0.01f;
    m_attachedCamera->up = Vector3RotateByAxisAngle(up, pitch, headSin * stepRotation + lean.x);

    const float bobSide = 0.1f;
    const float bobUp = 0.15f;
    Vector3 bobbing = Vector3Scale(right, headSin * bobSide);
    bobbing.y = fabsf(headCos * bobUp);

    m_attachedCamera->position = Vector3Add(m_attachedCamera->position, Vector3Scale(bobbing, walkLerp));
    m_attachedCamera->target = Vector3Add(m_attachedCamera->position, pitch);
}

void Player::Render() {
    // Currently logic for drawing level is in main, but maybe player wants to draw something?
    // main.cpp doesn't have player rendering in the second half.
}

} // namespace Hotones
