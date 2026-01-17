#include "bsp_geometry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bsp.h"

namespace ioq3_map {
namespace {

using ::testing::SizeIs;

// Helper to create a string from pod
template <typename T>
std::string CreateLump(const std::vector<T>& data) {
  return std::string(reinterpret_cast<const char*>(data.data()),
                     data.size() * sizeof(T));
}

class BspGeometryTest : public ::testing::Test {
 protected:
  // We need to keep the strings alive for the duration of the test
  // because BSP uses string_view
  std::list<std::string> lump_storage_;

  void SetLump(BSP& bsp, LumpType type, std::string&& data) {
    lump_storage_.push_back(std::move(data));
    bsp.lumps[type] = lump_storage_.back();
  }
};

TEST_F(BspGeometryTest, BuildBSPGeometriesEmpty) {
  BSP bsp;
  auto result = BuildBSPGeometries(bsp);
  EXPECT_THAT(result, SizeIs(0));
}

TEST_F(BspGeometryTest, BuildBSPGeometriesSinglePlanar) {
  BSP bsp;

  // 4 Vertices
  std::vector<vertex_t> verts(4);
  // Fill some data for validation
  verts[0].xyz[0] = 0;
  verts[1].xyz[0] = 1;
  verts[2].xyz[0] = 2;
  verts[3].xyz[0] = 3;
  SetLump(bsp, LumpType::Vertexes, CreateLump(verts));

  // 6 Indices (2 triangles)
  std::vector<int> meshverts = {0, 1, 2, 0, 2, 3};
  SetLump(bsp, LumpType::MeshVerts, CreateLump(meshverts));

  // 1 Surface (Planar)
  dsurface_t face{};
  face.shader_no = 5;
  face.surface_type = static_cast<int>(MapSurfaceType::PLANAR);
  face.first_vert = 0;
  face.num_verts = 4;
  face.first_index = 0;
  face.num_indexes = 6;

  std::vector<dsurface_t> faces = {face};
  SetLump(bsp, LumpType::Faces, CreateLump(faces));

  auto result = BuildBSPGeometries(bsp);
  ASSERT_THAT(result, SizeIs(1));

  const auto& geo = result[0];
  EXPECT_EQ(geo.texture_index, 5);
  ASSERT_TRUE(std::holds_alternative<BSPPolygon>(geo.primitive));

  const auto& poly = std::get<BSPPolygon>(geo.primitive);
  EXPECT_THAT(poly.vertices, SizeIs(4));
  EXPECT_EQ(poly.vertices[1].xyz[0], 1.0f);

  EXPECT_THAT(poly.indices, SizeIs(6));
  EXPECT_EQ(poly.indices[5], 3);
}

TEST_F(BspGeometryTest, BuildBSPGeometriesMeshWithOffset) {
  BSP bsp;

  // 10 Vertices total. Surface uses 3 starting at index 5.
  std::vector<vertex_t> verts(10);
  verts[5].xyz[0] = 5.0f;
  verts[6].xyz[0] = 6.0f;
  verts[7].xyz[0] = 7.0f;
  SetLump(bsp, LumpType::Vertexes, CreateLump(verts));

  // Meshverts (Indices)
  // Indices are relative to firstVert. So 0, 1, 2 refer to verts[5], verts[6],
  // verts[7]
  std::vector<int> meshverts = {0, 1, 2};
  SetLump(bsp, LumpType::MeshVerts, CreateLump(meshverts));

  dsurface_t face{};
  face.surface_type = static_cast<int>(MapSurfaceType::TRIANGLE_SOUP);
  face.first_vert = 5;
  face.num_verts = 3;
  face.first_index = 0;
  face.num_indexes = 3;

  std::vector<dsurface_t> faces = {face};
  SetLump(bsp, LumpType::Faces, CreateLump(faces));

  auto result = BuildBSPGeometries(bsp);
  ASSERT_THAT(result, SizeIs(1));
  const auto& geo = result[0];
  ASSERT_TRUE(std::holds_alternative<BSPMesh>(geo.primitive));
  const auto& mesh = std::get<BSPMesh>(geo.primitive);

  EXPECT_THAT(mesh.vertices, SizeIs(3));
  EXPECT_EQ(mesh.vertices[0].xyz[0], 5.0f);

  EXPECT_THAT(mesh.indices, SizeIs(3));
  EXPECT_EQ(mesh.indices[0], 0);
  EXPECT_EQ(mesh.indices[2], 2);
}

TEST_F(BspGeometryTest, BuildBSPGeometriesPatch) {
  BSP bsp;

  // 9 Control points for a 3x3 patch
  std::vector<vertex_t> verts(9);
  SetLump(bsp, LumpType::Vertexes, CreateLump(verts));

  dsurface_t face{};
  face.surface_type = static_cast<int>(MapSurfaceType::PATCH);
  face.first_vert = 0;
  face.num_verts = 9;
  face.patch_width = 3;
  face.patch_height = 3;

  std::vector<dsurface_t> faces = {face};
  SetLump(bsp, LumpType::Faces, CreateLump(faces));

  auto result = BuildBSPGeometries(bsp);
  ASSERT_THAT(result, SizeIs(1));
  const auto& geo = result[0];
  ASSERT_TRUE(std::holds_alternative<BSPPatch>(geo.primitive));
  const auto& patch = std::get<BSPPatch>(geo.primitive);

  EXPECT_EQ(patch.width, 3);
  EXPECT_EQ(patch.height, 3);
  EXPECT_THAT(patch.control_points, SizeIs(9));
}

}  // namespace
}  // namespace ioq3_map
