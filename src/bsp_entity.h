#ifndef IOQ3_MAP_BSP_ENTITY_H_
#define IOQ3_MAP_BSP_ENTITY_H_

#include <Eigen/Dense>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ioq3_map {

struct SpotLightEntity {
  Eigen::Vector3f origin;
  Eigen::Vector3f direction;
  float intensity;
  float spot_angle;
};

struct PointLightEntity {
  Eigen::Vector3f origin;
  float intensity;
};

struct Entity {
  std::variant<std::unordered_map<std::string, std::string>, SpotLightEntity,
               PointLightEntity>
      data;
};

// Parses the entity lump string into a list of Entity objects.
std::vector<Entity> ParseBSPEntities(std::string_view entity_lump);

// Helper to print entities to stdout (for debugging/inspection).
void PrintBSPEntities(const std::vector<Entity>& entities);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_BSP_ENTITY_H_
