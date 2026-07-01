#include "saver.h"

#include <glog/logging.h>
#include <nlohmann/json.hpp>
#include <tiny_gltf.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

// Include stb headers for image processing
#include <stb_image.h>
#include <stb_image_write.h>

#include <fstream>

namespace ioq3_map {
namespace {

const float kAreaLightIntensityScale = .5f;
const float kPunctualLightIntensityScale = 100.0f;

// Maps a Quake 3 blend factor to its canonical GL name (without the GL_ prefix),
// e.g. BlendFunc::ONE_MINUS_SRC_ALPHA -> "ONE_MINUS_SRC_ALPHA". The raw source
// and destination factors are emitted into the SH_material_layers extension so
// the renderer can reproduce the exact Quake 3 blend state, rather than a lossy
// enumerated mode that cannot express combinations like (SRC_ALPHA, ONE).
const char* BlendFuncToString(BlendFunc f) {
  switch (f) {
    case BlendFunc::ZERO: return "ZERO";
    case BlendFunc::ONE: return "ONE";
    case BlendFunc::SRC_COLOR: return "SRC_COLOR";
    case BlendFunc::ONE_MINUS_SRC_COLOR: return "ONE_MINUS_SRC_COLOR";
    case BlendFunc::DST_COLOR: return "DST_COLOR";
    case BlendFunc::ONE_MINUS_DST_COLOR: return "ONE_MINUS_DST_COLOR";
    case BlendFunc::SRC_ALPHA: return "SRC_ALPHA";
    case BlendFunc::ONE_MINUS_SRC_ALPHA: return "ONE_MINUS_SRC_ALPHA";
    case BlendFunc::DST_ALPHA: return "DST_ALPHA";
    case BlendFunc::ONE_MINUS_DST_ALPHA: return "ONE_MINUS_DST_ALPHA";
  }
  return "ONE";
}

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

void CreateSolidColorPNG(const std::filesystem::path& out_path, int r, int g,
                         int b) {
  if (std::filesystem::exists(out_path)) return;
  unsigned char data[4] = {static_cast<unsigned char>(r),
                           static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), 255};
  stbi_write_png(out_path.string().c_str(), 1, 1, 4, data, 4);
}

void ConvertToPNG(const std::filesystem::path& src,
                  const std::filesystem::path& dst, bool black_as_alpha) {
  if (std::filesystem::exists(dst)) return;
  int w, h, c;
  unsigned char* data = stbi_load(src.string().c_str(), &w, &h, &c, 4);
  if (data) {
    if (black_as_alpha) {
      for (int i = 0; i < w * h; ++i) {
        unsigned char r = data[4 * i + 0];
        unsigned char g = data[4 * i + 1];
        unsigned char b = data[4 * i + 2];
        unsigned char max_val = std::max({r, g, b});
        data[4 * i + 3] = max_val;
      }
    }
    stbi_write_png(dst.string().c_str(), w, h, 4, data, w * 4);
    stbi_image_free(data);
  } else {
    LOG(ERROR) << "Failed to load texture for conversion: " << src;
  }
}

int GetOrAddTexture(tinygltf::Model* model,
                    std::unordered_map<std::string, int>* texture_allocations,
                    const std::string& uri) {
  if (auto it = texture_allocations->find(uri);
      it != texture_allocations->end()) {
    return it->second;
  }

  tinygltf::Image img;
  img.uri = uri;
  model->images.push_back(img);

  tinygltf::Texture tex;
  tex.source = static_cast<int>(model->images.size() - 1);
  model->textures.push_back(tex);

  int idx = static_cast<int>(model->textures.size() - 1);
  (*texture_allocations)[uri] = idx;
  return idx;
}

// Emits `geo` as a mesh + single primitive + node parented under
// `world_node_idx`. `gltf_material` < 0 leaves the primitive's material unset.
// When `occluder_only`, the primitive is tagged with the SH_occluder extension
// and its lightmap UV set (TEXCOORD_1) is suppressed — an occluder shell never
// receives a lightmap chart.
void EmitGeometryNode(const Geometry& geo, const std::string& node_name,
                      int gltf_material, bool occluder_only, int world_node_idx,
                      tinygltf::Model* model) {
  // Optional attributes are emitted only when they line up 1:1 with the
  // vertices, so a mismatched attribute is dropped rather than producing an
  // invalid primitive with unequal accessor counts. (We do NOT early-return on a
  // mismatch: the caller has already recorded this primitive's index in the
  // manifest, so skipping emission here would desync that mapping.)
  const size_t vertex_count = geo.vertices.size();

  tinygltf::Mesh mesh;
  tinygltf::Primitive prim;
  prim.mode = TINYGLTF_MODE_TRIANGLES;
  if (gltf_material >= 0) prim.material = gltf_material;

  // Position (with required min/max bounds).
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
                  TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, model);
    prim.attributes["POSITION"] =
        AddAccessor(view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.vertices.size(),
                    TINYGLTF_TYPE_VEC3, min_v, max_v, model);
  }

  // Normal.
  if (!geo.normals.empty() && geo.normals.size() == vertex_count) {
    int view_idx;
    std::vector<float> buffer_data;
    buffer_data.reserve(geo.normals.size() * 3);
    for (const auto& n : geo.normals) {
      buffer_data.push_back(n.x());
      buffer_data.push_back(n.y());
      buffer_data.push_back(n.z());
    }
    AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 12,
                  TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, model);
    prim.attributes["NORMAL"] =
        AddAccessor(view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.normals.size(),
                    TINYGLTF_TYPE_VEC3, {}, {}, model);
  }

  // Texcoord 0 (texture UVs).
  if (!geo.texture_uvs.empty() && geo.texture_uvs.size() == vertex_count) {
    int view_idx;
    std::vector<float> buffer_data;
    buffer_data.reserve(geo.texture_uvs.size() * 2);
    for (const auto& uv : geo.texture_uvs) {
      buffer_data.push_back(uv.x());
      buffer_data.push_back(uv.y());
    }
    AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 8,
                  TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, model);
    prim.attributes["TEXCOORD_0"] = AddAccessor(
        view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.texture_uvs.size(),
        TINYGLTF_TYPE_VEC2, {}, {}, model);
  }

  // Texcoord 1 (Q3 lightmap UVs) — never on an occluder shell.
  if (!occluder_only && !geo.lightmap_uvs.empty() &&
      geo.lightmap_uvs.size() == vertex_count) {
    int view_idx;
    std::vector<float> buffer_data;
    buffer_data.reserve(geo.lightmap_uvs.size() * 2);
    for (const auto& uv : geo.lightmap_uvs) {
      buffer_data.push_back(uv.x());
      buffer_data.push_back(uv.y());
    }
    AddBufferView(buffer_data.data(), buffer_data.size() * sizeof(float), 8,
                  TINYGLTF_TARGET_ARRAY_BUFFER, view_idx, model);
    prim.attributes["TEXCOORD_1"] = AddAccessor(
        view_idx, TINYGLTF_COMPONENT_TYPE_FLOAT, geo.lightmap_uvs.size(),
        TINYGLTF_TYPE_VEC2, {}, {}, model);
  }

  // Indices.
  {
    int view_idx;
    AddBufferView(geo.indices.data(), geo.indices.size() * sizeof(uint32_t), 0,
                  TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, view_idx, model);
    prim.indices =
        AddAccessor(view_idx, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                    geo.indices.size(), TINYGLTF_TYPE_SCALAR, {}, {}, model);
  }

  if (occluder_only) {
    tinygltf::Value::Object occ;
    occ["occluderOnly"] = tinygltf::Value(true);
    prim.extensions["SH_occluder"] = tinygltf::Value(occ);
  }

  mesh.primitives.push_back(prim);
  model->meshes.push_back(mesh);

  tinygltf::Node node;
  node.mesh = static_cast<int>(model->meshes.size() - 1);
  node.name = node_name;
  Eigen::Matrix4f matrix = geo.transform.matrix();
  std::vector<double> node_matrix;
  for (int k = 0; k < 16; ++k) node_matrix.push_back(matrix(k));
  node.matrix = node_matrix;
  model->nodes.push_back(node);
  model->nodes[world_node_idx].children.push_back(
      static_cast<int>(model->nodes.size() - 1));
}

}  // namespace

bool SaveScene(const Scene& scene, const std::filesystem::path& path) {
  tinygltf::Model model;
  model.asset.generator = "ioq3-map-exporter";
  model.asset.version = "2.0";

  // Texture Allocations: uri string -> glTF texture index
  std::unordered_map<std::string, int> texture_allocations;
  // Material Mapping: BSPTextureIndex -> glTF Material Index
  std::unordered_map<BSPTextureIndex, int> bsp_to_gltf_material;

  bool used_sh_material_layers = false;
  bool used_khr_emissive_strength = false;

  std::filesystem::path export_root = path.parent_path();
  std::filesystem::create_directories(export_root);

  // 1. Export Materials
  for (const auto& [bsp_tex_idx, mat] : scene.materials) {
    tinygltf::Material gmat;
    gmat.name = mat.name;

    // Populate PBR
    gmat.pbrMetallicRoughness.metallicFactor = 0.;
    gmat.pbrMetallicRoughness.roughnessFactor = 1.;

    // Determine Folder Name
    // Determine Folder Name
    std::filesystem::path folder_name;
    std::filesystem::path from_uri;

    if (!mat.albedo.file_path.empty())
      from_uri = mat.albedo.file_path;
    else if (!mat.emission.file_path.empty())
      from_uri = mat.emission.file_path;

    if (!from_uri.empty()) {
      if (from_uri.has_parent_path() && from_uri.parent_path().has_filename()) {
        folder_name = from_uri.parent_path().filename().string() + "@" +
                      from_uri.stem().string();
      } else {
        folder_name = from_uri.stem();
      }
    } else {
      folder_name = "material_" + std::to_string(bsp_tex_idx);
    }

    std::filesystem::path texture_dir = export_root / folder_name;
    std::filesystem::create_directories(texture_dir);

    // Handle Albedo
    if (!mat.albedo.file_path.empty()) {
      std::string diff_name = folder_name.string() + "_diffuse.png";
      std::string albedo_name = folder_name.string() + "_albedo.png";
      std::string normal_name = folder_name.string() + "_normal.png";
      std::string orm_name = folder_name.string() + "_orm.png";

      // Convert Original
      ConvertToPNG(mat.albedo.file_path, texture_dir / diff_name,
                   mat.albedo.black_as_alpha);

      // Create Placeholders
      CreateSolidColorPNG(texture_dir / albedo_name, 255, 0, 255);
      CreateSolidColorPNG(texture_dir / normal_name, 128, 128, 255);
      CreateSolidColorPNG(texture_dir / orm_name, 255, 255, 0);

      // Link glTF to Placeholders
      std::string albedo_uri = (folder_name / albedo_name).string();
      std::string orm_uri = (folder_name / orm_name).string();
      std::string normal_uri = (folder_name / normal_name).string();

      gmat.pbrMetallicRoughness.baseColorTexture.index =
          GetOrAddTexture(&model, &texture_allocations, albedo_uri);
      gmat.pbrMetallicRoughness.metallicRoughnessTexture.index =
          GetOrAddTexture(&model, &texture_allocations, orm_uri);
      gmat.normalTexture.index =
          GetOrAddTexture(&model, &texture_allocations, normal_uri);
    }

    // Handle Emission (Area Light)
    if (mat.emission_intensity > 0.0f) {
      // 1. Set Emissive Factor (White)
      gmat.emissiveFactor = {1.0, 1.0, 1.0};

      // 2. Use Emission Texture
      if (!mat.emission.file_path.empty()) {
        std::string emissive_name = folder_name.string() + "_emissive.png";
        ConvertToPNG(mat.emission.file_path, texture_dir / emissive_name,
                     mat.emission.black_as_alpha);

        std::string emissive_uri = (folder_name / emissive_name).string();
        gmat.emissiveTexture.index =
            GetOrAddTexture(&model, &texture_allocations, emissive_uri);
      }

      // 3. Use KHR_materials_emissive_strength
      used_khr_emissive_strength = true;

      tinygltf::Value::Object ext_obj;
      ext_obj["emissiveStrength"] = tinygltf::Value(
          double(mat.emission_intensity * kAreaLightIntensityScale));
      gmat.extensions["KHR_materials_emissive_strength"] =
          tinygltf::Value(ext_obj);
    }

    // Construct SH_material_layers extension
    if (!mat.texture_layers.empty()) {
      tinygltf::Value::Object sh_ext;
      
      std::string surfaceBlend = "OPAQUE";
      if ((mat.surface_flags & SURF_ALPHASHADOW) ||
          (mat.surface_flags & SURF_NONSOLID)) {
        surfaceBlend = "BLEND";
      } else if (mat.texture_layers.size() == 1 &&
                 mat.texture_layers[0].blend_src == BlendFunc::ONE &&
                 mat.texture_layers[0].blend_dst == BlendFunc::ONE) {
        surfaceBlend = "ADD";
      }
      sh_ext["surfaceBlend"] = tinygltf::Value(surfaceBlend);
      
      std::string cullStr = "FRONT";
      if (mat.cull == Q3CullType::BACK) cullStr = "BACK";
      else if (mat.cull == Q3CullType::NONE) cullStr = "NONE";
      
      sh_ext["cullMode"] = tinygltf::Value(cullStr);

      // Index of the layer whose texture is the albedo source; the modern
      // `_albedo` map substitutes for this layer when consumers composite.
      // Guard against an out-of-range index so we never emit an invalid
      // reference (e.g. from a hand-built material).
      int base_layer = mat.albedo_layer;
      if (base_layer < 0 ||
          base_layer >= static_cast<int>(mat.texture_layers.size())) {
        LOG(WARNING) << "Material " << mat.name << " has invalid albedo_layer "
                     << base_layer << " (texture_layers size is "
                     << mat.texture_layers.size() << "). Defaulting to 0.";
        base_layer = 0;
      }
      sh_ext["baseLayer"] = tinygltf::Value(base_layer);

      tinygltf::Value::Array layers_array;
      for (size_t i = 0; i < mat.texture_layers.size(); ++i) {
        const auto& layer = mat.texture_layers[i];
        tinygltf::Value::Object layer_obj;

        std::string tex_name = folder_name.string() + "_layer" + std::to_string(i) + ".png";
        std::string tex_uri = (folder_name / tex_name).string();
        
        ConvertToPNG(layer.path, texture_dir / tex_name, false);

        tinygltf::Value::Object tex_obj;
        tex_obj["index"] = tinygltf::Value(GetOrAddTexture(&model, &texture_allocations, tex_uri));
        layer_obj["texture"] = tinygltf::Value(tex_obj);

        // Animated stage (animMap): keep `texture` pointing at frame 0 above and
        // emit the playback frequency plus the full list of frame texture
        // indices so the renderer can cycle through them.
        if (!layer.anim_frame_paths.empty()) {
          layer_obj["animFreq"] = tinygltf::Value(double(layer.anim_frequency));
          tinygltf::Value::Array anim_frames;
          for (size_t f = 0; f < layer.anim_frame_paths.size(); ++f) {
            std::string frame_uri;
            if (f == 0) {
              // Frame 0 is the layer's representative texture, already written
              // above as `_layer{i}.png`. Reuse it so animFrames[0] dedups to
              // the same texture index as `texture`.
              frame_uri = tex_uri;
            } else {
              std::string frame_name = folder_name.string() + "_layer" +
                                       std::to_string(i) + "_frame" +
                                       std::to_string(f) + ".png";
              frame_uri = (folder_name / frame_name).string();
              ConvertToPNG(layer.anim_frame_paths[f], texture_dir / frame_name,
                           false);
            }
            anim_frames.push_back(tinygltf::Value(
                GetOrAddTexture(&model, &texture_allocations, frame_uri)));
          }
          layer_obj["animFrames"] = tinygltf::Value(anim_frames);
        }

        // Emit the raw Quake 3 blendFunc factors so the renderer can reproduce
        // the exact GL blend state. An enumerated blendMode would be lossy and
        // could not express combinations such as (SRC_ALPHA, ONE).
        layer_obj["blendSrc"] = tinygltf::Value(std::string(BlendFuncToString(layer.blend_src)));
        layer_obj["blendDst"] = tinygltf::Value(std::string(BlendFuncToString(layer.blend_dst)));

        tinygltf::Value::Object rgbgen_obj;
        if (layer.rgbgen.type == RgbGenType::IDENTITY) rgbgen_obj["type"] = tinygltf::Value("IDENTITY");
        else if (layer.rgbgen.type == RgbGenType::VERTEX) rgbgen_obj["type"] = tinygltf::Value("VERTEX");
        else if (layer.rgbgen.type == RgbGenType::EXACT_VERTEX) rgbgen_obj["type"] = tinygltf::Value("EXACT_VERTEX");
        else if (layer.rgbgen.type == RgbGenType::IDENTITY_LIGHTING) rgbgen_obj["type"] = tinygltf::Value("IDENTITY_LIGHTING");
        else if (layer.rgbgen.type == RgbGenType::WAVE) {
            rgbgen_obj["type"] = tinygltf::Value("WAVE");
            if (layer.rgbgen.wave_type == Q3WaveType::SINE) rgbgen_obj["func"] = tinygltf::Value("SIN");
            else if (layer.rgbgen.wave_type == Q3WaveType::TRIANGLE) rgbgen_obj["func"] = tinygltf::Value("TRIANGLE");
            else if (layer.rgbgen.wave_type == Q3WaveType::SQUARE) rgbgen_obj["func"] = tinygltf::Value("SQUARE");
            else if (layer.rgbgen.wave_type == Q3WaveType::SAWTOOTH) rgbgen_obj["func"] = tinygltf::Value("SAWTOOTH");
            else if (layer.rgbgen.wave_type == Q3WaveType::INVERSE_SAWTOOTH) rgbgen_obj["func"] = tinygltf::Value("INVERSE_SAWTOOTH");
            
            rgbgen_obj["base"] = tinygltf::Value(double(layer.rgbgen.base));
            rgbgen_obj["amplitude"] = tinygltf::Value(double(layer.rgbgen.amplitude));
            rgbgen_obj["phase"] = tinygltf::Value(double(layer.rgbgen.phase));
            rgbgen_obj["frequency"] = tinygltf::Value(double(layer.rgbgen.frequency));
        }
        layer_obj["rgbGen"] = tinygltf::Value(rgbgen_obj);

        tinygltf::Value::Array tcmods;
        if (std::holds_alternative<Q3TCModScale>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModScale>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("SCALE");
            tinygltf::Value::Array val_arr = {tinygltf::Value(double(mod.s_scale)), tinygltf::Value(double(mod.t_scale))};
            t["value"] = tinygltf::Value(val_arr);
            tcmods.push_back(tinygltf::Value(t));
        } else if (std::holds_alternative<Q3TCModScroll>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModScroll>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("SCROLL");
            tinygltf::Value::Array val_arr = {tinygltf::Value(double(mod.s_rate)), tinygltf::Value(double(mod.t_rate))};
            t["value"] = tinygltf::Value(val_arr);
            tcmods.push_back(tinygltf::Value(t));
        } else if (std::holds_alternative<Q3TCModRotate>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModRotate>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("ROTATE");
            t["value"] = tinygltf::Value(double(mod.angle));
            tcmods.push_back(tinygltf::Value(t));
        } else if (std::holds_alternative<Q3TCModTurb>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModTurb>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("TURB");
            std::string wave_str = "NONE";
            if (mod.wave_type == Q3WaveType::SINE) wave_str = "SIN";
            else if (mod.wave_type == Q3WaveType::TRIANGLE) wave_str = "TRIANGLE";
            else if (mod.wave_type == Q3WaveType::SQUARE) wave_str = "SQUARE";
            else if (mod.wave_type == Q3WaveType::SAWTOOTH) wave_str = "SAWTOOTH";
            else if (mod.wave_type == Q3WaveType::INVERSE_SAWTOOTH) wave_str = "INVERSE_SAWTOOTH";
            tinygltf::Value::Array val_arr = {tinygltf::Value(wave_str), tinygltf::Value(double(mod.base)), tinygltf::Value(double(mod.amplitude)), tinygltf::Value(double(mod.phase)), tinygltf::Value(double(mod.frequency))};
            t["value"] = tinygltf::Value(val_arr);
            tcmods.push_back(tinygltf::Value(t));
        } else if (std::holds_alternative<Q3TCModStretch>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModStretch>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("STRETCH");
            std::string wave_str = "NONE";
            if (mod.wave_type == Q3WaveType::SINE) wave_str = "SIN";
            else if (mod.wave_type == Q3WaveType::TRIANGLE) wave_str = "TRIANGLE";
            else if (mod.wave_type == Q3WaveType::SQUARE) wave_str = "SQUARE";
            else if (mod.wave_type == Q3WaveType::SAWTOOTH) wave_str = "SAWTOOTH";
            else if (mod.wave_type == Q3WaveType::INVERSE_SAWTOOTH) wave_str = "INVERSE_SAWTOOTH";
            tinygltf::Value::Array val_arr = {tinygltf::Value(wave_str), tinygltf::Value(double(mod.base)), tinygltf::Value(double(mod.amplitude)), tinygltf::Value(double(mod.phase)), tinygltf::Value(double(mod.frequency))};
            t["value"] = tinygltf::Value(val_arr);
            tcmods.push_back(tinygltf::Value(t));
        } else if (std::holds_alternative<Q3TCModTransform>(layer.tcmod)) {
            const auto& mod = std::get<Q3TCModTransform>(layer.tcmod);
            tinygltf::Value::Object t;
            t["type"] = tinygltf::Value("TRANSFORM");
            tinygltf::Value::Array val_arr = {tinygltf::Value(double(mod(0,0))), tinygltf::Value(double(mod(0,1))), tinygltf::Value(double(mod(0,2))), tinygltf::Value(double(mod(1,0))), tinygltf::Value(double(mod(1,1))), tinygltf::Value(double(mod(1,2)))};
            t["value"] = tinygltf::Value(val_arr);
            tcmods.push_back(tinygltf::Value(t));
        }
        
        if (!tcmods.empty()) {
            layer_obj["tcMod"] = tinygltf::Value(tcmods);
        }

        layers_array.push_back(tinygltf::Value(layer_obj));
      }
      sh_ext["layers"] = tinygltf::Value(layers_array);

      used_sh_material_layers = true;
      gmat.extensions["SH_material_layers"] = tinygltf::Value(sh_ext);
    }

    model.materials.push_back(gmat);
    bsp_to_gltf_material[bsp_tex_idx] =
        static_cast<int>(model.materials.size() - 1);
  }

  if (used_sh_material_layers) {
    model.extensionsUsed.push_back("SH_material_layers");
  }
  if (used_khr_emissive_strength) {
    model.extensionsUsed.push_back("KHR_materials_emissive_strength");
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
  //
  // Iterate geometries in ascending BSP surface index order (rather than the
  // unordered_map hash order) so that both scene.gltf and the Phase 6
  // manifest.json are deterministic and diffable across re-exports. Each
  // geometry becomes exactly one mesh holding one primitive, so the running
  // counter below is the global flat glTF primitive index.
  std::vector<std::pair<BSPSurfaceIndex, const Geometry*>> sorted_geometries;
  sorted_geometries.reserve(scene.geometries.size());
  for (const auto& [bsp_surf_idx, geo] : scene.geometries) {
    sorted_geometries.emplace_back(bsp_surf_idx, &geo);
  }
  std::sort(sorted_geometries.begin(), sorted_geometries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // Phase 6 manifest: bsp_surface_index -> gltf_primitive_index + material.
  nlohmann::json surface_mapping = nlohmann::json::array();
  int gltf_primitive_index = 0;

  for (const auto& [bsp_surf_idx, geo_ptr] : sorted_geometries) {
    const Geometry& geo = *geo_ptr;

    std::string material_name;
    auto mat_name_it = scene.materials.find(geo.material_id);
    if (mat_name_it != scene.materials.end()) {
      material_name = mat_name_it->second.name;
    }
    surface_mapping.push_back({
        {"bsp_surface_index", static_cast<int>(bsp_surf_idx)},
        {"gltf_primitive_index", gltf_primitive_index},
        {"material", material_name},
    });
    ++gltf_primitive_index;

    int gltf_material = -1;
    auto mat_it = bsp_to_gltf_material.find(geo.material_id);
    if (mat_it != bsp_to_gltf_material.end()) {
      gltf_material = mat_it->second;
    }

    EmitGeometryNode(geo, "Geometry_" + std::to_string(bsp_surf_idx),
                     gltf_material, /*occluder_only=*/false, world_node_idx,
                     &model);
  }

  // 3b. Export occluder shells. These are synthetic (no BSP face), so they are
  // emitted after every real surface — their glTF primitive indices fall past
  // the last real one and are deliberately absent from manifest.json, keeping
  // the real-face -> primitive mapping intact. Named after their source surface
  // for debugging. Each carries SH_occluder and no lightmap UVs.
  bool used_sh_occluder = false;
  for (const Geometry& shell : scene.occluder_shells) {
    int gltf_material = -1;
    auto mat_it = bsp_to_gltf_material.find(shell.material_id);
    if (mat_it != bsp_to_gltf_material.end()) {
      gltf_material = mat_it->second;
    }
    std::string node_name =
        "Geometry_" + std::to_string(shell.source_surface) + "_shell";
    EmitGeometryNode(shell, node_name, gltf_material, /*occluder_only=*/true,
                     world_node_idx, &model);
    used_sh_occluder = true;
  }
  if (used_sh_occluder) {
    model.extensionsUsed.push_back("SH_occluder");
  }

  // TODO: Export Environment (Skybox)

  // 4. Export Lights (KHR_lights_punctual)
  if (!scene.lights.empty()) {
    tinygltf::Value::Array light_array;
    std::vector<int> light_node_indices;

    int light_idx = 0;
    for (const auto& light : scene.lights) {
      if (light.type == Light::Type::Area) {
        continue;
      }

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

  model.defaultScene = 0;

  // Manual Write to control URIs and Files and bypass TinyGLTF path stripping

  // 1. Write Binary Buffer
  if (!model.buffers.empty() && !model.buffers[0].data.empty()) {
    std::string bin_filename = path.stem().string() + ".bin";
    model.buffers[0].uri = bin_filename;

    std::filesystem::path bin_path = path.parent_path() / bin_filename;
    std::ofstream bin_file(bin_path, std::ios::binary);
    if (bin_file) {
      bin_file.write(
          reinterpret_cast<const char*>(model.buffers[0].data.data()),
          model.buffers[0].data.size());
    }
  }

  // 2. Write glTF JSON
  tinygltf::TinyGLTF loader;
  std::stringstream ss;
  bool ret = loader.WriteGltfSceneToStream(&model, ss, false, false);
  if (ret) {
    std::string json_str = ss.str();

    // Patch URIs because TinyGLTF might strip paths
    for (const auto& img : model.images) {
      if (img.uri.empty()) continue;
      std::filesystem::path full_path = img.uri;
      std::string base_name = full_path.filename().string();

      // We look for "uri" : "base_name" and replace with "uri" : "full_uri"
      // Note: tinygltf output usually doesn't have spaces around :
      std::string search = "\"uri\":\"" + base_name + "\"";
      std::string replace = "\"uri\":\"" + img.uri + "\"";

      size_t pos = 0;
      while ((pos = json_str.find(search, pos)) != std::string::npos) {
        json_str.replace(pos, search.length(), replace);
        pos += replace.length();
      }
    }

    std::ofstream gltf_file(path);
    gltf_file << json_str;
  } else {
    return false;
  }

  // 3. Write Phase 6 manifest (BSP surface index -> glTF primitive index +
  // material name), alongside scene.gltf.
  {
    nlohmann::json manifest;
    manifest["surface_mapping"] = std::move(surface_mapping);

    std::filesystem::path manifest_path =
        path.parent_path() / "manifest.json";
    std::ofstream manifest_file(manifest_path);
    if (!manifest_file) {
      LOG(ERROR) << "Failed to open manifest file for writing: "
                 << manifest_path;
      return false;
    }
    manifest_file << manifest.dump(2);
    manifest_file.close();
    if (!manifest_file) {
      LOG(ERROR) << "Failed to write manifest data to " << manifest_path;
      return false;
    }
  }

  return true;
}

}  // namespace ioq3_map
