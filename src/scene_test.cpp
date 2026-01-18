#include "scene.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bsp_geometry.h"

namespace ioq3_map {
namespace {

using ::testing::Pointwise;

// Helper to match vectors with tolerance
MATCHER_P(VectorNear, tol, "") {
  return (std::get<0>(arg) - std::get<1>(arg)).norm() < tol;
}

class SceneTest : public ::testing::Test {
 protected:
  BSP bsp_;
  std::unordered_map<BSPSurfaceIndex, BSPGeometry> geometries_;
  std::unordered_map<BSPTextureIndex, BSPMaterial> materials_;
};

TEST_F(SceneTest, AssembleBSPObjectsPlanarTransform) {
  // Setup a simple Polygon
  // Q3 Coords: triangle at Z=0
  // v0=(0,0,0), v1=(100,0,0), v2=(0,100,0) normal=(0,0,1)
  BSPGeometry geo;
  geo.texture_index = 0;
  BSPPolygon poly;
  vertex_t v0;
  v0.xyz = {0, 0, 0};
  v0.normal = {0, 0, 1};
  vertex_t v1;
  v1.xyz = {100, 0, 0};
  v1.normal = {0, 0, 1};
  vertex_t v2;
  v2.xyz = {0, 100, 0};
  v2.normal = {0, 0, 1};
  poly.vertices = {v0, v1, v2};
  poly.indices = {0, 1, 2};
  geo.primitive = poly;
  geometries_[0] = geo;

  // Setup Material
  BSPMaterial mat;
  mat.name = "textures/base_wall/concrete";
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_);

  ASSERT_EQ(scene.geometries.size(), 1);
  const auto& out_geo = scene.geometries.at(0);

  // Verify transform: x'=x, y'=z, z'=-y
  // v0': (0, 0, 0)
  // v1': (100*scale, 0, 0) -> (2.54, 0, 0)
  // v2': (0, 0, -100*scale) -> (0, 0, -2.54)
  // normal': (0, 1, 0)

  constexpr float kScale = 0.0254f;
  EXPECT_NEAR(out_geo.vertices[0].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[0].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[0].z(), 0.0f, 1e-5f);

  EXPECT_NEAR(out_geo.vertices[1].x(), 100 * kScale, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[1].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[1].z(), 0.0f, 1e-5f);

  EXPECT_NEAR(out_geo.vertices[2].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[2].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[2].z(), -100 * kScale, 1e-5f);  // z' = -y

  // Normal (0,0,1) -> (0,1,0)
  EXPECT_NEAR(out_geo.normals[0].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].y(), 1.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].z(), 0.0f, 1e-5f);

  // Material Check
  ASSERT_EQ(scene.materials.size(), 1);
  EXPECT_EQ(scene.materials.at(0).name, "textures/base_wall/concrete");
  EXPECT_EQ(out_geo.material_id, 0);
}

TEST_F(SceneTest, AssembleBSPObjectsExtractsSun) {
  BSPMaterial mat;
  mat.name = "textures/skies/sky_sun";
  mat.q3map_sun_intensity = 100.0f;
  mat.q3map_sun_color = Eigen::Vector3f(1.0f, 1.0f, 1.0f);
  mat.q3map_sun_direction = Eigen::Vector2f(90.0f, 45.0f);  // North, 45deg up
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_);

  bool found_sun = false;
  for (const auto& l : scene.lights) {
    if (l.type == Light::Type::Directional) {
      found_sun = true;
      EXPECT_FLOAT_EQ(l.intensity, 100.0f);
      // Verify direction
      // Yaw 90 (North), El 45
      // Q3: x=0, y=r, z=sin(45)
      // r = cos(45) = 0.707
      // y = 0.707 * sin(90) = 0.707
      // z = 0.707
      // Q3 Sun Pos: (0, 0.707, 0.707)
      // Q3 Light Dir: (0, -0.707, -0.707)
      // Transform (Rot -90 X):
      // x'=x=0
      // y'=z=-0.707
      // z'=-y=0.707
      EXPECT_NEAR(l.direction.x(), 0.0f, 1e-3f);
      EXPECT_NEAR(l.direction.y(), -0.707f, 1e-3f);
      EXPECT_NEAR(l.direction.z(), 0.707f, 1e-3f);
    }
  }
  EXPECT_TRUE(found_sun);
}

}  // namespace
}  // namespace ioq3_map
