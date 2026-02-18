#include <Physics/physics.h>
#include <iostream>

Vector3 Body::GetCenterOfMassWorldSpace() const
{
  const Vector3 com = shape->GetCenterOfMass();
  const Vector3 pos = Vector3Add(position, Vector3RotateByQuaternion(com, rotation));
  return pos;
}

Vector3 Body::GetCenterOfMassModelSpace() const
{
  return shape->GetCenterOfMass();
}

Vector3 Body::WorldSpaceToLocalSpace(const Vector3 &point) const
{
  Vector3 tmp = Vector3Subtract(point, GetCenterOfMassWorldSpace());
  Quaternion inverserotation = QuaternionInvert(rotation);
  Vector3 localSpace = Vector3RotateByQuaternion(tmp, inverserotation);
  return localSpace;
}

Vector3 Body::LocalSpaceToWorldSpace(const Vector3 &point) const
{
  Vector3 worldSpace = Vector3Add(GetCenterOfMassWorldSpace(), Vector3RotateByQuaternion(point, rotation));
  return worldSpace;
}

void Body::ApplyLinearImpulse(const Vector3 &impulse)
{
  if (invertedMass == 0.0f)
  {
    return;
  }

  linearVelocity = Vector3Add(linearVelocity, Vector3Scale(impulse, invertedMass));
}

// Body::~Body() { delete shape; }

void Scene::Initialize()
{
  Body body;
  body.position = Vector3{0, 100, 0};
  body.linearVelocity = Vector3{0, 0, 0};
  body.rotation = Quaternion{0, 0, 0, 1};
  body.invertedMass = 1.0f;
  body.restitutionCoefficient = 0.5;
  body.shape = new Sphere(5.0f);
  bodies.push_back(body);

  body.position = Vector3{0, -1000, 0};
  body.linearVelocity = Vector3{0, 0, 0};
  body.rotation = Quaternion{0, 0, 0, 1};
  body.invertedMass = 0.0f;
  body.restitutionCoefficient = 1.0f;
  body.shape = new Sphere(1000.0f);
  bodies.push_back(body);
}

void Scene::Update(const float deltaTime)
{
  // Substepped update loop for continuous collision detection.
  float remainingTime = deltaTime;
  const float eps = 1e-8f;
  const float minNudge = 1e-4f; // small advance to escape persistent overlap

  while (remainingTime > eps)
  {
    // Find earliest time-of-impact (TOI) within remainingTime
    float earliestTOI = remainingTime;
    CollisionPoint earliestCP;
    bool foundCollision = false;

    for (int i = 0; i < bodies.size(); i++)
    {
      for (int j = i + 1; j < bodies.size(); j++)
      {
        Body *bodyA = &bodies[i];
        Body *bodyB = &bodies[j];

        if (bodyA->invertedMass == 0.0f && bodyB->invertedMass == 0.0f)
          continue;

        CollisionPoint cp;
        if (Intersect(bodyA, bodyB, cp, remainingTime))
        {
          if (cp.impactTime < earliestTOI)
          {
            earliestTOI = cp.impactTime;
            earliestCP = cp;
            foundCollision = true;
          }
        }
      }
    }

    if (!foundCollision)
    {
      // No collision in the remaining time: advance whole interval and finish
      // Apply gravity impulse for the full remaining time and integrate positions.
      for (auto &body : bodies)
      {
        if (body.invertedMass == 0.0f)
          continue;
        float mass = 1.0f / body.invertedMass;
        Vector3 impulseGravity = Vector3Scale(gravity, mass * remainingTime);
        body.ApplyLinearImpulse(impulseGravity);
      }

      for (auto &body : bodies)
      {
        Vector3 deltaPosition = Vector3Scale(body.linearVelocity, remainingTime);
        body.position = Vector3Add(body.position, deltaPosition);
      }

      break;
    }

    // Advance to the TOI (may be zero if already overlapping)
    float toi = earliestTOI;
    if (toi > 0.0f)
    {
      // Apply gravity impulse for toi and advance bodies
      for (auto &body : bodies)
      {
        if (body.invertedMass == 0.0f)
          continue;
        float mass = 1.0f / body.invertedMass;
        Vector3 impulseGravity = Vector3Scale(gravity, mass * toi);
        body.ApplyLinearImpulse(impulseGravity);
      }

      for (auto &body : bodies)
      {
        Vector3 deltaPosition = Vector3Scale(body.linearVelocity, toi);
        body.position = Vector3Add(body.position, deltaPosition);
      }

      remainingTime -= toi;
    }
    else
    {
      // toi == 0: bodies are touching/overlapping now. We'll resolve immediately
      // without advancing time. To avoid tight loops if resolution doesn't separate
      // them, later we nudge forward by a tiny amount.
    }

    // Resolve the earliest collision at its contact state
    ResolveContact(earliestCP);

    // If TOI was zero, nudge forward a tiny bit to avoid repeated zero-time collisions
    if (toi <= 0.0f)
    {
      float nudge = fminf(minNudge, remainingTime);
      if (nudge > 0.0f)
      {
        for (auto &body : bodies)
        {
          if (body.invertedMass == 0.0f)
            continue;
          float mass = 1.0f / body.invertedMass;
          Vector3 impulseGravity = Vector3Scale(gravity, mass * nudge);
          body.ApplyLinearImpulse(impulseGravity);
        }

        for (auto &body : bodies)
        {
          Vector3 deltaPosition = Vector3Scale(body.linearVelocity, nudge);
          body.position = Vector3Add(body.position, deltaPosition);
        }

        remainingTime -= nudge;
      }
      else
      {
        // nothing left to simulate
        break;
      }
    }
  }
}


bool Intersect(Body *bodyA, Body *bodyB, CollisionPoint &collisionPoint, float deltaTime)
{
  collisionPoint.bodyA = bodyA;
  collisionPoint.bodyB = bodyB;

  // Only support sphere-sphere CCD for now
  if (bodyA->shape->GetType() == Shape::SPHERE && bodyB->shape->GetType() == Shape::SPHERE)
  {
    Sphere *sphereA = dynamic_cast<Sphere *>(bodyA->shape);
    Sphere *sphereB = dynamic_cast<Sphere *>(bodyB->shape);

    Vector3 r = Vector3Subtract(bodyB->position, bodyA->position); // relative position A->B
    Vector3 v = Vector3Subtract(bodyB->linearVelocity, bodyA->linearVelocity); // relative velocity
    float radiusSum = sphereA->radius + sphereB->radius;

    float a = Vector3DotProduct(v, v);
    float b = 2.0f * Vector3DotProduct(r, v);
    float c = Vector3DotProduct(r, r) - radiusSum * radiusSum;

    // If already overlapping now
    if (c <= 0.0f)
    {
      float len = Vector3Length(r);
      Vector3 n = len > 0.0001f ? Vector3Scale(r, 1.0f / len) : Vector3{1,0,0};
      collisionPoint.normal = n;
      collisionPoint.impactTime = 0.0f;
      collisionPoint.A_WorldSpace = Vector3Add(bodyA->position, Vector3Scale(n, sphereA->radius));
      collisionPoint.B_WorldSpace = Vector3Add(bodyB->position, Vector3Scale(n, -sphereB->radius));
      return true;
    }

    // If relative velocity is zero, they won't close distance
    if (a <= 1e-8f)
    {
      return false;
    }

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
    {
      return false;
    }

    float sqrtD = sqrtf(disc);
    float t = (-b - sqrtD) / (2.0f * a); // earliest impact time (seconds)

    if (t < 0.0f || t > deltaTime)
    {
      return false;
    }

    // Positions at impact
    Vector3 posA_impact = Vector3Add(bodyA->position, Vector3Scale(bodyA->linearVelocity, t));
    Vector3 posB_impact = Vector3Add(bodyB->position, Vector3Scale(bodyB->linearVelocity, t));

    Vector3 AtoB = Vector3Subtract(posB_impact, posA_impact);
    float len = Vector3Length(AtoB);
    Vector3 normal = len > 0.0001f ? Vector3Scale(AtoB, 1.0f / len) : Vector3{1,0,0};

    collisionPoint.normal = normal;
    collisionPoint.impactTime = t;
    collisionPoint.A_WorldSpace = Vector3Add(posA_impact, Vector3Scale(normal, sphereA->radius));
    collisionPoint.B_WorldSpace = Vector3Add(posB_impact, Vector3Scale(normal, -sphereB->radius));

    return true;
  }

  return false;
}

void ResolveContact(CollisionPoint &collisionPoint)
{
  Body *bodyA = collisionPoint.bodyA;
  Body *bodyB = collisionPoint.bodyB;
  // collision impulse
  Vector3 velocityDelta = Vector3Subtract(bodyA->linearVelocity, bodyB->linearVelocity);
  float restitutionCoefficient = bodyA->restitutionCoefficient * bodyB->restitutionCoefficient;
  float denom = bodyA->invertedMass + bodyB->invertedMass;
  if (denom == 0.0f) return; // both immovable

  float impulse = -1.0f * (1.0f + restitutionCoefficient) * Vector3DotProduct(velocityDelta, collisionPoint.normal) / denom;

  Vector3 impulseVectorBToA = Vector3Scale(collisionPoint.normal, impulse);
  Vector3 impulseVectorAToB = Vector3Negate(impulseVectorBToA);

  bodyA->ApplyLinearImpulse(impulseVectorBToA);
  bodyB->ApplyLinearImpulse(impulseVectorAToB);

  float aFractionOfTotalMass = (bodyA->invertedMass) / denom;
  float bFractionOfTotalMass = (bodyB->invertedMass) / denom;

  Vector3 AtoBWorldSpace = Vector3Subtract(collisionPoint.B_WorldSpace, collisionPoint.A_WorldSpace);
  Vector3 BtoAWorldSpace = Vector3Negate(AtoBWorldSpace);

  bodyA->position = Vector3Add(bodyA->position, Vector3Scale(AtoBWorldSpace, aFractionOfTotalMass));
  bodyB->position = Vector3Add(bodyB->position, Vector3Scale(BtoAWorldSpace, bFractionOfTotalMass));
}