#include "extrude.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
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

// Clearance callback reporting "nothing behind the wall", so thickness is never
// clamped. Exercises the cone-sampling path in ComputeShellThickness.
float FarClearance(const Eigen::Vector3f&, const Eigen::Vector3f&) {
  return std::numeric_limits<float>::infinity();
}

// Clearance for a unit-cube face whose neighbours sit flush along its rim: any
// ray whose origin lies on a rim plane (y or z at 0 or 1) hits a neighbour
// immediately, while an interior origin sees the opposite wall 1 m back. This is
// the shared-edge case that collapses a naive rim-vertex clearance to zero; a
// correct interior-sampled clearance must still report room behind the face.
float CubeRimClearance(const Eigen::Vector3f& o, const Eigen::Vector3f&) {
  bool on_rim = o.y() < 1e-3f || o.y() > 1.0f - 1e-3f || o.z() < 1e-3f ||
                o.z() > 1.0f - 1e-3f;
  return on_rim ? 0.0f : 1.0f;
}

// The closed shell as seen by a consumer: the front's own triangles plus the
// shell's (back cap + side walls), sharing the shell's vertex array (whose first
// N entries are a copy of the front rim).
Geometry FrontPlusShell(const Geometry& front, const Geometry& shell) {
  Geometry c;
  c.vertices = shell.vertices;
  c.indices = front.indices;  // front cap references [0, N)
  c.indices.insert(c.indices.end(), shell.indices.begin(), shell.indices.end());
  return c;
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

TEST(Extrude, DisabledReturnsNullopt) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell({.thickness = 0.0f, .inset = 0.1f}, g);
  EXPECT_FALSE(shell.has_value());
}

TEST(Extrude, ShellIsIndependentOccluder) {
  Geometry g = MakeQuad();
  Geometry before = g;
  auto shell = BuildOccluderShell({.thickness = 0.2f, .inset = 0.01f}, g);
  ASSERT_TRUE(shell.has_value());

  // The front surface is untouched (the shell is a separate Geometry).
  EXPECT_EQ(g.vertices.size(), before.vertices.size());
  EXPECT_EQ(g.indices.size(), before.indices.size());

  // Occluder-only: flagged, and carries no lightmap chart / texture UVs.
  EXPECT_TRUE(shell->occluder_only);
  EXPECT_TRUE(shell->lightmap_uvs.empty());
  EXPECT_TRUE(shell->texture_uvs.empty());

  // 4 front-rim copies + 4 back-cap vertices.
  EXPECT_EQ(shell->vertices.size(), 8u);
}

TEST(Extrude, BackCapOffsetAndInset) {
  Geometry g = MakeQuad();
  const float thickness = 0.2f;
  const float inset = 0.05f;
  auto shell = BuildOccluderShell(
      {.thickness = thickness, .inset = inset}, g, FarClearance);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  // Back vertex i corresponds to front-rim copy i, pushed to z = -thickness and
  // pulled toward the centroid (0.5, 0.5, 0).
  for (int i = 0; i < 4; ++i) {
    const Eigen::Vector3f& f = shell->vertices[i];       // front-rim copy
    const Eigen::Vector3f& b = shell->vertices[i + 4];   // back cap
    EXPECT_NEAR(b.z(), -thickness, 1e-5f);
    EXPECT_EQ(shell->normals[i + 4], -shell->normals[i]);
    float df = std::hypot(f.x() - 0.5f, f.y() - 0.5f);
    float db = std::hypot(b.x() - 0.5f, b.y() - 0.5f);
    EXPECT_LT(db, df);
    EXPECT_NEAR(df - db, inset, 1e-5f);
  }
}

TEST(Extrude, ShellPlusFrontIsWatertight) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell({.thickness = 0.2f, .inset = 0.01f}, g);
  ASSERT_TRUE(shell.has_value());
  // Front cap + back cap + side walls together close the volume: every edge is
  // shared by exactly two triangles.
  for (const auto& [edge, count] : EdgeUseCounts(FrontPlusShell(g, *shell))) {
    EXPECT_EQ(count, 2) << "edge (" << edge.first << "," << edge.second << ")";
  }
}

TEST(Extrude, BackCapWindingFacesAway) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell({.thickness = 0.2f, .inset = 0.0f}, g);
  ASSERT_TRUE(shell.has_value());
  // The shell's index list begins with the back-cap triangles; their geometric
  // normal (by winding) must point along -Z, away from the front.
  ASSERT_GE(shell->indices.size(), 6u);
  Eigen::Vector3f a = shell->vertices[shell->indices[0]];
  Eigen::Vector3f b = shell->vertices[shell->indices[1]];
  Eigen::Vector3f c = shell->vertices[shell->indices[2]];
  Eigen::Vector3f n = (b - a).cross(c - a);
  EXPECT_LT(n.z(), 0.0f);
}

// Outward-facing cube: the +X face has normal +X, so the shell extrudes inward
// (toward the cube centre). Even though every rim ray hits a neighbour flush
// (CubeRimClearance), interior sampling still finds the 1 m of room behind the
// face, so the shell extrudes the FULL thickness — never zero — and its side
// walls stay strictly inside the neighbouring planes (no z-fighting).
TEST(Extrude, OutwardCubeFaceExtrudesInward) {
  const float thickness = 0.2f;
  const float inset = 0.05f;
  Geometry px;
  px.vertices = {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}};
  px.normals = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
  px.indices = {0, 1, 2, 0, 2, 3};  // CCW from +X
  auto shell = BuildOccluderShell(
      {.thickness = thickness, .inset = inset}, px, CubeRimClearance);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = shell->vertices[i];
    // Real (non-zero) extrusion off x=1, at the full thickness.
    EXPECT_NEAR(b.x(), 1.0f - thickness, 1e-4f);
    // Pulled strictly inside the y=0/1 and z=0/1 planes (inset, no z-fighting).
    EXPECT_GT(b.y(), 1e-4f);
    EXPECT_LT(b.y(), 1.0f - 1e-4f);
    EXPECT_GT(b.z(), 1e-4f);
    EXPECT_LT(b.z(), 1.0f - 1e-4f);
  }
}

// Inward-facing cube (a room): the +X wall at x=1 has normal -X (pointing into
// the room), with winding reversed to match. The shell must extrude the OTHER
// way — outward, into the solid behind the wall (x > 1). As with the outward
// case, rim rays hit flush neighbours but interior sampling keeps the extrusion
// at full thickness rather than collapsing to zero.
TEST(Extrude, InwardRoomFaceExtrudesOutward) {
  const float thickness = 0.2f;
  const float inset = 0.05f;
  Geometry rx;
  rx.vertices = {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}};
  rx.normals = {{-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}};
  rx.indices = {0, 2, 1, 0, 3, 2};  // CCW from -X (geometric normal = -X)
  auto shell = BuildOccluderShell(
      {.thickness = thickness, .inset = inset}, rx, CubeRimClearance);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = shell->vertices[i];
    // Extruded outward (behind the wall), not into the room — full thickness.
    EXPECT_NEAR(b.x(), 1.0f + thickness, 1e-4f);
    // Rim still pulled into the face interior by the inset.
    EXPECT_GT(b.y(), 1e-4f);
    EXPECT_LT(b.y(), 1.0f - 1e-4f);
    EXPECT_GT(b.z(), 1e-4f);
    EXPECT_LT(b.z(), 1.0f - 1e-4f);
  }
}

// A long, thin strip is the case a centroid pull handles badly: at a far corner
// the direction to the centroid runs almost parallel to the long edges, so the
// back rim barely clears them. The per-edge inward inset clears every boundary
// edge perpendicularly, so each back vertex sits a real margin inside the strip.
TEST(Extrude, ElongatedStripClearsLongEdges) {
  const float thickness = 0.2f;
  const float inset = 0.05f;
  Geometry g;
  g.vertices = {{0, 0, 0}, {10, 0, 0}, {10, 0.1f, 0}, {0, 0.1f, 0}};
  g.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
  g.indices = {0, 1, 2, 0, 2, 3};
  auto shell = BuildOccluderShell(
      {.thickness = thickness, .inset = inset}, g, FarClearance);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);
  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = shell->vertices[i];
    EXPECT_GT(b.y(), 0.02f) << "back vertex " << i << " hugs the y=0 plane";
    EXPECT_LT(b.y(), 0.08f) << "back vertex " << i << " hugs the y=0.1 plane";
  }
}

// Spatial awareness: with a surface behind the wall, the thickness is clamped so
// the back cap stops `clearance_margin` short of it, rather than reaching the
// full requested thickness.
TEST(Extrude, ClearanceMarginClampsThickness) {
  Geometry g = MakeQuad();  // z=0, normal +Z; backward is -Z.
  const float thickness = 0.2f;
  const float margin = 0.01f;
  const float d_back = 0.1f;  // a wall 0.1 m behind, reported for every ray.
  auto near_wall = [d_back](const Eigen::Vector3f&, const Eigen::Vector3f&) {
    return d_back;
  };
  auto shell = BuildOccluderShell(
      {.thickness = thickness, .inset = 0.0f, .clearance_margin = margin}, g,
      near_wall);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);
  // t = min(thickness, d_back - margin) = 0.09, so the back cap sits at z=-0.09.
  for (int i = 4; i < 8; ++i) {
    EXPECT_NEAR(shell->vertices[i].z(), -(d_back - margin), 1e-4f);
  }
}

// When there is essentially no room behind the wall (clearance <= margin), the
// shell is NOT dropped — it is floored at min_thickness so shelling still
// happens. A degenerate zero-thickness shell would defeat the purpose.
TEST(Extrude, TightSpaceExtrudesMinThickness) {
  Geometry g = MakeQuad();  // z=0, normal +Z; backward is -Z.
  const float min_thickness = 0.02f;
  auto flush = [](const Eigen::Vector3f&, const Eigen::Vector3f&) {
    return 0.005f;  // 5 mm behind, < 10 mm margin => room would be negative.
  };
  auto shell = BuildOccluderShell({.thickness = 0.2f,
                                   .inset = 0.0f,
                                   .clearance_margin = 0.01f,
                                   .min_thickness = min_thickness},
                                  g, flush);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);
  for (int i = 4; i < 8; ++i) {
    EXPECT_NEAR(shell->vertices[i].z(), -min_thickness, 1e-4f);
  }
}

}  // namespace
}  // namespace ioq3_map
