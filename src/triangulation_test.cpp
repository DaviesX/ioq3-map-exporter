#include "triangulation.h"

#include <gtest/gtest.h>

#include "bsp_geometry.h"

namespace ioq3_map {
namespace {

TEST(TriangulationTest, TriangulateSquare) {
  BSPPolygon poly;
  // Make a square: 4 vertices
  // 0 -- 1
  // |    |
  // 3 -- 2
  poly.vertices.resize(4);
  poly.vertices[0].xyz = {0, 0, 0};
  poly.vertices[1].xyz = {1, 0, 0};
  poly.vertices[2].xyz = {1, 1, 0};
  poly.vertices[3].xyz = {0, 1, 0};

  BSPMesh mesh = Triangulate(poly);

  EXPECT_EQ(mesh.vertices.size(), 4);
  // Expect 2 triangles -> 6 indices
  EXPECT_EQ(mesh.indices.size(), 6);

  // Triangle 1: 0, 1, 2
  EXPECT_EQ(mesh.indices[0], 0);
  EXPECT_EQ(mesh.indices[1], 1);
  EXPECT_EQ(mesh.indices[2], 2);

  // Triangle 2: 0, 2, 3
  EXPECT_EQ(mesh.indices[3], 0);
  EXPECT_EQ(mesh.indices[4], 2);
  EXPECT_EQ(mesh.indices[5], 3);
}

TEST(TriangulationTest, TriangulateTriangle) {
  BSPPolygon poly;
  poly.vertices.resize(3);
  BSPMesh mesh = Triangulate(poly);

  EXPECT_EQ(mesh.vertices.size(), 3);
  EXPECT_EQ(mesh.indices.size(), 3);
  EXPECT_EQ(mesh.indices[0], 0);
  EXPECT_EQ(mesh.indices[1], 1);
  EXPECT_EQ(mesh.indices[2], 2);
}

TEST(TriangulationTest, NotEnoughVertices) {
  BSPPolygon poly;
  poly.vertices.resize(2);
  BSPMesh mesh = Triangulate(poly);

  EXPECT_EQ(mesh.vertices.size(), 2);
  EXPECT_EQ(mesh.indices.size(), 0);
}

// Helper to create a flat 3x3 patch on XY plane
BSPPatch CreateFlatPatch3x3() {
  BSPPatch patch;
  patch.width = 3;
  patch.height = 3;
  patch.control_points.resize(9);

  // 0 1 2
  // 3 4 5
  // 6 7 8
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      int idx = y * 3 + x;
      patch.control_points[idx].xyz = Eigen::Vector3f(x, y, 0);
      patch.control_points[idx].st = Eigen::Vector2f(x / 2.0f, y / 2.0f);
      patch.control_points[idx].normal = Eigen::Vector3f(0, 0, 1);
    }
  }
  return patch;
}

TEST(TriangulationTest, TriangulatePatchFlat) {
  BSPPatch patch = CreateFlatPatch3x3();
  // 2 subdivisions -> 3x3 grid of vertices per 3x3 control grid (since it's 1
  // sub-patch) Grid width = 1 * 2 + 1 = 3
  BSPMesh mesh = Triangulate(patch, 2);

  int expected_grid_width = 3;
  int expected_grid_height = 3;
  EXPECT_EQ(mesh.vertices.size(), expected_grid_width * expected_grid_height);

  // Indices: (width-1)*(height-1)*2*3 = 2 * 2 * 6 = 24 indices
  EXPECT_EQ(mesh.indices.size(), 24);

  // Check middle vertex (should be at 1.0, 1.0, 0.0)
  // It's at index 3*1 + 1 = 4
  EXPECT_FLOAT_EQ(mesh.vertices[4].xyz.x(), 1.0f);
  EXPECT_FLOAT_EQ(mesh.vertices[4].xyz.y(), 1.0f);
  EXPECT_FLOAT_EQ(mesh.vertices[4].xyz.z(), 0.0f);
}

TEST(TriangulationTest, TriangulatePatchCurve) {
  BSPPatch patch = CreateFlatPatch3x3();
  // Lift the center point (index 4)
  patch.control_points[4].xyz.z() = 2.0f;

  // 2 subdivisions
  BSPMesh mesh = Triangulate(patch, 2);

  // Midpoint of quadratic bezier p0(0), p1(2), p2(0) => 0.25*0 + 0.5*2 + 0.25*0
  // = 1.0 (Row interpolation) Then vertical interpolation of 0, 1, 0 => 0.5*1 =
  // 0.5
  EXPECT_NEAR(mesh.vertices[4].xyz.z(), 0.5f, 1e-5);
}

TEST(TriangulationTest, TriangulatePatchGrid) {
  // 5x3 patch (2 sub-patches wide, 1 high)
  BSPPatch patch;
  patch.width = 5;
  patch.height = 3;
  patch.control_points.resize(15);
  // Fill with dummy data
  for (auto& v : patch.control_points) {
    v.xyz = Eigen::Vector3f::Zero();
  }

  // 2 subdivisions
  // Grid Width = 2 * 2 + 1 = 5
  // Grid Height = 1 * 2 + 1 = 3
  BSPMesh mesh = Triangulate(patch, 2);

  EXPECT_EQ(mesh.vertices.size(), 15);
}

TEST(TriangulationTest, InvalidPatch) {
  BSPPatch patch;
  patch.width = 2;  // Even width invalid
  patch.height = 3;
  BSPMesh mesh = Triangulate(patch);
  EXPECT_TRUE(mesh.vertices.empty());

  patch.width = 3;
  patch.height = 2;  // Even height invalid
  mesh = Triangulate(patch);
  EXPECT_TRUE(mesh.vertices.empty());
}

}  // namespace
}  // namespace ioq3_map
