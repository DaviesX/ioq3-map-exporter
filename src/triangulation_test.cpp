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

}  // namespace
}  // namespace ioq3_map
