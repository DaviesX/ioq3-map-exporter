#include "extrude.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ioq3_map {

namespace {

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

}  // namespace

std::optional<Geometry> BuildOccluderShell(const ExtrusionConfig& config,
                                           const Geometry& front,
                                           const ClearanceFn& clearance) {
  if (config.thickness <= 0.0f) {
    return std::nullopt;
  }
  const size_t n = front.vertices.size();
  const size_t front_index_count = front.indices.size();
  if (n < 3 || front_index_count < 3) {
    return std::nullopt;
  }
  // Normals drive the extrusion direction, so they must be present and aligned
  // with the vertices.
  if (front.normals.size() != n) {
    return std::nullopt;
  }

  std::vector<EdgeInfo> boundary = FindBoundaryEdges(front);

  const float target = config.thickness;
  // Below this the shell would be a degenerate sliver; such a surface has no room
  // to be shelled, so it is skipped entirely rather than floored to a fudge.
  constexpr float kMinShellDepth = 1e-3f;

  // Pass 1: per-vertex extrusion depth -> provisional back cap. A ray straight
  // back (-normal) from each vertex says how far the wall may extrude before
  // something behind it; the depth is clamped to that (minus the margin). If any
  // vertex has no room to clear the margin, the whole surface is flush against
  // geometry (which already occludes) and is not shelled.
  std::vector<Eigen::Vector3f> unit_normals(n);
  std::vector<Eigen::Vector3f> back(n);
  std::vector<float> depth(n);
  for (size_t i = 0; i < n; ++i) {
    Eigen::Vector3f nrm = front.normals[i];
    float nl = nrm.norm();
    if (nl > 1e-8f) {
      nrm /= nl;
    } else {
      nrm.setZero();
    }
    unit_normals[i] = nrm;

    float t = target;
    if (clearance && nl > 1e-8f) {
      ClearanceHit hit = clearance(front.vertices[i], -nrm);
      if (std::isfinite(hit.distance)) {
        float room = hit.distance - config.clearance_margin;
        if (room < kMinShellDepth) {
          return std::nullopt;  // no room here -> do not shell this surface
        }
        t = std::min(target, room);
      }
    }
    depth[i] = t;
    back[i] = front.vertices[i] - nrm * t;
  }

  // Pass 2: conform the back cap to inward-leaning neighbours (e.g. a
  // trapezoidal prism's slanted sides). From each back vertex, cast toward the
  // back-cap centroid; if the ray enters a wall from outside, the vertex has
  // poked through that wall, so pull it in onto it. A right prism's back
  // vertices are inside the solid, so the ray leaves it (enters no wall from
  // outside) and nothing moves.
  //
  // The correction is bounded to a multiple of the extrusion depth. A genuine
  // slant poke-through puts the entering wall only ~slope*depth away, so a far
  // "entering" hit is spurious — the single-ray inside/outside test is fooled in
  // concave regions of the non-convex BSP, where a back vertex sees a distant
  // wall's outside face. Capping keeps the shell local instead of yanking a
  // vertex across the level.
  if (clearance) {
    constexpr float kMaxConformDepths = 4.0f;
    // Land the vertex this far *past* the wall (deeper into the solid) so the
    // back cap is not coplanar with the slanted face — coplanar occluders
    // z-fight, which is hard for artists to inspect in Blender.
    constexpr float kInwardMargin = 1e-3f;
    Eigen::Vector3f back_centroid = Eigen::Vector3f::Zero();
    for (const auto& b : back) back_centroid += b;
    back_centroid /= static_cast<float>(n);

    for (size_t i = 0; i < n; ++i) {
      Eigen::Vector3f dir = back_centroid - back[i];
      float len = dir.norm();
      if (len < 1e-6f) continue;
      dir /= len;
      ClearanceHit hit = clearance(back[i], dir);
      // hit.normal points out of the solid (front winding); a negative dot means
      // the ray is going into the wall's outside face, i.e. the vertex is out.
      if (std::isfinite(hit.distance) && hit.normal.dot(dir) < 0.0f &&
          hit.distance <= kMaxConformDepths * depth[i]) {
        back[i] += dir * std::min(hit.distance + kInwardMargin, len);
      }
    }
  }

  Geometry shell;
  shell.occluder_only = true;
  shell.material_id = front.material_id;
  shell.transform = front.transform;
  // No texture_uvs, no lightmap_uvs: the shell never receives a chart (R4).

  // The shell owns a copy of the front rim (indices [0, n)) so its side walls
  // have vertices to attach to; the conformed back cap follows in [n, 2n). The
  // front's own triangles are NOT copied — they stay in the visible geometry,
  // which both consumers include anyway.
  shell.vertices.reserve(n * 2);
  shell.vertices.insert(shell.vertices.end(), front.vertices.begin(),
                        front.vertices.end());
  shell.normals.reserve(n * 2);
  shell.normals.insert(shell.normals.end(), front.normals.begin(),
                       front.normals.end());
  for (size_t i = 0; i < n; ++i) {
    shell.vertices.push_back(back[i]);
    shell.normals.push_back(-unit_normals[i]);
  }

  const uint32_t back_base = static_cast<uint32_t>(n);
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
