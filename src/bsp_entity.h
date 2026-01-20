#ifndef IOQ3_MAP_BSP_ENTITY_H_
#define IOQ3_MAP_BSP_ENTITY_H_

#include <Eigen/Dense>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "bsp.h"

namespace ioq3_map {

struct SpotLightEntity {
  Eigen::Vector3f origin;
  Eigen::Vector3f direction;
  Eigen::Vector3f color;
  float intensity;
  float spot_angle;
};

struct PointLightEntity {
  Eigen::Vector3f origin;
  Eigen::Vector3f color;
  float intensity;
};

struct Entity {
  std::variant<std::unordered_map<std::string, std::string>, SpotLightEntity,
               PointLightEntity>
      data;
};

// Parses the entity lump string into a list of Entity objects.
std::vector<Entity> BuildBSPEntities(const BSP& bsp);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_BSP_ENTITY_H_
