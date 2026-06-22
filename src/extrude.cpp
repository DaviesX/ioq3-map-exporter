#include "extrude.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace ioq3_map {

namespace {

// Packs an undirected edge (the two endpoint indices, order-independent) into a
// single 64-bit key for hashing.
uint64_t EdgeKey(uint32_t a, uint32_t b) {
  uint32_t lo = a < b ? a : b;
  uint32_t hi = a < b ? b : a;
  return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

}  // namespace

void SolidifyGeometry(Geometry* geo, float thickness, float inset) {
  if (geo == nullptr || thickness <= 0.0f) {
    return;
  }
  const size_t front_count = geo->vertices.size();
  const size_t front_index_count = geo->indices.size();
  if (front_count < 3 || front_index_count < 3) {
    return;
  }
  // Normals drive the extrusion direction, so they must be present and aligned
  // with the vertices. UVs are copied only when they line up 1:1.
  if (geo->normals.size() != front_count) {
    return;
  }
  const bool has_tex_uv = geo->texture_uvs.size() == front_count;
  const bool has_light_uv = geo->lightmap_uvs.size() == front_count;

  // --- Find boundary edges first (edges used by exactly one front triangle) so
  // we can reserve the exact final index capacity and read the original front
  // triangles in place — no temporary copy, no reallocation invalidation. Keep
  // each boundary edge's directed orientation from its CCW triangle so the wall
  // is wound to face outward.
  struct EdgeInfo {
    int count = 0;
    uint32_t p = 0;
    uint32_t q = 0;
  };
  std::unordered_map<uint64_t, EdgeInfo> edges;
  edges.reserve(front_index_count);
  auto add_edge = [&](uint32_t p, uint32_t q) {
    EdgeInfo& info = edges[EdgeKey(p, q)];
    if (info.count == 0) {
      info.p = p;
      info.q = q;
    }
    ++info.count;
  };
  for (size_t t = 0; t + 2 < front_index_count; t += 3) {
    uint32_t a = geo->indices[t];
    uint32_t b = geo->indices[t + 1];
    uint32_t c = geo->indices[t + 2];
    add_edge(a, b);
    add_edge(b, c);
    add_edge(c, a);
  }
  size_t boundary_count = 0;
  for (const auto& [key, info] : edges) {
    if (info.count == 1) {
      ++boundary_count;
    }
  }

  // Reserve exact capacities: back cap doubles the vertices; indices gain the
  // back cap (front_index_count) plus 2 triangles per boundary edge.
  geo->vertices.reserve(front_count * 2);
  geo->normals.reserve(front_count * 2);
  if (has_tex_uv) {
    geo->texture_uvs.reserve(front_count * 2);
  }
  if (has_light_uv) {
    geo->lightmap_uvs.reserve(front_count * 2);
  }
  geo->indices.reserve(front_index_count * 2 + boundary_count * 6);

  // Centroid of the front surface, used to pull the back rim inward.
  Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
  for (const auto& v : geo->vertices) {
    centroid += v;
  }
  centroid /= static_cast<float>(front_count);

  // --- Back cap: each front vertex pushed along -normal, rim pulled toward the
  // centroid by up to `inset` so the side walls tilt off the neighbours' planes.
  const uint32_t back_base = static_cast<uint32_t>(front_count);
  for (size_t i = 0; i < front_count; ++i) {
    const Eigen::Vector3f& v = geo->vertices[i];
    const Eigen::Vector3f& n = geo->normals[i];

    Eigen::Vector3f inward = centroid - v;
    float dist = inward.norm();
    Eigen::Vector3f pull = Eigen::Vector3f::Zero();
    if (dist > 1e-6f) {
      pull = (inward / dist) * std::min(inset, dist);
    }
    Eigen::Vector3f back = v + pull - n * thickness;

    geo->vertices.push_back(back);
    geo->normals.push_back(-n);
    if (has_tex_uv) {
      geo->texture_uvs.push_back(geo->texture_uvs[i]);
    }
    if (has_light_uv) {
      geo->lightmap_uvs.push_back(geo->lightmap_uvs[i]);
    }
  }

  // --- Back-cap triangles: reversed winding so they face -normal (outward on
  // the back side). The front triangles are read in place; the exact reservation
  // above guarantees these appends never reallocate.
  for (size_t t = 0; t + 2 < front_index_count; t += 3) {
    uint32_t a = geo->indices[t];
    uint32_t b = geo->indices[t + 1];
    uint32_t c = geo->indices[t + 2];
    geo->indices.push_back(back_base + a);
    geo->indices.push_back(back_base + c);
    geo->indices.push_back(back_base + b);
  }

  // --- Side walls along the boundary edges found above.
  for (const auto& [key, info] : edges) {
    if (info.count != 1) {
      continue;  // interior edge — already sealed by neighbouring triangles.
    }
    uint32_t p = info.p;
    uint32_t q = info.q;
    uint32_t bp = back_base + p;
    uint32_t bq = back_base + q;
    // Outward-facing winding (derived for a +normal front / -normal back cap).
    geo->indices.push_back(p);
    geo->indices.push_back(bp);
    geo->indices.push_back(bq);

    geo->indices.push_back(p);
    geo->indices.push_back(bq);
    geo->indices.push_back(q);
  }
}

}  // namespace ioq3_map
