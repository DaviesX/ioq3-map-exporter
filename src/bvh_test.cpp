#include "bvh.h"

#include <gtest/gtest.h>

#include <cmath>

#include "scene.h"

namespace ioq3_map {
namespace {

// Unit quad in the z=0 plane.
Geometry QuadZ0() {
  Geometry g;
  g.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  g.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
  g.indices = {0, 1, 2, 0, 2, 3};
  return g;
}

TEST(SceneBVH, NearestHitReturnsWorldDistance) {
  Geometry q = QuadZ0();
  SceneBVH bvh({&q});
  ASSERT_TRUE(bvh.valid());
  float d = bvh.NearestHit({0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f});
  EXPECT_NEAR(d, 1.0f, 1e-4f);
}

TEST(SceneBVH, MissReturnsInfinity) {
  Geometry q = QuadZ0();
  SceneBVH bvh({&q});
  // Ray parallel to the quad, offset in +z: never intersects.
  float d = bvh.NearestHit({5.0f, 5.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
  EXPECT_TRUE(std::isinf(d));
}

TEST(SceneBVH, NonUnitDirectionScaledToWorldDistance) {
  Geometry q = QuadZ0();
  SceneBVH bvh({&q});
  // Direction of length 2; the hit is still at world distance 1.
  float d = bvh.NearestHit({0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, -2.0f});
  EXPECT_NEAR(d, 1.0f, 1e-4f);
}

TEST(SceneBVH, TnearSkipsHitsInFront) {
  Geometry q = QuadZ0();
  SceneBVH bvh({&q});
  // tnear past the hit at distance 1 => nothing found.
  float d = bvh.NearestHit({0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f},
                           /*tnear=*/2.0f);
  EXPECT_TRUE(std::isinf(d));
}

TEST(SceneBVH, TransformBakedIntoWorldSpace) {
  Geometry q = QuadZ0();
  q.transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, 10.0f));
  SceneBVH bvh({&q});
  // The quad now lives at z=10; a ray from z=12 downward hits at distance 2.
  float d = bvh.NearestHit({0.5f, 0.5f, 12.0f}, {0.0f, 0.0f, -1.0f});
  EXPECT_NEAR(d, 2.0f, 1e-4f);
}

}  // namespace
}  // namespace ioq3_map
