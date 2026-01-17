/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifndef TR_LOCAL_H
#define TR_LOCAL_H

#include "qfiles.h"

#define GLE(ret, name, ...) extern name##proc *qgl##name;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_3_0_PROCS;
#undef GLE

#define GL_INDEX_TYPE GL_UNSIGNED_INT
typedef unsigned int glIndex_t;

// 14 bits
// can't be increased without changing bit packing for drawsurfs
// see QSORT_SHADERNUM_SHIFT
#define SHADERNUM_BITS 14
#define MAX_SHADERS (1 << SHADERNUM_BITS)

typedef struct dlight_s {
  vec3_t origin;
  vec3_t color;  // range from 0.0 to 1.0, should be color normalized
  float radius;

  vec3_t transformed;  // origin in local coordinate system
  int additive;        // texture detail is lost tho when the lightmap is dark
} dlight_t;

// a trRefEntity_t has all the information passed in by
// the client game, as well as some locally derived info
typedef struct {
  refEntity_t e;

  float axisLength;  // compensate for non-normalized axis

  qboolean needDlights;  // true for bmodels that touch a dlight
  qboolean lightingCalculated;
  vec3_t lightDir;      // normalized direction towards light
  vec3_t ambientLight;  // color normalized to 0-255
  int ambientLightInt;  // 32 bit rgba packed
  vec3_t directedLight;
} trRefEntity_t;

typedef struct {
  vec3_t origin;      // in world coordinates
  vec3_t axis[3];     // orientation in world
  vec3_t viewOrigin;  // viewParms->or.origin in local coordinates
  float modelMatrix[16];
} orientationr_t;

//===============================================================================

typedef enum {
  SS_BAD,
  SS_PORTAL,       // mirrors, portals, viewscreens
  SS_ENVIRONMENT,  // sky box
  SS_OPAQUE,       // opaque

  SS_DECAL,        // scorch marks, etc.
  SS_SEE_THROUGH,  // ladders, grates, grills that may have small blended edges
                   // in addition to alpha test
  SS_BANNER,

  SS_FOG,

  SS_UNDERWATER,  // for items that should be drawn in front of the water plane

  SS_BLEND0,  // regular transparency and filters
  SS_BLEND1,  // generally only used for additive type effects
  SS_BLEND2,
  SS_BLEND3,

  SS_BLEND6,
  SS_STENCIL_SHADOW,
  SS_ALMOST_NEAREST,  // gun smoke puffs

  SS_NEAREST  // blood blobs
} shaderSort_t;

#define MAX_SHADER_STAGES 8

typedef enum {
  GF_NONE,

  GF_SIN,
  GF_SQUARE,
  GF_TRIANGLE,
  GF_SAWTOOTH,
  GF_INVERSE_SAWTOOTH,

  GF_NOISE

} genFunc_t;

typedef enum {
  DEFORM_NONE,
  DEFORM_WAVE,
  DEFORM_NORMALS,
  DEFORM_BULGE,
  DEFORM_MOVE,
  DEFORM_PROJECTION_SHADOW,
  DEFORM_AUTOSPRITE,
  DEFORM_AUTOSPRITE2,
  DEFORM_TEXT0,
  DEFORM_TEXT1,
  DEFORM_TEXT2,
  DEFORM_TEXT3,
  DEFORM_TEXT4,
  DEFORM_TEXT5,
  DEFORM_TEXT6,
  DEFORM_TEXT7
} deform_t;

typedef enum {
  AGEN_IDENTITY,
  AGEN_SKIP,
  AGEN_ENTITY,
  AGEN_ONE_MINUS_ENTITY,
  AGEN_VERTEX,
  AGEN_ONE_MINUS_VERTEX,
  AGEN_LIGHTING_SPECULAR,
  AGEN_WAVEFORM,
  AGEN_PORTAL,
  AGEN_CONST
} alphaGen_t;

typedef enum {
  CGEN_BAD,
  CGEN_IDENTITY_LIGHTING,  // tr.identityLight
  CGEN_IDENTITY,           // always (1,1,1,1)
  CGEN_ENTITY,             // grabbed from entity's modulate field
  CGEN_ONE_MINUS_ENTITY,   // grabbed from 1 - entity.modulate
  CGEN_EXACT_VERTEX,       // tess.vertexColors
  CGEN_VERTEX,             // tess.vertexColors * tr.identityLight
  CGEN_ONE_MINUS_VERTEX,
  CGEN_WAVEFORM,  // programmatically generated
  CGEN_LIGHTING_DIFFUSE,
  CGEN_FOG,   // standard fog
  CGEN_CONST  // fixed color
} colorGen_t;

typedef enum {
  TCGEN_BAD,
  TCGEN_IDENTITY,  // clear to 0,0
  TCGEN_LIGHTMAP,
  TCGEN_TEXTURE,
  TCGEN_ENVIRONMENT_MAPPED,
  TCGEN_FOG,
  TCGEN_VECTOR  // S and T from world coordinates
} texCoordGen_t;

typedef enum {
  ACFF_NONE,
  ACFF_MODULATE_RGB,
  ACFF_MODULATE_RGBA,
  ACFF_MODULATE_ALPHA
} acff_t;

typedef struct {
  genFunc_t func;

  float base;
  float amplitude;
  float phase;
  float frequency;
} waveForm_t;

#define TR_MAX_TEXMODS 4

typedef enum {
  TMOD_NONE,
  TMOD_TRANSFORM,
  TMOD_TURBULENT,
  TMOD_SCROLL,
  TMOD_SCALE,
  TMOD_STRETCH,
  TMOD_ROTATE,
  TMOD_ENTITY_TRANSLATE
} texMod_t;

#define MAX_SHADER_DEFORMS 3
typedef struct {
  deform_t deformation;  // vertex coordinate modification type

  vec3_t moveVector;
  waveForm_t deformationWave;
  float deformationSpread;

  float bulgeWidth;
  float bulgeHeight;
  float bulgeSpeed;
} deformStage_t;

typedef struct {
  texMod_t type;

  // used for TMOD_TURBULENT and TMOD_STRETCH
  waveForm_t wave;

  // used for TMOD_TRANSFORM
  float matrix[2][2];  // s' = s * m[0][0] + t * m[1][0] + trans[0]
  float translate[2];  // t' = s * m[0][1] + t * m[0][1] + trans[1]

  // used for TMOD_SCALE
  float scale[2];  // s *= scale[0]
                   // t *= scale[1]

  // used for TMOD_SCROLL
  float scroll[2];  // s' = s + scroll[0] * time
                    // t' = t + scroll[1] * time

  // + = clockwise
  // - = counterclockwise
  float rotateSpeed;

} texModInfo_t;

#define MAX_IMAGE_ANIMATIONS 8

typedef struct {
  image_t *image[MAX_IMAGE_ANIMATIONS];
  int numImageAnimations;
  float imageAnimationSpeed;

  texCoordGen_t tcGen;
  vec3_t tcGenVectors[2];

  int numTexMods;
  texModInfo_t *texMods;

  int videoMapHandle;
  qboolean isLightmap;
  qboolean isVideoMap;
} textureBundle_t;

#define NUM_TEXTURE_BUNDLES 2

typedef struct {
  qboolean active;

  textureBundle_t bundle[NUM_TEXTURE_BUNDLES];

  waveForm_t rgbWave;
  colorGen_t rgbGen;

  waveForm_t alphaWave;
  alphaGen_t alphaGen;

  byte constantColor[4];  // for CGEN_CONST and AGEN_CONST

  unsigned stateBits;  // GLS_xxxx mask

  acff_t adjustColorsForFog;

  qboolean isDetail;
} shaderStage_t;

struct shaderCommands_s;

typedef enum { CT_FRONT_SIDED, CT_BACK_SIDED, CT_TWO_SIDED } cullType_t;

typedef enum {
  FP_NONE,   // surface is translucent and will just be adjusted properly
  FP_EQUAL,  // surface is opaque but possibly alpha tested
  FP_LE      // surface is trnaslucent, but still needs a fog pass (fog surface)
} fogPass_t;

typedef struct {
  float cloudHeight;
  image_t *outerbox[6], *innerbox[6];
} skyParms_t;

typedef struct {
  vec3_t color;
  float depthForOpaque;
} fogParms_t;

typedef struct shader_s {
  char name[MAX_QPATH];  // game path, including extension
  int lightmapIndex;  // for a shader to match, both name and lightmapIndex must
                      // match

  int index;        // this shader == tr.shaders[index]
  int sortedIndex;  // this shader == tr.sortedShaders[sortedIndex]

  float sort;  // lower numbered shaders draw before higher numbered

  qboolean defaultShader;  // we want to return index 0 if the shader failed to
                           // load for some reason, but R_FindShader should
                           // still keep a name allocated for it, so if
                           // something calls RE_RegisterShader again with
                           // the same name, we don't try looking for it again

  qboolean explicitlyDefined;  // found in a .shader file

  int surfaceFlags;  // if explicitlyDefined, this will have SURF_* flags
  int contentFlags;

  qboolean entityMergable;  // merge across entites optimizable (smoke, blood)

  qboolean isSky;
  skyParms_t sky;
  fogParms_t fogParms;

  float portalRange;  // distance to fog out at

  int multitextureEnv;  // 0, GL_MODULATE, GL_ADD (FIXME: put in stage)

  cullType_t cullType;     // CT_FRONT_SIDED, CT_BACK_SIDED, or CT_TWO_SIDED
  qboolean polygonOffset;  // set for decals and other items that must be offset
  qboolean noMipMaps;      // for console fonts, 2D elements, etc.
  qboolean noPicMip;       // for images that must always be full resolution

  fogPass_t fogPass;  // draw a blended pass, possibly with depth test equals

  qboolean needsNormal;  // not all shaders will need all data to be gathered
  qboolean needsST1;
  qboolean needsST2;
  qboolean needsColor;

  int numDeforms;
  deformStage_t deforms[MAX_SHADER_DEFORMS];

  int numUnfoggedPasses;
  shaderStage_t *stages[MAX_SHADER_STAGES];

  void (*optimalStageIteratorFunc)(void);

  double clampTime;   // time this shader is clamped to
  double timeOffset;  // current time offset for this shader

  struct shader_s *remappedShader;  // current shader this one is remapped too

  struct shader_s *next;
} shader_t;

// trRefdef_t holds everything that comes in refdef_t,
// as well as the locally generated scene information
typedef struct {
  int x, y, width, height;
  float fov_x, fov_y;
  vec3_t vieworg;
  vec3_t viewaxis[3];  // transformation matrix

  stereoFrame_t stereoFrame;

  int time;  // time in milliseconds for shader effects and other time dependent
             // rendering issues
  int rdflags;  // RDF_NOWORLDMODEL, etc

  // 1 bits will prevent the associated area from rendering at all
  byte areamask[MAX_MAP_AREA_BYTES];
  qboolean areamaskModified;  // qtrue if areamask changed since last scene

  double floatTime;  // tr.refdef.time / 1000.0

  // text messages for deform text shaders
  char text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];

  int num_entities;
  trRefEntity_t *entities;

  int num_dlights;
  struct dlight_s *dlights;

  int numPolys;
  struct srfPoly_s *polys;

  int numDrawSurfs;
  struct drawSurf_s *drawSurfs;

} trRefdef_t;

//=================================================================================

// max surfaces per-skin
// This is an arbitry limit. Vanilla Q3 only supported 32 surfaces in skins but
// failed to enforce the maximum limit when reading skin files. It was possile
// to use more than 32 surfaces which accessed out of bounds memory past end of
// skin->surfaces hunk block.
#define MAX_SKIN_SURFACES 256

// skins allow models to be retextured without modifying the model file
typedef struct {
  char name[MAX_QPATH];
  shader_t *shader;
} skinSurface_t;

typedef struct skin_s {
  char name[MAX_QPATH];  // game path, including extension
  int numSurfaces;
  skinSurface_t *surfaces;  // dynamically allocated array of surfaces
} skin_t;

typedef struct {
  int originalBrushNumber;
  vec3_t bounds[2];

  unsigned colorInt;  // in packed byte format
  float tcScale;      // texture coordinate vector scales
  fogParms_t parms;

  // for clipping distance in fog when outside
  qboolean hasSurface;
  float surface[4];
} fog_t;

typedef struct {
  orientationr_t or ;
  orientationr_t world;
  vec3_t pvsOrigin;      // may be different than or.origin for portals
  qboolean isPortal;     // true if this view is through a portal
  qboolean isMirror;     // the portal is a mirror, invert the face culling
  int frameSceneNum;     // copied from tr.frameSceneNum
  int frameCount;        // copied from tr.frameCount
  cplane_t portalPlane;  // clip anything behind this if mirroring
  int viewportX, viewportY, viewportWidth, viewportHeight;
  float fovX, fovY;
  float projectionMatrix[16];
  cplane_t frustum[4];
  vec3_t visBounds[2];
  float zFar;
  stereoFrame_t stereoFrame;
} viewParms_t;

/*
==============================================================================

SURFACES

==============================================================================
*/

// any changes in surfaceType must be mirrored in rb_surfaceTable[]
typedef enum {
  SF_BAD,
  SF_SKIP,  // ignore
  SF_FACE,
  SF_GRID,
  SF_TRIANGLES,
  SF_POLY,
  SF_MD3,
  SF_MDR,
  SF_IQM,
  SF_FLARE,
  SF_ENTITY,  // beams, rails, lightning, etc that can be determined by entity

  SF_NUM_SURFACE_TYPES,
  SF_MAX = 0x7fffffff  // ensures that sizeof( surfaceType_t ) == sizeof( int )
} surfaceType_t;

typedef struct drawSurf_s {
  unsigned sort;           // bit combination for fast compares
  surfaceType_t *surface;  // any of surface*_t
} drawSurf_t;

#define MAX_FACE_POINTS 64

#define MAX_PATCH_SIZE 32  // max dimensions of a patch mesh in map file
#define MAX_GRID_SIZE 65   // max dimensions of a grid mesh in memory

// when cgame directly specifies a polygon, it becomes a srfPoly_t
// as soon as it is called
typedef struct srfPoly_s {
  surfaceType_t surfaceType;
  qhandle_t hShader;
  int fogIndex;
  int numVerts;
  polyVert_t *verts;
} srfPoly_t;

typedef struct srfFlare_s {
  surfaceType_t surfaceType;
  vec3_t origin;
  vec3_t normal;
  vec3_t color;
} srfFlare_t;

typedef struct srfGridMesh_s {
  surfaceType_t surfaceType;

  // dynamic lighting information
  int dlightBits;

  // culling information
  vec3_t meshBounds[2];
  vec3_t localOrigin;
  float meshRadius;

  // lod information, which may be different
  // than the culling information to allow for
  // groups of curves that LOD as a unit
  vec3_t lodOrigin;
  float lodRadius;
  int lodFixed;
  int lodStitched;

  // vertexes
  int width, height;
  float *widthLodError;
  float *heightLodError;
  drawVert_t verts[1];  // variable sized
} srfGridMesh_t;

#define VERTEXSIZE 8
typedef struct {
  surfaceType_t surfaceType;
  cplane_t plane;

  // dynamic lighting information
  int dlightBits;

  // triangle definitions (no normals at points)
  int numPoints;
  int numIndices;
  int ofsIndices;
  float points[1][VERTEXSIZE];  // variable sized
                                // there is a variable length list of indices
                                // here also
} srfSurfaceFace_t;

// misc_models in maps are turned into direct geometry by q3map
typedef struct {
  surfaceType_t surfaceType;

  // dynamic lighting information
  int dlightBits;

  // culling information (FIXME: use this!)
  vec3_t bounds[2];
  vec3_t localOrigin;
  float radius;

  // triangle definitions
  int numIndexes;
  int *indexes;

  int numVerts;
  drawVert_t *verts;
} srfTriangles_t;

typedef struct {
  vec3_t translate;
  quat_t rotate;
  vec3_t scale;
} iqmTransform_t;

// inter-quake-model
typedef struct {
  int num_vertexes;
  int num_triangles;
  int num_frames;
  int num_surfaces;
  int num_joints;
  int num_poses;
  struct srfIQModel_s *surfaces;

  int *triangles;

  // vertex arrays
  float *positions;
  float *texcoords;
  float *normals;
  float *tangents;
  byte *colors;
  int *influences;  // [num_vertexes] indexes into influenceBlendVertexes

  // unique list of vertex blend indexes/weights for faster CPU vertex skinning
  byte *influenceBlendIndexes;  // [num_influences]
  union {
    float *f;
    byte *b;
  } influenceBlendWeights;  // [num_influences]

  // depending upon the exporter, blend indices and weights might be int/float
  // as opposed to the recommended byte/byte, for example Noesis exports
  // int/float whereas the official IQM tool exports byte/byte
  int blendWeightsType;  // IQM_UBYTE or IQM_FLOAT

  char *jointNames;
  int *jointParents;
  float *bindJoints;      // [num_joints * 12]
  float *invBindJoints;   // [num_joints * 12]
  iqmTransform_t *poses;  // [num_frames * num_poses]
  float *bounds;
} iqmData_t;

// inter-quake-model surface
typedef struct srfIQModel_s {
  surfaceType_t surfaceType;
  char name[MAX_QPATH];
  shader_t *shader;
  iqmData_t *data;
  int first_vertex, num_vertexes;
  int first_triangle, num_triangles;
  int first_influence, num_influences;
} srfIQModel_t;

extern void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])(void *);

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

//
// in memory representation
//

#define SIDE_FRONT 0
#define SIDE_BACK 1
#define SIDE_ON 2

typedef struct msurface_s {
  int viewCount;  // if == tr.viewCount, already added
  struct shader_s *shader;
  int fogIndex;

  surfaceType_t *data;  // any of srf*_t
} msurface_t;

#define CONTENTS_NODE -1
typedef struct mnode_s {
  // common with leaf and node
  int contents;       // -1 for nodes, to differentiate from leafs
  int visframe;       // node needs to be traversed if current
  vec3_t mins, maxs;  // for bounding box culling
  struct mnode_s *parent;

  // node specific
  cplane_t *plane;
  struct mnode_s *children[2];

  // leaf specific
  int cluster;
  int area;

  msurface_t **firstmarksurface;
  int nummarksurfaces;
} mnode_t;

typedef struct {
  vec3_t bounds[2];  // for culling
  msurface_t *firstSurface;
  int numSurfaces;
} bmodel_t;

typedef struct {
  char name[MAX_QPATH];      // ie: maps/tim_dm2.bsp
  char baseName[MAX_QPATH];  // ie: tim_dm2

  int dataSize;

  int numShaders;
  dshader_t *shaders;

  bmodel_t *bmodels;

  int numplanes;
  cplane_t *planes;

  int numnodes;  // includes leafs
  int numDecisionNodes;
  mnode_t *nodes;

  int numsurfaces;
  msurface_t *surfaces;

  int nummarksurfaces;
  msurface_t **marksurfaces;

  int numfogs;
  fog_t *fogs;

  vec3_t lightGridOrigin;
  vec3_t lightGridSize;
  vec3_t lightGridInverseSize;
  int lightGridBounds[3];
  byte *lightGridData;

  int numClusters;
  int clusterBytes;
  const byte *vis;  // may be passed in by CM_LoadMap to save space

  byte *novis;  // clusterBytes of 0xff

  char *entityString;
  char *entityParsePoint;
} world_t;

//======================================================================

typedef enum { MOD_BAD, MOD_BRUSH, MOD_MESH, MOD_MDR, MOD_IQM } modtype_t;

typedef struct model_s {
  char name[MAX_QPATH];
  modtype_t type;
  int index;  // model = tr.models[model->index]

  int dataSize;                    // just for listing purposes
  bmodel_t *bmodel;                // only if type == MOD_BRUSH
  md3Header_t *md3[MD3_MAX_LODS];  // only if type == MOD_MESH
  void *modelData;                 // only if type == (MOD_MDR | MOD_IQM)

  int numLods;
} model_t;

#endif  // TR_LOCAL_H
