#include "extrude.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace ioq3_map {

namespace {

// Backward-clearance sampling: a small cone about -normal, not just the axis, so
// oblique nearby geometry (e.g. a slanted surface behind the wall) is caught.
constexpr int kConeSamples = 4;
constexpr float kConeHalfAngleRad = 0.26f;  // ~15 degrees

// Fraction each rim vertex is pulled toward the face centroid to obtain an
// interior clearance-sample origin. Rim vertices sit ON the wall's neighbouring
// faces (shared edges/corners), so a backward ray from them would immediately
// hit a neighbour and collapse the thickness to zero. Sampling from the face
// interior measures what is genuinely *behind* the wall, not beside it.
constexpr float kInteriorPull = 0.3f;

// A front-surface edge plus how many triangles use it. Boundary edges (count
// == 1) get a side wall; the directed (p, q) orientation comes from the CCW
// triangle that owns them, so the wall can be wound to face outward.
struct EdgeInfo {
  int count = 0;
  uint32_t p = 0;
  uint32_t q = 0;
};

// Packs an undirected edge (the two endpoint indices, order-independent) into a
// single 64-bit key for hashing.
uint64_t EdgeKey(uint32_t a, uint32_t b) {
  uint32_t lo = a < b ? a : b;
  uint32_t hi = a < b ? b : a;
  return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

// Records directed edge (p, q), keeping the first-seen orientation.
void AddEdge(uint32_t p, uint32_t q,
             std::unordered_map<uint64_t, EdgeInfo>* edges) {
  EdgeInfo& info = (*edges)[EdgeKey(p, q)];
  if (info.count == 0) {
    info.p = p;
    info.q = q;
  }
  ++info.count;
}

// Returns the surface's boundary edges — those used by exactly one front
// triangle — each carrying its directed orientation from that triangle.
std::vector<EdgeInfo> FindBoundaryEdges(const Geometry& geo) {
  const size_t index_count = geo.indices.size();
  std::unordered_map<uint64_t, EdgeInfo> edges;
  edges.reserve(index_count);
  for (size_t t = 0; t + 2 < index_count; t += 3) {
    uint32_t a = geo.indices[t];
    uint32_t b = geo.indices[t + 1];
    uint32_t c = geo.indices[t + 2];
    AddEdge(a, b, &edges);
    AddEdge(b, c, &edges);
    AddEdge(c, a, &edges);
  }

  std::vector<EdgeInfo> boundary;
  for (const auto& [key, info] : edges) {
    if (info.count == 1) {
      boundary.push_back(info);
    }
  }
  return boundary;
}

// Per-boundary-vertex inward direction (unit, in the surface plane) used to inset
// the back rim. For every boundary edge (p, q) — directed so the surface interior
// lies to its left — the in-plane interior normal is faceNormal x edgeDir;
// accumulating these over a vertex's incident boundary edges and renormalising
// yields a direction that pushes each side wall perpendicularly off its own edge.
// Interior vertices stay zero (their back copies are hidden, so no inset needed).
std::vector<Eigen::Vector3f> ComputeBoundaryInward(
    const std::vector<EdgeInfo>& boundary, const Geometry& geo) {
  std::vector<Eigen::Vector3f> inward(geo.vertices.size(),
                                      Eigen::Vector3f::Zero());
  for (const auto& info : boundary) {
    const Eigen::Vector3f& vp = geo.vertices[info.p];
    const Eigen::Vector3f& vq = geo.vertices[info.q];
    Eigen::Vector3f edge_dir = vq - vp;
    // Average the endpoint normals for the local face normal of this edge.
    Eigen::Vector3f face_n = geo.normals[info.p] + geo.normals[info.q];
    Eigen::Vector3f interior = face_n.cross(edge_dir);  // left of (p -> q)
    float len = interior.norm();
    if (len > 1e-6f) {
      interior /= len;
      inward[info.p] += interior;
      inward[info.q] += interior;
    }
  }
  for (auto& dir : inward) {
    float len = dir.norm();
    if (len > 1e-6f) {
      dir /= len;
    }
  }
  return inward;
}

// One uniform shell thickness for the whole (planar) surface. Starts at
// config.thickness and, when a clearance query is supplied, clamps to
// `backward_clearance - clearance_margin`, but never below config.min_thickness
// so the shell is never degenerate. Clearance is measured from face-INTERIOR
// sample points (see kInteriorPull) so shared edges/corners cannot collapse it.
float ComputeShellThickness(const ExtrusionConfig& config, const Geometry& geo,
                            const ClearanceFn& clearance) {
  const float target = config.thickness;
  const float floor = std::min(config.min_thickness, target);
  if (!clearance) {
    return target;
  }

  const size_t n = geo.vertices.size();
  Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
  Eigen::Vector3f normal_sum = Eigen::Vector3f::Zero();
  for (size_t i = 0; i < n; ++i) {
    centroid += geo.vertices[i];
    normal_sum += geo.normals[i];
  }
  centroid /= static_cast<float>(n);
  float nl = normal_sum.norm();
  if (nl < 1e-8f) {
    return target;  // No coherent facing direction; skip clamping.
  }
  const Eigen::Vector3f axis = -(normal_sum / nl);  // "backward"

  // Tangent basis around the backward axis, for the cone.
  Eigen::Vector3f up = std::abs(axis.z()) < 0.9f ? Eigen::Vector3f::UnitZ()
                                                 : Eigen::Vector3f::UnitX();
  Eigen::Vector3f tx = axis.cross(up).normalized();
  Eigen::Vector3f ty = axis.cross(tx).normalized();
  const float cos_h = std::cos(kConeHalfAngleRad);
  const float sin_h = std::sin(kConeHalfAngleRad);

  // Interior sample origins: the centroid plus each rim vertex pulled inward.
  std::vector<Eigen::Vector3f> origins;
  origins.reserve(n + 1);
  origins.push_back(centroid);
  for (size_t i = 0; i < n; ++i) {
    origins.push_back(geo.vertices[i] +
                      kInteriorPull * (centroid - geo.vertices[i]));
  }

  float d = std::numeric_limits<float>::infinity();
  for (const Eigen::Vector3f& origin : origins) {
    d = std::min(d, clearance(origin, axis));
    for (int s = 0; s < kConeSamples; ++s) {
      float phi = (2.0f * std::numbers::pi_v<float> * static_cast<float>(s)) /
                  kConeSamples;
      Eigen::Vector3f dir =
          (cos_h * axis + sin_h * (std::cos(phi) * tx + std::sin(phi) * ty))
              .normalized();
      d = std::min(d, clearance(origin, dir));
    }
  }

  float room = d - config.clearance_margin;
  return std::min(target, std::max(floor, room));
}

}  // namespace

std::optional<Geometry> BuildOccluderShell(const ExtrusionConfig& config,
                                           const Geometry& front,
                                           const ClearanceFn& clearance) {
  if (config.thickness <= 0.0f) {
    return std::nullopt;
  }
  const size_t front_count = front.vertices.size();
  const size_t front_index_count = front.indices.size();
  if (front_count < 3 || front_index_count < 3) {
    return std::nullopt;
  }
  // Normals drive the extrusion direction, so they must be present and aligned
  // with the vertices.
  if (front.normals.size() != front_count) {
    return std::nullopt;
  }

  std::vector<EdgeInfo> boundary = FindBoundaryEdges(front);
  std::vector<Eigen::Vector3f> inward = ComputeBoundaryInward(boundary, front);
  // One thickness for the whole planar surface, clamped by the free space behind
  // it but floored so the shell is never degenerate.
  const float thickness = ComputeShellThickness(config, front, clearance);

  Geometry shell;
  shell.occluder_only = true;
  shell.material_id = front.material_id;
  shell.transform = front.transform;
  // No texture_uvs, no lightmap_uvs: the shell never receives a chart (R4).

  // The shell owns a copy of the front rim (indices [0, front_count)) so its
  // side walls have vertices to attach to; the back cap follows in
  // [front_count, 2*front_count). The front's own triangles are NOT copied —
  // they stay in the visible geometry, which both consumers include anyway.
  shell.vertices = front.vertices;
  shell.normals = front.normals;
  shell.vertices.reserve(front_count * 2);
  shell.normals.reserve(front_count * 2);

  Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
  for (const auto& v : front.vertices) centroid += v;
  centroid /= static_cast<float>(front_count);

  const uint32_t back_base = static_cast<uint32_t>(front_count);
  for (size_t i = 0; i < front_count; ++i) {
    const Eigen::Vector3f& v = front.vertices[i];
    Eigen::Vector3f nrm = front.normals[i];
    float nl = nrm.norm();
    if (nl > 1e-8f) nrm /= nl;

    // Inset is capped by the shell thickness so a clamped-thin shell insets
    // proportionally rather than over-pulling, and by the centroid distance so
    // tiny surfaces can't overshoot/invert.
    float dist_to_centroid = (centroid - v).norm();
    float inset = std::min({config.inset, thickness, dist_to_centroid});
    Eigen::Vector3f pull = inward[i] * inset;
    Eigen::Vector3f back = v + pull - nrm * thickness;

    shell.vertices.push_back(back);
    shell.normals.push_back(-nrm);
  }

  shell.indices.reserve(front_index_count + boundary.size() * 6);

  // Back cap: front triangles with reversed winding so they face -normal.
  for (size_t t = 0; t + 2 < front_index_count; t += 3) {
    uint32_t a = front.indices[t];
    uint32_t b = front.indices[t + 1];
    uint32_t c = front.indices[t + 2];
    shell.indices.push_back(back_base + a);
    shell.indices.push_back(back_base + c);
    shell.indices.push_back(back_base + b);
  }

  // Side walls: two outward-facing triangles per boundary edge, joining the
  // front rim to the back rim.
  for (const auto& info : boundary) {
    uint32_t p = info.p;
    uint32_t q = info.q;
    uint32_t bp = back_base + p;
    uint32_t bq = back_base + q;
    shell.indices.push_back(p);
    shell.indices.push_back(bp);
    shell.indices.push_back(bq);

    shell.indices.push_back(p);
    shell.indices.push_back(bq);
    shell.indices.push_back(q);
  }

  return shell;
}

}  // namespace ioq3_map
