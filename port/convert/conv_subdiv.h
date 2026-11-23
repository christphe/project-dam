/*
 * conv_subdiv.h -- character-model subdivision for the first-launch converter.
 *
 * Pure code (libc + libm, no I/O), so tests/test_subdiv.c links it alone.
 *   - mesh level: PN-triangle edge midpoints, 1 triangle -> up to 4; only head-region
 *     triangles are split, an edge shared with a body triangle or crossing matrices is not
 *   - model level: rewrites a decompressed N64 model image: head parts get a LOD pair
 *     (subdivided near / original far); body parts stay original at every distance.
 */
#ifndef CONV_SUBDIV_H
#define CONV_SUBDIV_H

#include <stddef.h>
#include <stdint.h>

/* Near/far switch, model units. The game multiplies LOD distances by
 * model->scale (0.1 for the characters) and 1 world unit is ~1 cm, so
 * 30 m = 3000 world units = 30000 model units. */
#define CV_SD_NEAR_DIST 30000.0f
#define CV_SD_HEAD_CREASE_DEG 130.0f  /* head region: dihedral limit for a PN edge (the converter's CvSdParams) */

/* ------------------------------------------------------------ mesh level -- */
typedef struct {
    int16_t pos[3];
    uint16_t flag;
    int16_t st[2];
    uint8_t cn[4];
    int mtx;           /* matrix the vertex is loaded under; edges join only equal ones */
    int layer;         /* vertices weld (share a normal) only inside one layer */
    int head;          /* in the head region: only a triangle whose corners are all head is subdivided */
    float rest[3];     /* rest-pose position: local position + joint offsets */
} CvSdVert;

/* Tunables of the run (the converter passes none = these defaults). */
#define CV_SD_DEFAULT_CREASE_DEG 60.0f   /* dihedral above this = crease (flat midpoint) */
#define CV_SD_DEFAULT_BULGE_CLAMP 0.15f  /* a PN bulge above this fraction of the edge length falls back to flat */
typedef struct { float crease_deg, bulge_clamp; } CvSdParams;

/* Why an edge got no PN midpoint (CvSdMesh.ereason). 0 = PN. */
#define CV_SD_E_CREASE   1   /* two faces more than crease_deg apart: flat midpoint */
#define CV_SD_E_BOUNDARY 2   /* one face (or more than two): flat midpoint */
#define CV_SD_E_JOINT    4   /* ends under different matrices: edge not split */

typedef struct {
    CvSdVert *v;       /* nv_in originals, then the midpoints (nv total after run) */
    int nv, nv_in;
    int *tri;          /* 3 vertex ids per input triangle */
    int nt;
    int *otri;         /* run output: 3 ids per triangle, in input order */
    int *osrc;         /* run output: input triangle of each output triangle */
    int not_;
    int *mid_ab;       /* run output: endpoints of midpoint k (vertex nv_in + k) */
    int nmid;
    int nflat, npn;    /* run output: midpoints made flat / with the PN shape */
    int head_only;     /* 1 = only triangles whose corners are all CvSdVert.head are split (the converter); 0 = all (viewers) */
    const CvSdParams *params;  /* input, optional: NULL = defaults */
    uint8_t *ereason;  /* run output: CV_SD_E_* of each input triangle edge (3 per triangle) */
    uint8_t *mid_pn;   /* run output: midpoint k was made with the PN shape (1) or flat (0) */
} CvSdMesh;

/* Edge-midpoint position of a PN triangle: (Pi+Pj)/2 - (wij*Ni + wji*Nj)/8,
 * wij = (Pj-Pi).Ni, wji = (Pi-Pj).Nj. Symmetric in (i, j). */
void cv_sd_pn_mid(const float pi[3], const float ni[3], const float pj[3], const float nj[3], float out[3]);
/* UV (s10.5), flag and colour/alpha of the midpoint: linear, symmetric rounding. */
void cv_sd_attr_mid(const CvSdVert *a, const CvSdVert *b, CvSdVert *out);
/* Splits a triangle by its split edges (m[e] >= 0 = midpoint id of edge e:
 * e0 = v0-v1, e1 = v1-v2, e2 = v2-v0). Keeps the winding. Returns 1..4. */
int cv_sd_split_tri(const int v[3], const int m[3], const float pv[3][3], const float pm[3][3], int out[4][3]);
/* Whole mesh. 0 on success. Frees nothing the caller owns. */
int cv_sd_run(CvSdMesh *m);
void cv_sd_free_out(CvSdMesh *m);

/* ----------------------------------------------------------- model level -- */
/* The flat mesh of a model image's display-list nodes, as cv_subdiv_model builds it
 * (viewers: tools_pc/modelview). tier 0 = the near / detailed parts (skips the far
 * simplified LOD meshes), 1 = the far parts. Vertex positions are joint-local (the
 * matrix id is CvSdVert.mtx); joint_off[id] is that matrix's rest offset. Returns 1 when
 * a mesh was built, 0 when the file has no display-list node, <0 on error. */
typedef struct {
    CvSdMesh mesh;           /* nv == nv_in, tri/nt filled; call cv_sd_run on it */
    float (*joint_off)[3];   /* 1024 entries */
    uint8_t *joint_ok;       /* 1024 flags: the entry is known */
    int ndl;                 /* display-list nodes used */
    int *tri_marker;         /* per mesh triangle: index into marker[] of the texture marker (G_NOOP 0xC0) in force, -1 none */
    uint32_t (*marker)[2];   /* the markers' w0, w1 (texLoadFromGdl: id = w1 & 0xfff, type = w0 & 7, modes in w0 bits 10..23) */
    int nmarker;
} CvSdModelMesh;
int cv_sd_model_mesh(const uint8_t *src, uint32_t D, int NS, int NT, int tier, CvSdModelMesh *out);
void cv_sd_model_mesh_free(CvSdModelMesh *m);

typedef struct {
    int models;          /* files given to the pass */
    int nodes;           /* DL nodes seen */
    int done;            /* DL nodes wrapped in a LOD pair */
    int skipped;         /* DL nodes left alone (unsupported DL, overflow, verify) */
    long tri_before, tri_after;
    long vtx_before, vtx_after;
    long mid_pn, mid_flat;  /* midpoints made with the PN shape / flat */
    int max_slot;        /* highest vertex-cache slot a rebuilt DL touches */
    int verify_fail;
    int lod_shifted;     /* head parts, game LOD pairs: near = subdivided, far = original detailed, simplified dropped */
    int lod_collapsed;   /* body parts, game LOD pairs: detailed mesh kept at every distance, simplified dropped */
    int head_nodes, body_nodes;   /* DL nodes with / without a head region */
    int dl_dropped;      /* simplified DL nodes removed by the shift */
    int lod_fallback_dl; /* DL nodes under a game LOD that could not be shifted and were wrapped */
    int dl_wrapped;      /* DL nodes given a new near/far pair (incl. the fallback ones) */
    int dls, max_mtx, mtx_over;   /* rebuilt DLs, most G_MTX in one, DLs with >= 32 (mesh-cache palette limit) */
} CvSdStats;

/* src = decompressed N64 model file (D bytes, NS switches, NT texture
 * configs). Returns 1 and a malloc'd image (*out, *outlen) when anything was
 * rewritten, 0 when the file is left as is, <0 on error. */
int cv_subdiv_model(const uint8_t *src, uint32_t D, int NS, int NT, int head_file,
                    uint8_t **out, uint32_t *outlen, CvSdStats *st);

#endif
