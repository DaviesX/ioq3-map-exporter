# Project: Quake 3 Map to glTF Exporter (ioq3-map-exporter)

## 1. Mission Overview

**Objective:** Develop a standalone C++ console application to extract static geometry (Worldspawn + Movers), light sources and material copies from Quake 3 `.pk3` (BSP) archives and export them to the glTF 2.0 format.

**Input:**

* `--base-path`: A base path to a folder of Quake 3 `.pk3` (BSP) archives.
* `--map`: A map name which references a `.bsp` file in the `maps` directory of a `.pk3` archive.
* `--output`: The output directory for the glTF files.

**Output:**

* `scene.gltf`: The geometry and scene hierarchy.
* `*.tga/*.jpg/*.png`: The texture copies.
* `manifest.json`: A mapping of Q3 Face Indices to glTF Primitive Indices.

Dependencies:
* CMake
* Eigen3
* miniz
* zlib
* tinygltf
* nlohmann/json
* glog
* gflags
* googletest

Coding Style: google c++ style guide
Building and testing:
```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build -S . && cmake --build build --parallel 12 && ./build/ioq3_map_exporter
```
Expected binaries:
* `build/libioq3_map.so`
* `build/ioq3_map_exporter`
* `build/ioq3_map_exporter_test`

## 2. Architecture: The Antigravity Pipeline

The application will follow a strict unidirectional data flow composed of three distinct modules.

## Module A: The Loader (Data In)

* **Responsibility:** Handling filesystem operations and binary parsing.
* **BSP Specs:**: `docs/Unofficial Quake 3 Map Specs.html`, `docs/qfiles.h` and `docs/tr_local.h`.
* **Test Data:**
  - Example `.pk3` file: `data/pak0.pk3`.
  - Example `.bsp` file: `maps/q3dm1.bsp`.
* **Key Components:**
* **PK3 Interface:** Treating `.pk3` files as ZIP archives. (Recommendation: Use `miniz` or `zlib`).
* **BSP Parser:** Reading the specific IBSP lumps (Header `IBSP`, Version `0x2E`).
* **Lump Extraction:** Specifically extracting:
* `Lump 0 (Entities)`: Extract the sun light source. Example:
```json
{
  "classname" "worldspawn"
  "_sunlight" "250"             // Intensity of the sun/sky light
  "_sun_mangle" "90 -45 0"      // Direction of sun: Yaw, Pitch, Roll
  "_sunlight_color" "255 255 220" // Color of the sunlight
}
```
* `Lump 1 (Textures)`: Surface flags and names (Extract texture copies).
* `Lump 7 (Models)`: To distinguish Worldspawn (Model 0) from Movers (Doors/Plats).
* `Lump 10 (Vertexes)`: Position, Normal, TexCoords, Colors (Extract geometry).
* `Lump 11 (MeshVerts)`: Index lists for triangulation (Extract geometry).
* `Lump 13 (Faces)`: Polygon and Mesh type faces (Extract geometry). We will need to tessellate Bezier patches into triangle meshes. (Reference: `docs/tr_curve.c`)


## Module B: The Converter (Logic Core)

* **Responsibility:** Transformation of raw Q3 structures into generic 3D geometry.
* **Conversions:**
  - **Coordinate System:** Convert Q3 (Z-up, X-forward) to glTF (Y-up, Z-forward).
  - **Matrix:** Rotate -90° on X-axis.
  - **Light Source:** Convert the sun light source into a glTF punctual directional light source.
  - **Units:** from inches to meters.


* **Geometry Processing:**
  - *Polygons (Type 1) & Meshes (Type 3):* Resolve `MeshVerts` offsets to absolute vertex indices.
  - *Patches (Type 2):* (Optional Phase 2) Tessellate Bezier patches into triangles. For Phase 1, these can be skipped or exported as control points.
  - *Billboards (Type 4):* Export as quad geometry centered on the vertex.


* **Texture Paths:** Strip Q3 specific extensions (`.tga`, `.jpg`) and remap shader paths to generic material names.



## Module C: The Writer (Data Out)

* **Responsibility:** Serialization of the intermediate data to disk.
* **Key Components:**
  - **Buffer Management:** Writing binary `.bin` chunks for vertex attributes (POS, NORM, TEXCOORD_0).
  - **JSON Construction:** Generating the glTF node hierarchy. (Recommendation: `tinygltf` or `nlohmann/json`).
  - **Manifest Generator:** Parallel writing of the `manifest.json` linking `BSP_Face_ID` -> `glTF_Mesh_Primitive_ID`.



## 3. Data Structures (Intermediate)
* ioq3 data structures are documented in the `docs` directory.
* Useful intermediate data structures have been populated in the `src` directory.

## 4. Implementation Plan

## Phase 1: The Skeleton

1. **Setup:** Initialize a CMake project with `miniz.c` and `tinygltf.h`.
2. **PK3 Reader:** Implement a class to open a `.pk3` and read a file into a memory buffer by name (e.g., `maps/q3dm6.bsp`).
3. **Header Check:** Verify `IBSP` magic number and `0x2E` version.

## Phase 2: Geometry Extraction

1. **Lump Parsing:** Define C structs for `dface_t`, `dvertex_t`, etc., exactly matching the Q3 specs.
2. **Triangulation:** Write a loop that iterates through all `Faces`.
* If `type == 1` (Polygon) or `type == 3` (Mesh): Read `n_meshverts` from the `MeshVerts` lump starting at `meshvert` offset.
* Store these as a `RenderBatch`.

## Phase 3: Material/Shader Extraction

 * TBD

## Phase 4: The glTF Writer

1. **Buffer Creation:** Interleave or pack vertex data into a binary blob.
2. **Node Hierarchy:** Create a single root node "Worldspawn".
3. **Mesh Generation:** Create one glTF `mesh` per material or one monolithic mesh with multiple `primitives`. *Recommendation: One mesh with multiple primitives is more efficient for glTF.*
4. **Export:** Write `scene.gltf` and `scene.bin`.

## Phase 5: Metadata & Manifest

1. **Manifest Logic:** During the `RenderBatch` creation loop, record the mapping.
2. **JSON Output:**
```json
{
  "face_mapping": [
    { "bsp_face_index": 0, "gltf_primitive_index": 0, "material": "textures/base_wall/concrete" },
    ...
  ]
}
```
