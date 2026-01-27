#include "saver.h"

#include <glog/logging.h>
#include <tiny_gltf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <unordered_map>

namespace ioq3_map {
namespace {

const float kAreaLightIntensityScale = 1.0f;
const float kPunctualLightIntensityScale = 100.0f;

// Helpers for buffer management
void AddBufferView(const void* data, size_t size, size_t stride, int target,
                   int& view_index, tinygltf::Model* model) {
  if (model->buffers.empty()) {
    model->buffers.emplace_back();
  }
  tinygltf::Buffer& buffer = model->buffers[0];

  // Align to 4 bytes
  size_t padding = 0;
  if (buffer.data.size() % 4 != 0) {
    padding = 4 - (buffer.data.size() % 4);
  }
  for (size_t i = 0; i < padding; ++i) buffer.data.push_back(0);

  size_t byte_offset = buffer.data.size();
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  buffer.data.insert(buffer.data.end(), bytes, bytes + size);

  tinygltf::BufferView view;
  view.buffer = 0;
  view.byteOffset = byte_offset;
  view.byteLength = size;
  view.byteStride = stride;
  view.target = target;
  model->bufferViews.push_back(view);
  view_index = static_cast<int>(model->bufferViews.size() - 1);
}

int AddAccessor(int buffer_view, int component_type, size_t count, int type,
                const std::vector<double>& min_vals,
                const std::vector<double>& max_vals, tinygltf::Model* model) {
  tinygltf::Accessor acc;
  acc.bufferView = buffer_view;
  acc.byteOffset = 0;
  acc.componentType = component_type;
  acc.count = count;
  acc.type = type;
  acc.minValues = min_vals;
  acc.maxValues = max_vals;
  model->accessors.push_back(acc);
  return static_cast<int>(model->accessors.size() - 1);
}

std::optional<int> AddOrReuseTexture(
    const std::filesystem::path& from_uri,
    const std::filesystem::path& output_dir, tinygltf::Model* model,
    std::unordered_map<std::string, int>* texture_allocations) {
  // Copy the file to the same directory as the output file.
  // We use the filename as the relative URI in the glTF.
  std::filesystem::path filename = from_uri.filename();
  if (from_uri.has_parent_path() && from_uri.parent_path().has_filename()) {
    std::string new_name =
        from_uri.parent_path().filename().string() + "@" + filename.string();
    filename = std::filesystem::path(new_name);
  }
  std::filesystem::path destination = output_dir / filename;

  std::string uri_key = filename.string();
  auto texture_index_it = texture_allocations->find(uri_key);
  if (texture_index_it != texture_allocations->end()) {
    return texture_index_it->second;
  }

  try {
    // to_uri is usually unique, but just in case we have a collision we
    // will overwrite the file.
    if (!std::filesystem::exists(destination) ||
        !std::filesystem::equivalent(from_uri, destination)) {
      std::filesystem::copy_file(
          from_uri, destination,
          std::filesystem::copy_options::overwrite_existing);
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG(ERROR) << "Failed to copy file from " << from_uri << " to "
               << destination << ". Cause: " << e.what();
    return std::nullopt;
  }

  tinygltf::Image img;
  img.uri = uri_key;
  model->images.push_back(img);

  tinygltf::Texture tex;
  tex.source = static_cast<int>(model->images.size() - 1);
  model->textures.push_back(tex);

  texture_index_it =
      texture_allocations->emplace(uri_key, model->textures.size() - 1).first;
  return texture_index_it->second;
}

}  // namespace

bool SaveScene(const Scene& scene, const std::filesystem::path& path) {
  tinygltf::Model model;
  model.asset.generator = "ioq3-map-exporter";
  model.asset.version = "2.0";

  // Texture Allocations: absolute path string -> glTF texture index
  std::unordered_map<std::string, int> texture_allocations;
  // Material Mapping: BSPTextureIndex -> glTF Material Index
  std::unordered_map<BSPTextureIndex, int> bsp_to_gltf_material;

  // 1. Export Materials
  for (const auto& [bsp_tex_idx, mat] : scene.materials) {
    tinygltf::Material gmat;
    gmat.name = mat.name;

    // Populate PBR
    gmat.pbrMetallicRoughness.metallicFactor = 0.;
    gmat.pbrMetallicRoughness.roughnessFactor = 1.;

    // Handle Albedo Texture
    if (!mat.albedo.file_path.empty()) {
      auto texture_index =
          AddOrReuseTexture(mat.albedo.file_path, path.parent_path(), &model,
                            &texture_allocations);
      if (texture_index.has_value()) {
        gmat.pbrMetallicRoughness.baseColorTexture.index = *texture_index;
      }
    }

    // Handle Emission (Area Light)
    if (mat.emission_intensity > 0.0f) {
      // 1. Set Emissive Factor (White)
      gmat.emissiveFactor = {1.0, 1.0, 1.0};

      // 2. Use Emission Texture
      if (!mat.emission.file_path.empty()) {
        auto texture_index =
            AddOrReuseTexture(mat.emission.file_path, path.parent_path(),
                              &model, &texture_allocations);
        if (texture_index.has_value()) {
          gmat.emissiveTexture.index = *texture_index;
        }
      }

      // 3. Use KHR_materials_emissive_strength for high intensity
      if (std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(),
                    "KHR_materials_emissive_strength") ==
          model.extensionsUsed.end()) {
        model.extensionsUsed.push_back("KHR_materials_emissive_strength");
      }

      tinygltf::Value::Object ext_obj;
      ext_obj["emissiveStrength"] = tinygltf::Value(
          double(mat.emission_intensity * kAreaLightIntensityScale));
      gmat.extensions["KHR_materials_emissive_strength"] =
          tinygltf::Value(ext_obj);
    }

    model.materials.push_back(gmat);
    bsp_to_gltf_material[bsp_tex_idx] =
        static_cast<int>(model.materials.size() - 1);
  }

  // 2. Create Root "Worldspawn" Node
  tinygltf::Node world_node;
  world_node.name = "Worldspawn";
  // We will push this node last to ensure all children indices are valid,
  // or we can push it first and update children later.
  // Let's push it first to be node 0.
  model.nodes.push_back(world_node);
  int world_node_idx = 0;

  tinygltf::Scene gscene;
  gscene.nodes.push_back(world_node_idx);

  // 3. Export Geometries
  for (const auto& [bsp_surf_idx, geo] : scene.geometries) {
    // Create Mesh
    tinygltf::Mesh mesh;
    tinygltf::Primitive prim;
    prim.mode = TINYGLTF_MODE_TRIANGLES;

    // Find assigned material
    auto mat_it = bsp_to_gltf_material.find(geo.material_id);
    if (mat_it != bsp_to_gltf_material.end()) {
      prim.material = mat_it->second;
    }

    // Position
    {
      int view_idx;
      std::vector<float> buffer_data;
      buffer_data.reserve(geo.vertices.size() * 3);
      std::vector<double> min_v = {1e9, 1e9, 1e9};
      std::vector<double> max_v = {-1e9, -1e9, -1e9};

      for (const auto& v : geo.vertices) {
        buffer_data.push_back(v.x());
        buffer_data.push_back(v.y());
        buffer_data.push_back(v.z());

        if (v.x() < min_v[0]) min_v[0] = v.x();
        if (v.y() < min_v[1]) min_v[1] = v.y();
        if (v.z() < min_v[2]) min_v[2] = v.z();
        if (v.x() > max_v[0]) max_v[0] = v.x();
        if (v.y() > max_v[1]) max_v[1] = v.y();
        if (v.z() > max_v[2]) max_v[2] = v.z();
      }
      if (buffer_data.empty()) {
        min_v = {0, 0, 0};
        max_v = {0, 0, 0};
      }
      AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 12,
                    TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, &model);
      prim.attributes["POSITION"] = AddAccessor(
          view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.vertices.size(),
          TINYGLTF_TYPE_VEC3, min_v, max_v, &model);
    }

    // Normal
    if (!geo.normals.empty()) {
      int view_idx;
      std::vector<float> buffer_data;
      buffer_data.reserve(geo.normals.size() * 3);
      for (const auto& n : geo.normals) {
        buffer_data.push_back(n.x());
        buffer_data.push_back(n.y());
        buffer_data.push_back(n.z());
      }
      AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 12,
                    TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, &model);
      prim.attributes["NORMAL"] =
          AddAccessor(view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT,
                      geo.normals.size(), TINYGLTF_TYPE_VEC3, {}, {}, &model);
    }

    // Texcoord 0 (Texture UVs)
    if (!geo.texture_uvs.empty()) {
      int view_idx;
      std::vector<float> buffer_data;
      buffer_data.reserve(geo.texture_uvs.size() * 2);
      for (const auto& uv : geo.texture_uvs) {
        buffer_data.push_back(uv.x());
        buffer_data.push_back(uv.y());
      }
      AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 8,
                    TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, &model);
      prim.attributes["TEXCOORD_0"] = AddAccessor(
          view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.texture_uvs.size(),
          TINYGLTF_TYPE_VEC2, {}, {}, &model);
    }

    // Texcoord 1 (Lightmap UVs)
    if (!geo.lightmap_uvs.empty()) {
      int view_idx;
      std::vector<float> buffer_data;
      buffer_data.reserve(geo.lightmap_uvs.size() * 2);
      for (const auto& uv : geo.lightmap_uvs) {
        buffer_data.push_back(uv.x());
        buffer_data.push_back(uv.y());
      }
      AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 8,
                    TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, &model);
      prim.attributes["TEXCOORD_1"] = AddAccessor(
          view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.lightmap_uvs.size(),
          TINYGLTF_TYPE_VEC2, {}, {}, &model);
    }

    // Indices
    {
      int view_idx;
      AddBufferView(geo.indices.data(), geo.indices.size() * sizeof(uint32_t),
                    0, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, view_idx, &model);
      prim.indices =
          AddAccessor(view_idx, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                      geo.indices.size(), TINYGLTF_TYPE_SCALAR, {}, {}, &model);
    }

    mesh.primitives.push_back(prim);
    model.meshes.push_back(mesh);

    // Node for this mesh
    tinygltf::Node node;
    node.mesh = static_cast<int>(model.meshes.size() - 1);
    node.name =
        "Geometry_" + std::to_string(bsp_surf_idx);  // Optional: Name it

    // Transform
    Eigen::Matrix4f mat = geo.transform.matrix();
    std::vector<double> matrix;
    for (int k = 0; k < 16; ++k) matrix.push_back(mat(k));
    node.matrix = matrix;

    model.nodes.push_back(node);

    // Add as child of Worldspawn
    model.nodes[world_node_idx].children.push_back(
        static_cast<int>(model.nodes.size() - 1));
  }

  // TODO: Export Environment (Skybox)

  // 4. Export Lights (KHR_lights_punctual)
  if (!scene.lights.empty()) {
    tinygltf::Value::Array light_array;
    std::vector<int> light_node_indices;

    int light_idx = 0;
    for (const auto& light : scene.lights) {
      if (light.type == Light::Type::Area) continue;

      tinygltf::Value::Object light_obj;

      // Color
      std::vector<tinygltf::Value> color_vec;
      color_vec.push_back(tinygltf::Value(double(light.color.x())));
      color_vec.push_back(tinygltf::Value(double(light.color.y())));
      color_vec.push_back(tinygltf::Value(double(light.color.z())));
      light_obj["color"] = tinygltf::Value(color_vec);

      light_obj["intensity"] = tinygltf::Value(
          double(light.intensity * kPunctualLightIntensityScale));

      std::string type_str;
      if (light.type == Light::Type::Directional) {
        type_str = "directional";
      } else if (light.type == Light::Type::Point) {
        type_str = "point";
      } else if (light.type == Light::Type::Spot) {
        type_str = "spot";

        tinygltf::Value::Object spot_obj;
        // Clamp to [-1, 1] to avoid NaN from std::acos with -ffast-math
        auto safe_acos = [](float cos_val) -> double {
          if (cos_val >= 1.0f) return 0.0;
          if (cos_val <= -1.0f) return 3.14159265358979323846;
          return std::acos(cos_val);
        };

        spot_obj["innerConeAngle"] =
            tinygltf::Value(safe_acos(light.cos_inner_cone));
        spot_obj["outerConeAngle"] =
            tinygltf::Value(safe_acos(light.cos_outer_cone));
        light_obj["spot"] = tinygltf::Value(spot_obj);
      }
      light_obj["type"] = tinygltf::Value(type_str);
      light_obj["name"] = tinygltf::Value("Light_" + std::to_string(light_idx));

      light_array.push_back(tinygltf::Value(light_obj));

      // Create Node for this light
      tinygltf::Node node;
      node.name = "LightNode_" + std::to_string(light_idx);

      // Position (Translation)
      node.translation.push_back(light.position.x());
      node.translation.push_back(light.position.y());
      node.translation.push_back(light.position.z());

      // Orientation (Rotation)
      // glTF lights point down -Z. We need to align -Z with light.direction.
      if (light.type == Light::Type::Directional ||
          light.type == Light::Type::Spot) {
        Eigen::Vector3f Z = -light.direction.normalized();
        Eigen::Vector3f up = Eigen::Vector3f::UnitY();
        if (std::abs(Z.dot(up)) > 0.99f) up = Eigen::Vector3f::UnitX();

        Eigen::Vector3f X = up.cross(Z).normalized();
        Eigen::Vector3f Y = Z.cross(X).normalized();

        Eigen::Matrix3f rot;
        rot.col(0) = X;
        rot.col(1) = Y;
        rot.col(2) = Z;

        Eigen::Quaternionf q(rot);
        node.rotation.push_back(q.x());
        node.rotation.push_back(q.y());
        node.rotation.push_back(q.z());
        node.rotation.push_back(q.w());
      }

      // Extension on Node
      node.light = light_idx;

      model.nodes.push_back(node);
      // Add light as child of world
      model.nodes[world_node_idx].children.push_back(
          static_cast<int>(model.nodes.size() - 1));

      light_idx++;
    }

    if (light_idx > 0) {
      if (std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(),
                    "KHR_lights_punctual") == model.extensionsUsed.end()) {
        model.extensionsUsed.push_back("KHR_lights_punctual");
      }

      tinygltf::Value::Object ext_container;
      ext_container["lights"] = tinygltf::Value(light_array);
      model.extensions["KHR_lights_punctual"] = tinygltf::Value(ext_container);
    }
  }

  model.scenes.push_back(gscene);
  model.defaultScene = 0;

  tinygltf::TinyGLTF loader;
  return loader.WriteGltfSceneToFile(&model, path.string(),
                                     /*embed_images=*/false,
                                     /*embed_textures=*/false,
                                     /*embed_buffers=*/false,
                                     /*embed_binary=*/false);
}

}  // namespace ioq3_map
