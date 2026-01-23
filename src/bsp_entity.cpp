#include "bsp_entity.h"

#include <glog/logging.h>

#include <cmath>
#include <sstream>

namespace ioq3_map {

namespace {

// Helper to parse a vector3 string "x y z"
Eigen::Vector3f ParseVector3(const std::string& str) {
  std::stringstream ss(str);
  float x = 0, y = 0, z = 0;
  ss >> x >> y >> z;
  return Eigen::Vector3f(x, y, z);
}

// Helper to parse a color string "r g b" (0.0-1.0)
Eigen::Vector3f ParseColor(const std::string& str) {
  std::stringstream ss(str);
  float r = 1, g = 1, b = 1;
  ss >> r >> g >> b;
  return Eigen::Vector3f(r, g, b);
}

// Parses the raw entity lump string into a list of key-value maps
std::vector<std::unordered_map<std::string, std::string>> ParseEntityString(
    std::string_view entity_str) {
  std::vector<std::unordered_map<std::string, std::string>> entities;
  std::unordered_map<std::string, std::string> current_entity;
  std::string current_key;
  std::string current_value;

  bool in_token = false;
  bool in_quote = false;
  std::string token;

  // Simple tokenizer state machine
  for (size_t i = 0; i < entity_str.length(); ++i) {
    char c = entity_str[i];

    if (in_quote) {
      if (c == '\"') {
        in_quote = false;
        // End of token
        if (current_key.empty()) {
          current_key = token;
        } else {
          current_value = token;
          current_entity[current_key] = current_value;
          current_key.clear();
        }
        token.clear();
      } else {
        token += c;
      }
    } else {
      if (c == '\"') {
        in_quote = true;
        token.clear();
      } else if (c == '{') {
        current_entity.clear();
      } else if (c == '}') {
        if (!current_entity.empty()) {
          entities.push_back(current_entity);
          current_entity.clear();
        }
      } else if (c == '/') {
        // Check for comment //
        if (i + 1 < entity_str.length() && entity_str[i + 1] == '/') {
          // Skip until newline
          while (i < entity_str.length() && entity_str[i] != '\n') {
            i++;
          }
        }
      }
    }
  }
  return entities;
}

}  // namespace

std::vector<Entity> BuildBSPEntities(const BSP& bsp) {
  std::vector<Entity> result;
  // Convert string_view to string for easier debug/handling if needed,
  // though ParseEntityString takes view.
  std::string_view entities_lump;
  if (bsp.lumps.count(LumpType::Entities)) {
    entities_lump = bsp.lumps.at(LumpType::Entities);
  }
  auto raw_entities = ParseEntityString(entities_lump);

  // First pass: Build a map of targetname -> origin for spotlight target lookup
  std::unordered_map<std::string, Eigen::Vector3f> target_origins;
  for (const auto& ent : raw_entities) {
    auto it_name = ent.find("targetname");
    auto it_origin = ent.find("origin");
    if (it_name != ent.end() && it_origin != ent.end()) {
      target_origins[it_name->second] = ParseVector3(it_origin->second);
    }
  }

  // Second pass: Create structured entities
  for (const auto& ent : raw_entities) {
    auto it_class = ent.find("classname");
    std::string classname = (it_class != ent.end()) ? it_class->second : "";

    if (classname == "light") {
      // Common light properties
      Eigen::Vector3f origin = Eigen::Vector3f::Zero();
      if (ent.count("origin")) {
        origin = ParseVector3(ent.at("origin"));
      }

      float intensity = 300.0f;  // Default
      if (ent.count("light")) {
        intensity = std::stof(ent.at("light"));
      }
      if (ent.count("_light")) {
        intensity = std::stof(ent.at("_light"));
      }

      // Process color if present
      Eigen::Vector3f color = Eigen::Vector3f::Ones();
      if (ent.count("_color")) {
        color = ParseColor(ent.at("_color"));
      }

      auto it_target = ent.find("target");
      if (it_target != ent.end() && target_origins.count(it_target->second)) {
        // Spotlight
        SpotLightEntity spot;
        spot.origin = origin;
        spot.color = color;
        spot.intensity = intensity;

        Eigen::Vector3f target_pos = target_origins.at(it_target->second);
        spot.direction = (target_pos - origin).normalized();

        float radius = 64.0f;
        if (ent.count("radius")) radius = std::stof(ent.at("radius"));

        float dist = (target_pos - origin).norm();
        if (dist < 1.0f) dist = 1.0f;  // Avoid div by zero
        // angle = atan(radius / dist). This is half-angle.
        // The struct has 'spot_angle'. I'll store the half-angle in radians?
        // Or degrees? Usually ioq3 uses degrees in some places, radians in
        // math. Let's stick to radians for internal logic.
        spot.spot_angle = std::atan(radius / dist);

        result.push_back(Entity{spot});
      } else {
        // Point Light
        PointLightEntity point;
        point.origin = origin;
        point.color = color;
        point.intensity = intensity;
        result.push_back(Entity{point});
      }
    } else {
      // Generic Entity
      result.push_back(Entity{ent});
    }
  }

  return result;
}

}  // namespace ioq3_map
