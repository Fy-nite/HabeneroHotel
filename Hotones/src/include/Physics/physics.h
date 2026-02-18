#include "geometry.h"
#include "raylib.h"
#include "raymath.h"
#include <vector>

class Body
{
public:
  Vector3 position;
  Quaternion rotation;
  Shape *shape;
  Vector3 linearVelocity;
  float invertedMass; // allows use of (relatively) infinite masses - e.g. Earth
  // The ratio between an object’s velocity before and after a collision(0 - 1)
  // 1 = perfectly elastic, <1 = inelastic collision
  // source: https://research.ncl.ac.uk/game/mastersdegree/gametechnologies/physicstutorials/5collisionresponse/Physics%20-%20Collision%20Response.pdf
  float restitutionCoefficient;

  //   ~Body();

  enum Space
  {
    MODEL_SPACE,
    WORLD_SPACE
  };
  Vector3 GetCenterOfMassWorldSpace() const;
  Vector3 GetCenterOfMassModelSpace() const;

  Vector3 WorldSpaceToLocalSpace(const Vector3 &point) const;
  Vector3 LocalSpaceToWorldSpace(const Vector3 &point) const;

  void ApplyLinearImpulse(const Vector3 &impulse);
};

class Scene
{
public:
  void Initialize();
  //   void AddBody();
  //   void RemoveBody();
  void Update(const float dt_sec);

  void SetGravity(const Vector3 &g) { gravity = g; }
  Vector3 GetGravity() const { return gravity; }

  // gravity in world-space units (m/s^2)
  Vector3 gravity = Vector3{0, -9.8f, 0};

  std::vector<Body> bodies;
};

struct CollisionPoint
{
  Vector3 A_WorldSpace;
  Vector3 B_WorldSpace;

  Vector3 A_LocalSpace;
  Vector3 B_LocalSpace;

  Vector3 normal;       // world space
  float collisionDepth; // positive = no collision, negative = collision
  float impactTime;

  Body *bodyA;
  Body *bodyB;
};

// deltaTime: time window to sweep (seconds). Returns true if a collision
// occurs within [0, deltaTime]. If true, `collisionPoint.impactTime` is
// filled with the impact time (seconds) and world-space contact points are
// set to positions at impact.
bool Intersect(Body *bodyA, Body *bodyB, CollisionPoint &collisionPoint, float deltaTime);
void ResolveContact(CollisionPoint &collisionPoint);