#include "bsp_material.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include "bsp.h"
#include "shader_parser.h"  // For Q3Shader

namespace ioq3_map {
namespace {

class BspMaterialTest : public ::testing::Test {
 protected:
  void SetUp() override {
    //
  }

  void SetShaderLump(BSP& bsp, const std::vector<dshader_t>& shaders) {
    size_t size = shaders.size() * sizeof(dshader_t);
    // Ensure buffer has space and append.
    // Note: Reallocation invalidates existing views,
    // so in a real scenario we'd reserve or be careful.
    // Here we assume we only set one lump for the test.
    size_t offset = bsp.buffer.size();
    bsp.buffer.resize(offset + size);
    std::memcpy(bsp.buffer.data() + offset, shaders.data(), size);

    // LumpType::Textures corresponds to Lump 1 (Shaders) in Q3
    bsp.lumps[LumpType::Textures] =
        std::string_view(bsp.buffer.data() + offset, size);
  }
};

TEST_F(BspMaterialTest, BuildBSPMaterialsMatchesParsed) {
  BSP bsp;
  dshader_t ds1;
  std::memset(&ds1, 0, sizeof(ds1));
  std::strcpy(ds1.shader, "textures/common/test_shader");
  ds1.surface_flags = 123;
  ds1.content_flags = 456;

  SetShaderLump(bsp, {ds1});

  std::unordered_map<Q3ShaderName, Q3Shader> parsed;
  Q3Shader q3s;
  q3s.name = "textures/common/test_shader";
  q3s.q3map_surfacelight = 100.0f;
  parsed[q3s.name] = q3s;

  VirtualFilesystem vfs("dummy_mount");
  auto materials = BuildBSPMaterials(bsp, parsed, vfs);

  ASSERT_EQ(materials.size(), 1);
  const auto& mat = materials[0];
  EXPECT_EQ(mat.name, "textures/common/test_shader");
  EXPECT_EQ(mat.surface_flags, 123);                // From Lump
  EXPECT_EQ(mat.content_flags, 456);                // From Lump
  EXPECT_FLOAT_EQ(mat.q3map_surfacelight, 100.0f);  // From Parsed
}

TEST_F(BspMaterialTest, BuildBSPMaterialsDefaultWhenMissing) {
  BSP bsp;
  dshader_t ds1;
  std::memset(&ds1, 0, sizeof(ds1));
  std::strcpy(ds1.shader, "textures/common/unknown");
  ds1.surface_flags = 1;
  SetShaderLump(bsp, {ds1});

  std::unordered_map<Q3ShaderName, Q3Shader> parsed;  // Empty

  VirtualFilesystem vfs("dummy_mount");
  auto materials = BuildBSPMaterials(bsp, parsed, vfs);

  ASSERT_EQ(materials.size(), 1);
  const auto& mat = materials[0];
  EXPECT_EQ(mat.name, "textures/common/unknown");
  EXPECT_EQ(mat.surface_flags, 1);
  EXPECT_FLOAT_EQ(mat.q3map_surfacelight, 0.0f);  // Default
}

}  // namespace
}  // namespace ioq3_map
