#include "triangulation.h"

namespace ioq3_map {

BSPMesh Triangulate(const BSPPolygon& polygon) {
  BSPMesh mesh;
  mesh.vertices = polygon.vertices;

  const size_t num_verts = polygon.vertices.size();
  if (num_verts < 3) {
    return mesh;
  }

  // Create a triangle fan: (0, 1, 2), (0, 2, 3), ...
  for (size_t i = 1; i < num_verts - 1; ++i) {
    mesh.indices.push_back(0);
    mesh.indices.push_back(static_cast<int>(i));
    mesh.indices.push_back(static_cast<int>(i + 1));
  }

  return mesh;
}

namespace {

// Interpolates between two vertices a and b by t (0.0 to 1.0).
vertex_t Lerp(const vertex_t& a, const vertex_t& b, float t) {
  vertex_t v;
  v.xyz = a.xyz + (b.xyz - a.xyz) * t;
  v.st = a.st + (b.st - a.st) * t;
  v.lightmap = a.lightmap + (b.lightmap - a.lightmap) * t;
  v.normal = (a.normal + (b.normal - a.normal) * t).normalized();
  for (int i = 0; i < 4; ++i) {
    v.color[i] =
        static_cast<uint8_t>(a.color[i] + (b.color[i] - a.color[i]) * t);
  }
  return v;
}

// Evaluates a quadratic Bezier curve at t using control points p0, p1, p2.
vertex_t Bezier(const vertex_t& p0, const vertex_t& p1, const vertex_t& p2,
                float t) {
  float b0 = (1.0f - t) * (1.0f - t);
  float b1 = 2.0f * (1.0f - t) * t;
  float b2 = t * t;

  vertex_t v;
  v.xyz = p0.xyz * b0 + p1.xyz * b1 + p2.xyz * b2;
  v.st = p0.st * b0 + p1.st * b1 + p2.st * b2;
  v.lightmap = p0.lightmap * b0 + p1.lightmap * b1 + p2.lightmap * b2;
  // Normals shouldn't be linearly interpolated like this for a curved surface,
  // but it's a common approximation. Re-normalizing is crucial.
  // Ideally we would compute the tangent and bitangent derivatives.
  v.normal = (p0.normal * b0 + p1.normal * b1 + p2.normal * b2).normalized();

  for (int i = 0; i < 4; ++i) {
    v.color[i] = static_cast<uint8_t>(p0.color[i] * b0 + p1.color[i] * b1 +
                                      p2.color[i] * b2);
  }
  return v;
}

}  // namespace

BSPMesh Triangulate(const BSPPatch& patch, int subdivisions) {
  BSPMesh mesh;

  // A patch of size WxH must be odd dimensions and >= 3.
  // It effectively consists of a grid of (W-1)/2 x (H-1)/2 sub-patches of 3x3
  // control points.
  if (patch.width < 3 || patch.height < 3 || patch.width % 2 == 0 ||
      patch.height % 2 == 0) {
    return mesh;  // Invalid patch dimensions
  }

  const int sub_patches_x = (patch.width - 1) / 2;
  const int sub_patches_y = (patch.height - 1) / 2;

  // We will generate the entire grid of vertices first.
  // The total grid size will be:
  // Width:  sub_patches_x * subdivisions + 1
  // Height: sub_patches_y * subdivisions + 1

  const int grid_width = sub_patches_x * subdivisions + 1;
  const int grid_height = sub_patches_y * subdivisions + 1;

  mesh.vertices.resize(grid_width * grid_height);

  for (int py = 0; py < sub_patches_y; ++py) {
    for (int px = 0; px < sub_patches_x; ++px) {
      // Control points for this 3x3 sub-patch
      // The top-left corner of this sub-patch in the original control grid is
      // at (px * 2, py * 2).
      const int c_base_x = px * 2;
      const int c_base_y = py * 2;

      // Fetch the 9 control points
      const vertex_t* cp[3][3];
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          cp[r][c] = &patch.control_points[(c_base_y + r) * patch.width +
                                           (c_base_x + c)];
        }
      }

      // Tessellate this sub-patch
      for (int v_y = 0; v_y <= subdivisions; ++v_y) {
        for (int v_x = 0; v_x <= subdivisions; ++v_x) {
          // Local t coordinates (0.0 to 1.0)
          float t_x = static_cast<float>(v_x) / subdivisions;
          float t_y = static_cast<float>(v_y) / subdivisions;

          // Interpolate horizontally first to get 3 vertical points
          vertex_t temp[3];
          for (int r = 0; r < 3; ++r) {
            temp[r] = Bezier(*cp[r][0], *cp[r][1], *cp[r][2], t_x);
          }

          // Interpolate vertically to get final point
          vertex_t final_vert = Bezier(temp[0], temp[1], temp[2], t_y);

          // Calculate index in the global vertex grid
          // Base offset for this sub-patch is (px * subdivisions, py *
          // subdivisions)
          int global_x = px * subdivisions + v_x;
          int global_y = py * subdivisions + v_y;

          // Handle overlaps: neighboring sub-patches share edge vertices.
          // The calculation above is consistent; the 'last' vertex of patch N
          // (v_x = subdivisions) lands on the same global coordinate as the
          // 'first' vertex of patch N+1 (v_x = 0). We overwrite, which is fine
          // (should be identical).
          mesh.vertices[global_y * grid_width + global_x] = final_vert;
        }
      }
    }
  }

  // Generate Indices (Grid triangulation)
  for (int y = 0; y < grid_height - 1; ++y) {
    for (int x = 0; x < grid_width - 1; ++x) {
      // Quad: (x, y), (x+1, y), (x+1, y+1), (x, y+1)
      int v0 = y * grid_width + x;
      int v1 = y * grid_width + (x + 1);
      int v2 = (y + 1) * grid_width + (x + 1);
      int v3 = (y + 1) * grid_width + x;

      // To keep counter-clockwise ordering within Q3 structures.
      // Triangle 1: v0, v2, v1
      mesh.indices.push_back(v0);
      mesh.indices.push_back(v2);
      mesh.indices.push_back(v1);

      // Triangle 2: v0, v3, v2
      mesh.indices.push_back(v0);
      mesh.indices.push_back(v3);
      mesh.indices.push_back(v2);
    }
  }

  return mesh;
}

}  // namespace ioq3_map
