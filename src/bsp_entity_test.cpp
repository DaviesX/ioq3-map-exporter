#include "bsp_entity.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ioq3_map {
namespace {

using ::testing::Eq;
using ::testing::VariantWith;

TEST(BSPEntityTest, ParseGenericEntities) {
  BSP bsp;
  std::string data = R"(
{
"classname" "worldspawn"
"message" "Hello"
}
{
"classname" "misc_model"
"origin" "10 20 30"
}
)";
  bsp.buffer = data;
  bsp.lumps[LumpType::Entities] = bsp.buffer;

  auto entities = BuildBSPEntities(bsp);
  ASSERT_EQ(entities.size(), 2);

  // Check first entity (Map)
  ASSERT_TRUE(
      (std::holds_alternative<std::unordered_map<std::string, std::string>>(
          entities[0].data)));
  const auto& map0 =
      std::get<std::unordered_map<std::string, std::string>>(entities[0].data);
  EXPECT_EQ(map0.at("classname"), "worldspawn");
  EXPECT_EQ(map0.at("message"), "Hello");

  // Check second entity (Map)
  ASSERT_TRUE(
      (std::holds_alternative<std::unordered_map<std::string, std::string>>(
          entities[1].data)));
  const auto& map1 =
      std::get<std::unordered_map<std::string, std::string>>(entities[1].data);
  EXPECT_EQ(map1.at("classname"), "misc_model");
  EXPECT_EQ(map1.at("origin"), "10 20 30");
}

TEST(BSPEntityTest, ParsePointLight) {
  BSP bsp;
  std::string data = R"(
{
"classname" "light"
"origin" "100 200 -50"
"light" "400"
"_color" "1.0 0.5 0.0"
}
)";
  bsp.buffer = data;
  bsp.lumps[LumpType::Entities] = bsp.buffer;

  auto entities = BuildBSPEntities(bsp);
  ASSERT_EQ(entities.size(), 1);

  ASSERT_TRUE((std::holds_alternative<PointLightEntity>(entities[0].data)));
  const auto& light = std::get<PointLightEntity>(entities[0].data);

  EXPECT_EQ(light.origin, Eigen::Vector3f(100, 200, -50));
  EXPECT_EQ(light.intensity, 400.0f);
  EXPECT_EQ(light.color, Eigen::Vector3f(1.0f, 0.5f, 0.0f));
}

TEST(BSPEntityTest, ParseSpotLight) {
  BSP bsp;
  // Two entities: The spotlight and its target
  std::string data = R"(
{
"classname" "light"
"origin" "0 0 100"
"target" "t1"
"radius" "100"
}
{
"classname" "target_position"
"targetname" "t1"
"origin" "0 0 0"
}
)";
  bsp.buffer = data;
  bsp.lumps[LumpType::Entities] = bsp.buffer;

  auto entities = BuildBSPEntities(bsp);
  ASSERT_EQ(entities.size(), 2);

  // The light should be parsed as SpotLightEntity
  ASSERT_TRUE((std::holds_alternative<SpotLightEntity>(entities[0].data)));
  const auto& spot = std::get<SpotLightEntity>(entities[0].data);

  EXPECT_EQ(spot.origin, Eigen::Vector3f(0, 0, 100));
  // Direction from 0,0,100 to 0,0,0 is (0,0,-1)
  EXPECT_TRUE(spot.direction.isApprox(Eigen::Vector3f(0, 0, -1)));

  // Angle check: tan(theta) = radius / dist = 100 / 100 = 1. theta = 45 deg =
  // 0.785 rad
  EXPECT_NEAR(spot.spot_angle, 0.785398f, 0.001f);

  // The target entity is generic
  ASSERT_TRUE(
      (std::holds_alternative<std::unordered_map<std::string, std::string>>(
          entities[1].data)));
}

}  // namespace
}  // namespace ioq3_map
