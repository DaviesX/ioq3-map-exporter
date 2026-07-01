#include "extrude.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "bvh.h"
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

// A quad from four corners, with a per-vertex normal and explicit winding.
Geometry Quad(const std::array<Eigen::Vector3f, 4>& c, const Eigen::Vector3f& n,
              const std::array<uint32_t, 6>& idx) {
  Geometry g;
  g.vertices = {c[0], c[1], c[2], c[3]};
  g.normals = {n, n, n, n};
  g.indices = {idx[0], idx[1], idx[2], idx[3], idx[4], idx[5]};
  return g;
}

// Depth-only clearance: a wall `d` straight behind the face (the ray going along
// -Z, i.e. dir.z < 0), and a miss for any sideways (conform) ray, so it exercises
// only the depth pass.
ClearanceFn DepthWall(float d) {
  return [d](const Eigen::Vector3f&, const Eigen::Vector3f& dir) -> ClearanceHit {
    if (dir.z() < -0.5f) return {d, Eigen::Vector3f(0, 0, 1)};
    return {};  // miss (distance = +inf)
  };
}

// Wraps a SceneBVH as a ClearanceFn.
ClearanceFn WrapBVH(const SceneBVH& bvh) {
  return [&bvh](const Eigen::Vector3f& o, const Eigen::Vector3f& d) -> ClearanceHit {
    RayHit h = bvh.Cast(o, d);
    return {h.distance, h.normal};
  };
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
  auto shell = BuildOccluderShell({.thickness = 0.0f}, g);
  EXPECT_FALSE(shell.has_value());
}

TEST(Extrude, ShellIsIndependentOccluder) {
  Geometry g = MakeQuad();
  Geometry before = g;
  auto shell = BuildOccluderShell({.thickness = 0.2f}, g);
  ASSERT_TRUE(shell.has_value());

  EXPECT_EQ(g.vertices.size(), before.vertices.size());
  EXPECT_EQ(g.indices.size(), before.indices.size());

  EXPECT_TRUE(shell->occluder_only);
  EXPECT_TRUE(shell->lightmap_uvs.empty());
  EXPECT_TRUE(shell->texture_uvs.empty());
  EXPECT_EQ(shell->vertices.size(), 8u);
}

// No clearance -> full thickness, straight back along -normal (no inset).
TEST(Extrude, BackCapOffset) {
  Geometry g = MakeQuad();
  const float thickness = 0.2f;
  auto shell = BuildOccluderShell({.thickness = thickness}, g);
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  for (int i = 0; i < 4; ++i) {
    const Eigen::Vector3f& f = shell->vertices[i];       // front-rim copy
    const Eigen::Vector3f& b = shell->vertices[i + 4];   // back cap
    EXPECT_NEAR(b.z(), -thickness, 1e-5f);
    EXPECT_EQ(shell->normals[i + 4], -shell->normals[i]);
    EXPECT_NEAR(b.x(), f.x(), 1e-6f);  // straight extrusion: x/y unchanged
    EXPECT_NEAR(b.y(), f.y(), 1e-6f);
  }
}

TEST(Extrude, ShellPlusFrontIsWatertight) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell({.thickness = 0.2f}, g);
  ASSERT_TRUE(shell.has_value());
  for (const auto& [edge, count] : EdgeUseCounts(FrontPlusShell(g, *shell))) {
    EXPECT_EQ(count, 2) << "edge (" << edge.first << "," << edge.second << ")";
  }
}

TEST(Extrude, BackCapWindingFacesAway) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell({.thickness = 0.2f}, g);
  ASSERT_TRUE(shell.has_value());
  ASSERT_GE(shell->indices.size(), 6u);
  Eigen::Vector3f a = shell->vertices[shell->indices[0]];
  Eigen::Vector3f b = shell->vertices[shell->indices[1]];
  Eigen::Vector3f c = shell->vertices[shell->indices[2]];
  Eigen::Vector3f n = (b - a).cross(c - a);
  EXPECT_LT(n.z(), 0.0f);
}

// A wall 0.1 m behind clamps the depth to leave the clearance margin.
TEST(Extrude, ClearanceMarginClampsThickness) {
  Geometry g = MakeQuad();  // z=0, normal +Z; backward is -Z.
  const float d_back = 0.1f;
  const float margin = 0.01f;
  auto shell = BuildOccluderShell(
      {.thickness = 0.2f, .clearance_margin = margin}, g, DepthWall(d_back));
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);
  for (int i = 4; i < 8; ++i) {
    EXPECT_NEAR(shell->vertices[i].z(), -(d_back - margin), 1e-4f);
  }
}

// No room behind (clearance < margin) => the surface is not shelled at all,
// rather than floored to a degenerate sliver.
TEST(Extrude, TooTightYieldsNoShell) {
  Geometry g = MakeQuad();
  auto shell = BuildOccluderShell(
      {.thickness = 0.2f, .clearance_margin = 0.01f}, g, DepthWall(0.005f));
  EXPECT_FALSE(shell.has_value());
}

// The six outward faces of the unit cube, each wound so its geometric normal
// points out of the cube. Used as an Embree occluder set.
std::vector<Geometry> UnitCubeFaces() {
  const std::array<uint32_t, 6> f = {0, 1, 2, 0, 2, 3};
  return {
      Quad({{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}}, {1, 0, 0}, f),   // +X
      Quad({{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, {-1, 0, 0}, f),  // -X
      Quad({{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}, {0, 1, 0}, f),   // +Y
      Quad({{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, {0, -1, 0}, f),  // -Y
      Quad({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, {0, 0, 1}, f),   // +Z
      Quad({{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}}, {0, 0, -1}, f),  // -Z
  };
}

// Right prism (a plain box): extruding a face inward must NOT collapse the back
// cap toward the centre. The opposite wall is 1 m back, so the depth is full and
// the inward-conform ray only ever hits walls from the inside (never entering
// from outside), so nothing is pulled in.
TEST(Extrude, RightPrismDoesNotCollapse) {
  std::vector<Geometry> faces = UnitCubeFaces();
  std::vector<const Geometry*> occluders;
  for (const auto& g : faces) occluders.push_back(&g);
  SceneBVH bvh(occluders);
  ASSERT_TRUE(bvh.valid());

  const float thickness = 0.2f;
  const Geometry& plus_x = faces[0];  // +X face, extrudes toward -X.
  auto shell =
      BuildOccluderShell({.thickness = thickness}, plus_x, WrapBVH(bvh));
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  float y_min = 1e9f, y_max = -1e9f;
  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = shell->vertices[i];
    EXPECT_NEAR(b.x(), 1.0f - thickness, 1e-3f);  // full depth, no collapse in X
    y_min = std::min(y_min, b.y());
    y_max = std::max(y_max, b.y());
  }
  // The cap keeps its spread across the face (not pulled to the centre 0.5).
  EXPECT_GT(y_max - y_min, 0.8f);
}

// The five outer faces of an axis-aligned frustum: a large base rectangle at
// z=0 (half-width 2), a smaller top at z=4 (half-width 1), and four inward-
// leaning side walls. Winding makes each geometric normal point outward.
std::vector<Geometry> FrustumFaces() {
  const Eigen::Vector3f B0(-2, -2, 0), B1(2, -2, 0), B2(2, 2, 0), B3(-2, 2, 0);
  const Eigen::Vector3f T0(-1, -1, 4), T1(1, -1, 4), T2(1, 1, 4), T3(-1, 1, 4);
  const std::array<uint32_t, 6> f = {0, 1, 2, 0, 2, 3};
  const std::array<uint32_t, 6> r = {0, 2, 1, 0, 3, 2};  // reversed
  return {
      Quad({{B0, B1, B2, B3}}, {0, 0, -1}, r),   // base, outward -Z
      Quad({{T0, T1, T2, T3}}, {0, 0, 1}, f),    // top, outward +Z
      Quad({{B1, B2, T2, T1}}, {1, 0, 0}, f),    // +X side (leans in), outward +X
      Quad({{B2, B3, T3, T2}}, {0, 1, 0}, f),    // +Y side
      Quad({{B3, B0, T0, T3}}, {-1, 0, 0}, f),   // -X side
      Quad({{B0, B1, T1, T0}}, {0, -1, 0}, f),   // -Y side
  };
}

// Trapezoidal prism (frustum): extruding the base inward (+Z) with a naive
// straight-back shell pushes the corners OUT through the inward-leaning side
// walls. The inward-conform pass must pull each poked-through corner back onto
// the slanted wall. This is the case the old algorithm failed because the base
// vertices lie on the side faces (intersection at t=0).
TEST(Extrude, TrapezoidalPrismConformsToSlant) {
  std::vector<Geometry> faces = FrustumFaces();
  std::vector<const Geometry*> occluders;
  for (const auto& g : faces) occluders.push_back(&g);
  SceneBVH bvh(occluders);
  ASSERT_TRUE(bvh.valid());

  const float thickness = 0.5f;
  const Geometry& base = faces[0];  // normal -Z, extrudes +Z into the prism.
  auto shell = BuildOccluderShell({.thickness = thickness}, base, WrapBVH(bvh));
  ASSERT_TRUE(shell.has_value());
  ASSERT_EQ(shell->vertices.size(), 8u);

  // The wall half-width at the back-cap height z = thickness is 2 - 0.25*z.
  const float wall_half_width = 2.0f - 0.25f * thickness;  // 1.875 at z=0.5
  for (int i = 4; i < 8; ++i) {
    const Eigen::Vector3f& b = shell->vertices[i];
    EXPECT_NEAR(b.z(), thickness, 1e-3f);  // extruded the full depth (top is far)
    // Conformed to (or inside) the slanted wall, not left poking out at ~2.0.
    EXPECT_LT(std::abs(b.x()), wall_half_width + 5e-3f);
    EXPECT_LT(std::abs(b.y()), wall_half_width + 5e-3f);
    // ...and it really did extrude to a corner region, not collapse to centre.
    EXPECT_GT(std::abs(b.x()), 1.5f);
    EXPECT_GT(std::abs(b.y()), 1.5f);
  }
}

}  // namespace
}  // namespace ioq3_map
