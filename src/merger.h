#ifndef IOQ3_MAP_EXPORTER_SRC_MERGER_H_
#define IOQ3_MAP_EXPORTER_SRC_MERGER_H_

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ioq3_map {

// Statistics describing what a port operation changed. All counters are
// cumulative across the textures + lights passes of a single RunPort() call.
struct PortStats {
  // Texture pass.
  int textures_replaced = 0;       // artist file copied over an export file.
  int textures_missing_dest = 0;   // artist had a file with no export counterpart.

  // Light pass.
  int lights_updated = 0;  // existing export light node updated in place.
  int lights_added = 0;    // new artist light node appended to the export.
  int lights_removed = 0;  // stale export light node deleted.
};

// Controls which parts of the artist edit are merged and which files are used.
struct PortConfig {
  bool port_textures = true;
  bool port_lights = true;

  // glTF filename (relative to the respective base directory) to read the
  // artist-edited lights from and to merge into. The export side is rewritten
  // in place.
  std::string artist_gltf = "scene.gltf";
  std::string export_gltf = "scene.gltf";

  // Texture filename suffixes (PBR maps) to port. Defaults to the placeholder
  // albedo + ORM + normal maps the artist modernizes.
  std::vector<std::string> texture_suffixes = {"_albedo.png", "_orm.png",
                                               "_normal.png"};
};

// Copies the artist-edited PBR texture maps (albedo, ORM, normal — those whose
// filename ends with one of config.texture_suffixes) from artist_base into
// export_base, matched by
// path relative to each base directory. Only existing export files are
// overwritten (i.e. placeholders are replaced); artist files without an export
// counterpart are counted in stats.textures_missing_dest and skipped.
// Returns false on a filesystem error.
bool PortTextures(const std::filesystem::path& artist_base,
                  const std::filesystem::path& export_base,
                  const PortConfig& config, PortStats* stats);

// Replaces the KHR_lights_punctual light sources of the export glTF document
// (dest) with those of the artist document. Light nodes are matched by name:
// matching nodes are updated in place (transform + light reference), artist-only
// nodes are appended (parented under the export's light-node parent / root), and
// export-only ("stale") light nodes are removed. The lights array itself is
// replaced wholesale with the artist's. dest is mutated in place.
void MergeLights(const nlohmann::ordered_json& artist,
                 nlohmann::ordered_json& dest, PortStats* stats);

// Loads config.export_gltf and config.artist_gltf, runs MergeLights, and writes
// the export glTF back in place. Returns false on I/O or parse error.
bool PortLights(const std::filesystem::path& artist_base,
                const std::filesystem::path& export_base,
                const PortConfig& config, PortStats* stats);

// Top-level entry point: merges the artist edit in artist_base into the original
// export in export_base according to config. Returns false on error.
bool RunPort(const std::filesystem::path& artist_base,
             const std::filesystem::path& export_base, const PortConfig& config,
             PortStats* stats);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_SRC_MERGER_H_
