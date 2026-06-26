#include "merger.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace ioq3_map {
namespace {

namespace fs = std::filesystem;
using nlohmann::ordered_json;

// glTF column-major matrix for a -90 deg rotation about X (the exporter's
// Z-up -> Y-up Worldspawn convention).
const ordered_json kMinus90X = ordered_json::array(
    {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1});

// Normalized quaternion [x,y,z,w] for a rotation about X by `angle` radians.
ordered_json QuatAboutX(double angle) {
  return ordered_json::array(
      {std::sin(angle / 2), 0.0, 0.0, std::cos(angle / 2)});
}

// Compares a glTF rotation array to (x,y,z,w) up to sign (q and -q are equal).
::testing::AssertionResult QuatNear(const ordered_json& q, double x, double y,
                                    double z, double w) {
  double dot = q[0].get<double>() * x + q[1].get<double>() * y +
               q[2].get<double>() * z + q[3].get<double>() * w;
  if (std::abs(std::abs(dot) - 1.0) < 1e-5) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "quat " << q.dump() << " != [" << x << "," << y << "," << z << ","
         << w << "]";
}

// A minimal glTF document with one geometry node (0), one Worldspawn root (1)
// parenting it, and `num_lights` named light nodes also under Worldspawn, each
// rotated about X by `angle`. If `worldspawn_zup` is set, Worldspawn carries the
// -90 deg X matrix (the export-side coordinate convention).
ordered_json MakeGltf(int num_lights, double angle, bool worldspawn_zup = false) {
  ordered_json doc;
  doc["asset"] = {{"version", "2.0"}};
  doc["scene"] = 0;
  doc["scenes"] = ordered_json::array();
  doc["scenes"].push_back({{"nodes", ordered_json::array({1})}});
  doc["extensionsUsed"] = ordered_json::array({"KHR_lights_punctual"});

  ordered_json lights = ordered_json::array();
  ordered_json children = ordered_json::array({0});  // geometry node.
  doc["nodes"] = ordered_json::array();
  doc["nodes"].push_back({{"name", "Geometry"}});  // node 0
  doc["nodes"].push_back({{"name", "Worldspawn"}});  // node 1 (filled below)

  for (int i = 0; i < num_lights; ++i) {
    lights.push_back({{"type", "directional"},
                      {"name", "Light_" + std::to_string(i)},
                      {"intensity", 1000.0 + i}});
    ordered_json node;
    node["name"] = "LightNode_" + std::to_string(i);
    node["rotation"] = QuatAboutX(angle);
    node["extensions"]["KHR_lights_punctual"]["light"] = i;
    int idx = static_cast<int>(doc["nodes"].size());
    doc["nodes"].push_back(node);
    children.push_back(idx);
  }
  doc["nodes"][1]["children"] = children;
  if (worldspawn_zup) doc["nodes"][1]["matrix"] = kMinus90X;
  doc["extensions"]["KHR_lights_punctual"]["lights"] = lights;
  return doc;
}

// ---- MergeLights -----------------------------------------------------------

TEST(MergeLightsTest, UpdatesExistingLightInPlace) {
  ordered_json artist = MakeGltf(/*num_lights=*/1, /*angle=*/-0.5);
  ordered_json dest = MakeGltf(/*num_lights=*/1, /*angle=*/0.1);

  PortStats stats;
  MergeLights(artist, dest, &stats);

  EXPECT_EQ(stats.lights_updated, 1);
  EXPECT_EQ(stats.lights_added, 0);
  EXPECT_EQ(stats.lights_removed, 0);

  // Lights array replaced with the artist's intensity.
  EXPECT_DOUBLE_EQ(
      dest["extensions"]["KHR_lights_punctual"]["lights"][0]["intensity"]
          .get<double>(),
      1000.0);
  // Both parents are identity here, so the light node mirrors the artist's
  // rotation (about X by -0.5). Node 2 is the single LightNode_0.
  EXPECT_TRUE(QuatNear(dest["nodes"][2]["rotation"], std::sin(-0.25), 0.0, 0.0,
                       std::cos(-0.25)));
  EXPECT_EQ(dest["nodes"][2]["extensions"]["KHR_lights_punctual"]["light"]
                .get<int>(),
            0);
  // No nodes added or removed.
  EXPECT_EQ(dest["nodes"].size(), 3u);
}

TEST(MergeLightsTest, RebasesArtistLightIntoExportParentSpace) {
  // Artist places the light in Y-up world (identity Worldspawn); the export
  // parents lights under a -90 deg X Worldspawn. The ported local transform
  // must compensate so the light's world orientation/position is preserved.
  const double kAngle = 0.7;
  ordered_json artist = MakeGltf(/*num_lights=*/1, kAngle);
  artist["nodes"][2]["translation"] = {1.0, 2.0, 3.0};
  ordered_json dest = MakeGltf(/*num_lights=*/1, /*angle=*/0.0,
                               /*worldspawn_zup=*/true);

  PortStats stats;
  MergeLights(artist, dest, &stats);

  // Change of basis is M^-1 = Rx(+90 deg).
  // Rotation: Rx(angle) -> Rx(angle + pi/2).
  double local_angle = kAngle + M_PI / 2;
  EXPECT_TRUE(QuatNear(dest["nodes"][2]["rotation"], std::sin(local_angle / 2),
                       0.0, 0.0, std::cos(local_angle / 2)));
  // Translation: Rx(+90) maps (x,y,z) -> (x,-z,y).
  EXPECT_NEAR(dest["nodes"][2]["translation"][0].get<double>(), 1.0, 1e-6);
  EXPECT_NEAR(dest["nodes"][2]["translation"][1].get<double>(), -3.0, 1e-6);
  EXPECT_NEAR(dest["nodes"][2]["translation"][2].get<double>(), 2.0, 1e-6);
}

TEST(MergeLightsTest, AddsNewArtistLight) {
  ordered_json artist = MakeGltf(/*num_lights=*/2, /*rotation_x=*/-0.5);
  ordered_json dest = MakeGltf(/*num_lights=*/1, /*rotation_x=*/0.1);

  PortStats stats;
  MergeLights(artist, dest, &stats);

  EXPECT_EQ(stats.lights_updated, 1);
  EXPECT_EQ(stats.lights_added, 1);
  EXPECT_EQ(stats.lights_removed, 0);

  // The new light node was appended and parented under Worldspawn (node 1).
  EXPECT_EQ(dest["nodes"].size(), 4u);
  const auto& children = dest["nodes"][1]["children"];
  bool found_new = false;
  for (const auto& c : children) {
    if (c.get<int>() == 3) found_new = true;
  }
  EXPECT_TRUE(found_new);
  EXPECT_EQ(dest["nodes"][3]["name"].get<std::string>(), "LightNode_1");
  EXPECT_EQ(dest["extensions"]["KHR_lights_punctual"]["lights"].size(), 2u);
}

TEST(MergeLightsTest, RemovesStaleLightAndReindexes) {
  ordered_json artist = MakeGltf(/*num_lights=*/1, /*rotation_x=*/-0.5);
  ordered_json dest = MakeGltf(/*num_lights=*/2, /*rotation_x=*/0.1);

  PortStats stats;
  MergeLights(artist, dest, &stats);

  EXPECT_EQ(stats.lights_updated, 1);
  EXPECT_EQ(stats.lights_added, 0);
  EXPECT_EQ(stats.lights_removed, 1);

  // LightNode_1 removed; Worldspawn keeps geometry + LightNode_0.
  EXPECT_EQ(dest["nodes"].size(), 3u);
  const auto& children = dest["nodes"][1]["children"];
  EXPECT_EQ(children.size(), 2u);
  // All remaining child indices are valid.
  for (const auto& c : children) {
    EXPECT_LT(c.get<int>(), static_cast<int>(dest["nodes"].size()));
  }
  EXPECT_EQ(dest["extensions"]["KHR_lights_punctual"]["lights"].size(), 1u);
}

TEST(MergeLightsTest, AddsLightsWhenExportHasNone) {
  ordered_json artist = MakeGltf(/*num_lights=*/1, /*rotation_x=*/-0.5);
  ordered_json dest = MakeGltf(/*num_lights=*/0, /*rotation_x=*/0.0);

  PortStats stats;
  MergeLights(artist, dest, &stats);

  EXPECT_EQ(stats.lights_added, 1);
  EXPECT_EQ(dest["extensions"]["KHR_lights_punctual"]["lights"].size(), 1u);
}

TEST(MergeLightsTest, ToleratesOutOfBoundsChildIndices) {
  ordered_json artist = MakeGltf(/*num_lights=*/1, /*angle=*/-0.5);
  // Two lights so LightNode_1 is removed (exercising the node-remap path).
  ordered_json dest = MakeGltf(/*num_lights=*/2, /*angle=*/0.1);
  // Inject malformed child references on Worldspawn (node 1).
  dest["nodes"][1]["children"].push_back(9999);
  dest["nodes"][1]["children"].push_back(-1);

  PortStats stats;
  MergeLights(artist, dest, &stats);  // Must not crash / read out of bounds.

  EXPECT_EQ(stats.lights_updated, 1);
  EXPECT_EQ(stats.lights_removed, 1);
  // Every surviving child index is in range (malformed ones dropped).
  for (const auto& c : dest["nodes"][1]["children"]) {
    int idx = c.get<int>();
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, static_cast<int>(dest["nodes"].size()));
  }
}

// ---- PortTextures ----------------------------------------------------------

class PortTexturesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    base_ = fs::temp_directory_path() /
            ("merger_test_" + std::to_string(::testing::UnitTest::GetInstance()
                                               ->random_seed()));
    artist_ = base_ / "artist";
    export_ = base_ / "export";
    fs::remove_all(base_);
  }
  void TearDown() override { fs::remove_all(base_); }

  void Write(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
  }
  std::string Read(const fs::path& p) {
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)), {});
  }

  fs::path base_, artist_, export_;
};

TEST_F(PortTexturesTest, ReplacesAlbedoOrmAndNormalButNotOthers) {
  const std::string mat = "base_wall@x";
  Write(artist_ / mat / (mat + "_albedo.png"), "ARTIST_ALBEDO");
  Write(artist_ / mat / (mat + "_orm.png"), "ARTIST_ORM");
  Write(artist_ / mat / (mat + "_normal.png"), "ARTIST_NORMAL");
  Write(artist_ / mat / (mat + "_height.png"), "ARTIST_HEIGHT");

  Write(export_ / mat / (mat + "_albedo.png"), "placeholder");
  Write(export_ / mat / (mat + "_orm.png"), "placeholder");
  Write(export_ / mat / (mat + "_normal.png"), "placeholder");
  Write(export_ / mat / (mat + "_height.png"), "placeholder");

  PortConfig config;  // defaults: albedo + orm + normal.
  PortStats stats;
  ASSERT_TRUE(PortTextures(artist_, export_, config, &stats));

  EXPECT_EQ(stats.textures_replaced, 3);
  EXPECT_EQ(Read(export_ / mat / (mat + "_albedo.png")), "ARTIST_ALBEDO");
  EXPECT_EQ(Read(export_ / mat / (mat + "_orm.png")), "ARTIST_ORM");
  EXPECT_EQ(Read(export_ / mat / (mat + "_normal.png")), "ARTIST_NORMAL");
  // Maps outside the default port set (e.g. height) are left untouched.
  EXPECT_EQ(Read(export_ / mat / (mat + "_height.png")), "placeholder");
}

TEST_F(PortTexturesTest, SkipsArtistTextureWithoutExportCounterpart) {
  const std::string mat = "only_artist@x";
  Write(artist_ / mat / (mat + "_albedo.png"), "ARTIST");
  // Export has no such material folder; create it empty so it's a directory.
  fs::create_directories(export_);

  PortConfig config;
  PortStats stats;
  ASSERT_TRUE(PortTextures(artist_, export_, config, &stats));

  EXPECT_EQ(stats.textures_replaced, 0);
  EXPECT_EQ(stats.textures_missing_dest, 1);
}

}  // namespace
}  // namespace ioq3_map
