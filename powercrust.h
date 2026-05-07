/*=========================================================================
  powercrust.h  –  PowerCrust surface reconstruction (VTK-free public API)

  Reconstructs a closed surface and its medial axis from an unorganised
  point cloud using the Power Crust algorithm (Amenta, Choi & Kolluri 2001).

  Usage:
      #include "powercrust.h"
      PCResult r = powercrust_run(input, 0.6);
      // r.surface    – reconstructed polygon mesh
      // r.medial_axis – medial axis polygon mesh + pole weights
=========================================================================*/
#pragma once

#include <array>
#include <vector>

// Simple polygon mesh: vertices + face index lists + optional per-vertex scalars.
struct PCMesh {
    std::vector<std::array<double,3>> points;
    std::vector<std::vector<int>>     faces;
    std::vector<double>               point_scalars; // medial axis: polar-ball weights
};

struct PCResult {
    PCMesh surface;     // PowerCrust reconstructed surface
    PCMesh medial_axis; // medial axis (poles + polar faces + weights)
};

// Run PowerCrust reconstruction.
//   input      – point cloud (only points[], faces[] is ignored)
//   estimate_r – smoothness parameter, default 0.6 (range ~0.4–1.0)
PCResult powercrust_run(const PCMesh& input, double estimate_r = 0.6);
