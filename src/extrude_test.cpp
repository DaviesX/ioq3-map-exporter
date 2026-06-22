#include "extrude.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "scene.h"

namespace ioq3_map {
namespace {

// A unit quad in the z=0 plane, front normal +Z, CCW from +Z. Two triangles.
Geometry MakeQuad() {
  Geometry g;
  g.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  g.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
  g.texture_uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  g.lightmap_uvs = g.texture_uvs;
  g.indices = {0, 1, 2, 0, 2, 3};
  return g;
}

// Counts how many triangles touch each undirected (position-based) edge.
std::map<std::pair<int, int>, int> EdgeUseCounts(const Geometry& g) {
  // Map identical positions to a canonical vertex id so shared rims count once.
  auto quant = [](const Eigen::Vector3f& v) {
    return std::array<long, 3>{std::lround(v.x() * 1e4f),
                               std::lround(v.y() * 1e4f),
                               std::lround(v.z() * 1e4f)};
  };
  std::map<std::array<long, 3>, int> ids;
  std::vector<int> canon(g.vertices.size());
  for (size_t i = 0; i < g.vertices.size(); ++i) {
    auto key = quant(g.vertices[i]);
    auto [it, inserted] = ids.emplace(key, static_cast<int>(ids.size()));
    canon[i] = it->second;
  }
  std::map<std::pair<int, int>, int> counts;
  for (size_t t = 0; t + 2 < g.indices.size(); t += 3) {
    int v[3] = {canon[g.indices[t]], canon[g.indices[t + 1]],
                canon[g.indices[t + 2]]};
    for (int e = 0; e < 3; ++e) {
      int a = v[e], b = v[(e + 1) % 3];
      counts[{std::min(a, b), std::max(a, b)}]++;
    }
  }
  return counts;
}

TEST(Extrude, DisabledIsNoOp) {
  Geometry g = MakeQuad();
  Geometry before = g;
  SolidifyGeometry({.thickness = 0.0f, .inset = 0.1f}, &g);
  EXPECT_EQ(g.vertices.size(), before.vertices.size());
  EXPECT_EQ(g.indices.size(), before.indices.size());
}

TEST(Extrude, FrontGeometryPreserved) {
  Geometry g = MakeQuad();
  SolidifyGeometry({.thickness = 0.2f, .inset = 0.01f}, &g);
  // The original 4 vertices and 6 indices must be untouched at the front.
  ASSERT_GE(g.vertices.size(), 8u);
  EXPECT_FLOAT_EQ(g.vertices[1].x(), 1.0f);
  EXPECT_FLOAT_EQ(g.vertices[1].z(), 0.0f);
  EXPECT_EQ(g.indices[0], 0u);
  EXPECT_EQ(g.indices[1], 1u);
  EXPECT_EQ(g.indices[2], 2u);
}

TEST(Extrude, BackCapOffsetAndInset) {
  Geometry g = MakeQuad();
  const float thickness = 0.2f;
  const float inset = 0.05f;
  SolidifyGeometry({.thickness = thickness, .inset = inset}, &g);

  ASSERT_EQ(g.vertices.size(), 8u);  // 4 front + 4 back (rim reused for sides).
  // Back vertex i corresponds to front vertex i, pushed to z = -thickness and
  // pulled toward the centroid (0.5, 0.5, 0).
  for (int i = 0; i < 4; ++i) {
    const Eigen::Vector3f& f = g.vertices[i];
    const Eigen::Vector3f& b = g.vertices[i + 4];
    EXPECT_NEAR(b.z(), -thickness, 1e-5f);
    EXPECT_EQ(g.normals[i + 4], -g.normals[i]);
    // Inset moved it toward the centroid in x/y.
    float df = std::hypot(f.x() - 0.5f, f.y() - 0.5f);
    float db = std::hypot(b.x() - 0.5f, b.y() - 0.5f);
    EXPECT_LT(db, df);
    EXPECT_NEAR(df - db, inset, 1e-5f);
  }
}

TEST(Extrude, ShellIsWatertight) {
  Geometry g = MakeQuad();
  SolidifyGeometry({.thickness = 0.2f, .inset = 0.01f}, &g);
  // A closed manifold: every edge shared by exactly two triangles.
  for (const auto& [edge, count] : EdgeUseCounts(g)) {
    EXPECT_EQ(count, 2) << "edge (" << edge.first << "," << edge.second << ")";
  }
}

TEST(Extrude, BackCapWindingFacesAway) {
  Geometry g = MakeQuad();
  SolidifyGeometry({.thickness = 0.2f, .inset = 0.0f}, &g);
  // The back-cap triangles begin right after the 6 front indices. Their
  // geometric normal (by winding) must point along -Z.
  ASSERT_GE(g.indices.size(), 12u);
  Eigen::Vector3f a = g.vertices[g.indices[6]];
  Eigen::Vector3f b = g.vertices[g.indices[7]];
  Eigen::Vector3f c = g.vertices[g.indices[8]];
  Eigen::Vector3f n = (b - a).cross(c - a);
  EXPECT_LT(n.z(), 0.0f);
}

// Six inward-extruded faces of a unit cube: assert no side wall lands coplanar
// with the parallel face (the user's z-fighting case) thanks to the inset.
TEST(Extrude, CubeFacesNoCoplanarOverlap) {
  const float thickness = 0.2f;
  const float inset = 0.05f;
  // +X face at x=1, normal +X. Build the quad in its plane, CCW from +X.
  Geometry px;
  px.vertices = {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}};
  px.normals = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
  px.texture_uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  px.lightmap_uvs = px.texture_uvs;
  px.indices = {0, 1, 2, 0, 2, 3};
  SolidifyGeometry({.thickness = thickness, .inset = inset}, &px);

  // Without the inset, the +X face's side walls would lie exactly in the y=0,
  // y=1, z=0, z=1 planes — coplanar with the cube's other faces -> z-fighting.
  // With the inset, every back vertex is pulled strictly into the interior of
  // those planes (and back off x=1 by the thickness), so no side wall is
  // coplanar with a neighbouring face.
  ASSERT_EQ(px.vertices.size(), 8u);
  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = px.vertices[i];
    EXPECT_LT(b.x(), 1.0f - 1e-4f);
    EXPECT_GT(b.y(), 1e-4f);
    EXPECT_LT(b.y(), 1.0f - 1e-4f);
    EXPECT_GT(b.z(), 1e-4f);
    EXPECT_LT(b.z(), 1.0f - 1e-4f);
  }
}

}  // namespace
}  // namespace ioq3_map
