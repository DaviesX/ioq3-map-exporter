#include "bsp_entity.h"

#include <gtest/gtest.h>

namespace ioq3_map {
namespace {

TEST(BSPEntityTest, ParseEmpty) {
  std::string data = "";
  auto entities = ParseBSPEntities(data);
  EXPECT_TRUE(entities.empty());
}

TEST(BSPEntityTest, ParseSingleEntity) {
  std::string data = R"(
{
"classname" "worldspawn"
"message" "Welcome to Q3DM1"
}
)";
  auto entities = ParseBSPEntities(data);
  ASSERT_EQ(entities.size(), 1);
  // EXPECT_EQ(entities[0].at("classname"), "worldspawn");
  // EXPECT_EQ(entities[0].at("message"), "Welcome to Q3DM1");
}

TEST(BSPEntityTest, ParseMultipleEntities) {
  std::string data = R"(
{
"classname" "worldspawn"
}
{
"classname" "info_player_deathmatch"
"origin" "100 100 100"
}
)";
  auto entities = ParseBSPEntities(data);
  ASSERT_EQ(entities.size(), 2);
  // EXPECT_EQ(entities[0].at("classname"), "worldspawn");
  // EXPECT_EQ(entities[1].at("classname"), "info_player_deathmatch");
  // EXPECT_EQ(entities[1].at("origin"), "100 100 100");
}

TEST(BSPEntityTest, ParseWithComments) {
  std::string data = R"(
// This is a comment
{
"classname" "worldspawn" // Inline comment
"_color" "1 0 0"
}
// Another comment
)";
  auto entities = ParseBSPEntities(data);
  ASSERT_EQ(entities.size(), 1);
  // EXPECT_EQ(entities[0].at("classname"), "worldspawn");
  // EXPECT_EQ(entities[0].at("_color"), "1 0 0");
}

TEST(BSPEntityTest, ParseEscapedQuotes) {
  // Though rare, let's ensure we don't break on escaped quotes if logic
  // supports it
  std::string data = R"(
{
"classname" "info"
"message" "Say \"hello\""
}
)";
  auto entities = ParseBSPEntities(data);
  ASSERT_EQ(entities.size(), 1);
  // EXPECT_EQ(entities[0].at("message"), "Say \"hello\"");
}

}  // namespace
}  // namespace ioq3_map
