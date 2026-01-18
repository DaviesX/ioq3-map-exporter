#include "scene.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utility>
#include <variant>

#include "bsp.h"
#include "bsp_geometry.h"

namespace ioq3_map {
namespace {

using ::testing::FloatNear;
using ::testing::SizeIs;

TEST(SceneTest, AssembleBSPObjectsPlanarTransform) {
  BSP bsp;
  // We need LumpType::Textures for material resolution, but empty is fine for
  // default

  std::unordered_map<BSPSurfaceIndex, BSPGeometry> bsp_geometries;

  BSPGeometry geo;
  geo.texture_index = 0;

  BSPPolygon poly;
  // Q3 Point (100, 200, 300) -> glTF (100 * scale, 300 * scale, -200 * scale)
  // Scale = 0.0254
  // X: 100 * 0.0254 = 2.54
  // Y: 300 * 0.0254 = 7.62
  // Z: -200 * 0.0254 = -5.08
  vertex_t v0;
  v0.xyz[0] = 100.0f;
  v0.xyz[1] = 200.0f;
  v0.xyz[2] = 300.0f;
  v0.normal[0] = 0;
  v0.normal[1] = 1;
  v0.normal[2] = 0;  // Up in Q3 (Y)

  poly.vertices.push_back(v0);
  poly.indices.push_back(0);

  geo.primitive = std::move(poly);
  bsp_geometries.emplace(0, std::move(geo));

  Scene scene = AssembleBSPObjects(bsp, bsp_geometries);

  ASSERT_THAT(scene.geometries, SizeIs(1));
  const auto& out_geo = scene.geometries[0];

  ASSERT_THAT(out_geo.vertices, SizeIs(1));
  const auto& p = out_geo.vertices[0];

  EXPECT_THAT(p.x(), FloatNear(2.54f, 0.0001f));
  EXPECT_THAT(p.y(), FloatNear(7.62f, 0.0001f));   // Z maps to Y
  EXPECT_THAT(p.z(), FloatNear(-5.08f, 0.0001f));  // Y maps to -Z

  // Normal Transform: (0, 1, 0) -> (0, 0, -1)
  const auto& n = out_geo.normals[0];
  EXPECT_THAT(n.x(), FloatNear(0.0f, 0.0001f));
  EXPECT_THAT(n.y(), FloatNear(0.0f, 0.0001f));
  EXPECT_THAT(n.z(), FloatNear(-1.0f, 0.0001f));
}

TEST(SceneTest, AssembleBSPObjectsIgnoredPatch) {
  BSP bsp;
  std::unordered_map<BSPSurfaceIndex, BSPGeometry> bsp_geometries;

  BSPGeometry geo;
  geo.texture_index = 0;
  geo.primitive = BSPPatch{};  // Empty patch

  bsp_geometries.emplace(0, std::move(geo));

  Scene scene = AssembleBSPObjects(bsp, bsp_geometries);
  // Patch is processed, but empty because dimensions are invalid/zero.
  ASSERT_THAT(scene.geometries, SizeIs(1));
  EXPECT_THAT(scene.geometries[0].vertices, SizeIs(0));
}

}  // namespace
}  // namespace ioq3_map
