#include "merger.h"

#include <glog/logging.h>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ioq3_map {
namespace fs = std::filesystem;
using nlohmann::ordered_json;

namespace {

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool MatchesSuffix(const std::string& name,
                   const std::vector<std::string>& suffixes) {
  for (const auto& suffix : suffixes) {
    if (EndsWith(name, suffix)) return true;
  }
  return false;
}

// Returns the indices of nodes carrying a KHR_lights_punctual extension.
std::vector<int> FindLightNodeIndices(const ordered_json& doc) {
  std::vector<int> out;
  if (!doc.contains("nodes")) return out;
  const auto& nodes = doc.at("nodes");
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    const auto& node = nodes[i];
    if (node.contains("extensions") &&
        node.at("extensions").contains("KHR_lights_punctual")) {
      out.push_back(i);
    }
  }
  return out;
}

// Finds the node that parents the existing light nodes (so new lights are
// attached to the same parent). Falls back to a node named "Worldspawn", then
// to the first root node of the active scene. Returns -1 if none can be found.
int FindLightParent(const ordered_json& doc,
                    const std::vector<int>& light_nodes) {
  const auto& nodes = doc.at("nodes");
  if (!light_nodes.empty()) {
    std::set<int> light_set(light_nodes.begin(), light_nodes.end());
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      if (!nodes[i].contains("children")) continue;
      for (const auto& child : nodes[i].at("children")) {
        if (light_set.count(child.get<int>())) return i;
      }
    }
  }
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    if (nodes[i].value("name", std::string()) == "Worldspawn") return i;
  }
  if (doc.contains("scenes") && doc.contains("scene")) {
    const auto& scene = doc.at("scenes")[doc.at("scene").get<int>()];
    if (scene.contains("nodes") && !scene.at("nodes").empty()) {
      return scene.at("nodes")[0].get<int>();
    }
  }
  return -1;
}

// Removes the given node indices from doc, rewriting every child/scene
// reference to the surviving nodes. Light nodes are leaves, so no other lump
// references them.
void RemoveNodes(ordered_json& doc, const std::set<int>& to_remove) {
  if (to_remove.empty()) return;
  const auto& nodes = doc.at("nodes");
  const int n = static_cast<int>(nodes.size());

  std::vector<int> remap(n, -1);
  ordered_json new_nodes = ordered_json::array();
  for (int i = 0; i < n; ++i) {
    if (to_remove.count(i)) continue;
    remap[i] = static_cast<int>(new_nodes.size());
    new_nodes.push_back(nodes[i]);
  }

  auto remap_list = [&](const ordered_json& list) {
    ordered_json out = ordered_json::array();
    for (const auto& v : list) {
      int mapped = remap[v.get<int>()];
      if (mapped >= 0) out.push_back(mapped);
    }
    return out;
  };

  for (auto& node : new_nodes) {
    if (node.contains("children")) {
      ordered_json children = remap_list(node.at("children"));
      if (children.empty()) {
        node.erase("children");
      } else {
        node["children"] = std::move(children);
      }
    }
  }
  doc["nodes"] = std::move(new_nodes);

  if (doc.contains("scenes")) {
    for (auto& scene : doc.at("scenes")) {
      if (scene.contains("nodes")) {
        scene["nodes"] = remap_list(scene.at("nodes"));
      }
    }
  }
}

// Ensures "KHR_lights_punctual" appears in doc["extensionsUsed"].
void EnsureExtensionUsed(ordered_json& doc) {
  if (!doc.contains("extensionsUsed")) {
    doc["extensionsUsed"] = ordered_json::array();
  }
  for (const auto& e : doc.at("extensionsUsed")) {
    if (e.get<std::string>() == "KHR_lights_punctual") return;
  }
  doc["extensionsUsed"].push_back("KHR_lights_punctual");
}

const ordered_json& LightsArrayOf(const ordered_json& doc) {
  static const ordered_json kEmpty = ordered_json::array();
  if (doc.contains("extensions") &&
      doc.at("extensions").contains("KHR_lights_punctual") &&
      doc.at("extensions").at("KHR_lights_punctual").contains("lights")) {
    return doc.at("extensions").at("KHR_lights_punctual").at("lights");
  }
  return kEmpty;
}

ordered_json LoadGltf(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    LOG(ERROR) << "Failed to open glTF: " << path;
    return ordered_json();
  }
  ordered_json doc = ordered_json::parse(in, /*cb=*/nullptr,
                                         /*allow_exceptions=*/false);
  if (doc.is_discarded()) {
    LOG(ERROR) << "Failed to parse glTF: " << path;
    return ordered_json();
  }
  return doc;
}

// --- Node transform math (change of basis between glTF documents) ------------

// Returns the node's local transform as a 4x4 matrix, honoring either the
// column-major "matrix" form or the decomposed translation/rotation/scale form.
Eigen::Matrix4d NodeLocalMatrix(const ordered_json& node) {
  if (node.contains("matrix")) {
    const auto& m = node.at("matrix");
    Eigen::Matrix4d out;
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        out(row, col) = m[col * 4 + row].get<double>();
      }
    }
    return out;
  }
  Eigen::Vector3d t = Eigen::Vector3d::Zero();
  Eigen::Vector3d s = Eigen::Vector3d::Ones();
  Eigen::Quaterniond q = Eigen::Quaterniond::Identity();
  if (node.contains("translation")) {
    const auto& a = node.at("translation");
    t = {a[0].get<double>(), a[1].get<double>(), a[2].get<double>()};
  }
  if (node.contains("rotation")) {
    const auto& a = node.at("rotation");
    q = Eigen::Quaterniond(a[3].get<double>(), a[0].get<double>(),
                           a[1].get<double>(), a[2].get<double>());
  }
  if (node.contains("scale")) {
    const auto& a = node.at("scale");
    s = {a[0].get<double>(), a[1].get<double>(), a[2].get<double>()};
  }
  Eigen::Matrix4d out = Eigen::Matrix4d::Identity();
  out.block<3, 3>(0, 0) = q.normalized().toRotationMatrix() * s.asDiagonal();
  out.block<3, 1>(0, 3) = t;
  return out;
}

// Builds a child-node-index -> parent-node-index map (-1 for roots).
std::vector<int> BuildParentMap(const ordered_json& doc) {
  int n = doc.contains("nodes") ? static_cast<int>(doc.at("nodes").size()) : 0;
  std::vector<int> parent(n, -1);
  for (int i = 0; i < n; ++i) {
    const auto& node = doc.at("nodes")[i];
    if (!node.contains("children")) continue;
    for (const auto& c : node.at("children")) parent[c.get<int>()] = i;
  }
  return parent;
}

// Accumulates the world matrix of node `idx` by chaining local matrices from the
// root down to the node.
Eigen::Matrix4d WorldMatrix(const ordered_json& doc,
                            const std::vector<int>& parent, int idx) {
  std::vector<int> chain;
  for (int i = idx; i >= 0; i = parent[i]) chain.push_back(i);
  Eigen::Matrix4d acc = Eigen::Matrix4d::Identity();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    acc = acc * NodeLocalMatrix(doc.at("nodes")[*it]);
  }
  return acc;
}

// Writes `m` onto `node` as translation/rotation(/scale), clearing any prior
// transform keys. Scale is emitted only when it deviates from unity.
void SetNodeTransform(ordered_json& node, const Eigen::Matrix4d& m) {
  Eigen::Vector3d t = m.block<3, 1>(0, 3);
  Eigen::Matrix3d lin = m.block<3, 3>(0, 0);
  Eigen::Vector3d s(lin.col(0).norm(), lin.col(1).norm(), lin.col(2).norm());
  Eigen::Matrix3d rot;
  for (int i = 0; i < 3; ++i) {
    rot.col(i) = s[i] > 1e-12 ? Eigen::Vector3d(lin.col(i) / s[i]) : lin.col(i);
  }
  if (rot.determinant() < 0) {  // strip reflection into the scale.
    rot.col(0) *= -1;
    s[0] *= -1;
  }
  Eigen::Quaterniond q(rot);
  q.normalize();

  node.erase("matrix");
  node["translation"] = {t.x(), t.y(), t.z()};
  node["rotation"] = {q.x(), q.y(), q.z(), q.w()};
  if ((s - Eigen::Vector3d::Ones()).cwiseAbs().maxCoeff() > 1e-6) {
    node["scale"] = {s.x(), s.y(), s.z()};
  } else {
    node.erase("scale");
  }
}

}  // namespace

void MergeLights(const ordered_json& artist, ordered_json& dest,
                 PortStats* stats) {
  // 1. Replace the lights definition array wholesale with the artist's.
  const ordered_json& artist_lights = LightsArrayOf(artist);
  EnsureExtensionUsed(dest);
  dest["extensions"]["KHR_lights_punctual"]["lights"] = artist_lights;

  if (!dest.contains("nodes")) dest["nodes"] = ordered_json::array();

  // 2. Gather the artist's light nodes keyed by name.
  std::vector<int> artist_light_nodes = FindLightNodeIndices(artist);
  std::set<std::string> artist_names;
  for (int idx : artist_light_nodes) {
    artist_names.insert(artist.at("nodes")[idx].value("name", std::string()));
  }

  // 3. Remove export light nodes the artist no longer has.
  std::vector<int> dest_light_nodes = FindLightNodeIndices(dest);
  std::set<int> to_remove;
  for (int idx : dest_light_nodes) {
    std::string name = dest.at("nodes")[idx].value("name", std::string());
    if (!artist_names.count(name)) {
      to_remove.insert(idx);
      ++stats->lights_removed;
    }
  }
  RemoveNodes(dest, to_remove);

  // 4. Re-index the surviving export light nodes by name.
  dest_light_nodes = FindLightNodeIndices(dest);
  std::map<std::string, int> dest_by_name;
  for (int idx : dest_light_nodes) {
    dest_by_name[dest.at("nodes")[idx].value("name", std::string())] = idx;
  }

  int parent = FindLightParent(dest, dest_light_nodes);

  // Change of basis: artist light transforms are world-space under the artist's
  // parent; the export parents lights under a node that may carry its own
  // transform (e.g. the Worldspawn Z-up -> Y-up matrix). Re-express each artist
  // light's world transform in the export parent's local space:
  //   L_dest = W_dest_parent^-1 * W_artist(light).
  std::vector<int> dest_parents = BuildParentMap(dest);
  std::vector<int> artist_parents = BuildParentMap(artist);
  Eigen::Matrix4d dest_parent_inv = Eigen::Matrix4d::Identity();
  if (parent >= 0) {
    dest_parent_inv = WorldMatrix(dest, dest_parents, parent).inverse();
  }

  // 5. Apply each artist light node onto the export.
  for (int a_idx : artist_light_nodes) {
    const ordered_json& a_node = artist.at("nodes")[a_idx];
    std::string name = a_node.value("name", std::string());
    int a_light = a_node.at("extensions").at("KHR_lights_punctual").at("light").get<int>();
    Eigen::Matrix4d local =
        dest_parent_inv * WorldMatrix(artist, artist_parents, a_idx);

    auto it = dest_by_name.find(name);
    if (it != dest_by_name.end()) {
      // Update in place: rebased artist transform + light reference.
      ordered_json& d_node = dest["nodes"][it->second];
      SetNodeTransform(d_node, local);
      d_node["extensions"]["KHR_lights_punctual"]["light"] = a_light;
      ++stats->lights_updated;
    } else {
      // Append a new light node and parent it alongside the others.
      ordered_json d_node = ordered_json::object();
      if (!name.empty()) d_node["name"] = name;
      SetNodeTransform(d_node, local);
      d_node["extensions"]["KHR_lights_punctual"]["light"] = a_light;

      int new_idx = static_cast<int>(dest["nodes"].size());
      dest["nodes"].push_back(std::move(d_node));
      if (parent >= 0) {
        if (!dest["nodes"][parent].contains("children")) {
          dest["nodes"][parent]["children"] = ordered_json::array();
        }
        dest["nodes"][parent]["children"].push_back(new_idx);
      } else if (dest.contains("scenes") && dest.contains("scene")) {
        dest["scenes"][dest["scene"].get<int>()]["nodes"].push_back(new_idx);
      }
      ++stats->lights_added;
    }
  }
}

bool PortTextures(const fs::path& artist_base, const fs::path& export_base,
                  const PortConfig& config, PortStats* stats) {
  std::error_code ec;
  if (!fs::is_directory(artist_base, ec)) {
    LOG(ERROR) << "Artist base is not a directory: " << artist_base;
    return false;
  }
  if (!fs::is_directory(export_base, ec)) {
    LOG(ERROR) << "Export base is not a directory: " << export_base;
    return false;
  }

  for (auto it = fs::recursive_directory_iterator(artist_base, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file()) continue;
    const fs::path& src = it->path();
    if (!MatchesSuffix(src.filename().string(), config.texture_suffixes)) {
      continue;
    }

    fs::path rel = fs::relative(src, artist_base, ec);
    fs::path dst = export_base / rel;
    if (!fs::exists(dst)) {
      LOG(WARNING) << "No export counterpart for artist texture: " << rel;
      ++stats->textures_missing_dest;
      continue;
    }

    if (!fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec) ||
        ec) {
      LOG(ERROR) << "Failed to copy " << src << " -> " << dst << ": "
                 << ec.message();
      return false;
    }
    LOG(INFO) << "Replaced texture: " << rel;
    ++stats->textures_replaced;
  }
  if (ec) {
    LOG(ERROR) << "Error scanning artist textures: " << ec.message();
    return false;
  }
  return true;
}

bool PortLights(const fs::path& artist_base, const fs::path& export_base,
                const PortConfig& config, PortStats* stats) {
  fs::path artist_path = artist_base / config.artist_gltf;
  fs::path export_path = export_base / config.export_gltf;

  ordered_json artist = LoadGltf(artist_path);
  if (artist.is_null()) return false;
  ordered_json dest = LoadGltf(export_path);
  if (dest.is_null()) return false;

  MergeLights(artist, dest, stats);

  std::ofstream out(export_path);
  if (!out) {
    LOG(ERROR) << "Failed to open export glTF for writing: " << export_path;
    return false;
  }
  out << dest.dump();
  return true;
}

bool RunPort(const fs::path& artist_base, const fs::path& export_base,
             const PortConfig& config, PortStats* stats) {
  if (config.port_textures &&
      !PortTextures(artist_base, export_base, config, stats)) {
    return false;
  }
  if (config.port_lights &&
      !PortLights(artist_base, export_base, config, stats)) {
    return false;
  }
  return true;
}

}  // namespace ioq3_map
