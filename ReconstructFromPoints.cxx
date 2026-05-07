/*=========================================================================
  ReconstructFromPoints.cxx
  PowerCrust surface reconstruction – command-line driver

  Accepted input formats  : .obj  .ply  .gii  .vtk
  Accepted output formats : .obj  .ply  .vtk

  Usage:
    reconstruct -i <input_mesh_or_points> -o <output_surface> [options]

  Options:
    -i <file>          Input file (mesh or point cloud)
    -o <file>          Output reconstructed surface
    -m <file>          Output medial surface (optional)
    -r <value>         Estimate_r parameter (default 0.6)
    --points <file>    Legacy plain-text point cloud:
                         first line = N, then N lines of "X Y Z"
    -h / --help        Print this help
=========================================================================*/

#include <vtkSmartPointer.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPowerCrustSurfaceReconstruction.h>
#include "MeshIO.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static void print_usage(const char* argv0)
{
    std::cerr
        << "\nUsage:\n"
        << "  " << argv0 << " -i <input> -o <output> [options]\n\n"
        << "Input formats  : .obj  .ply  .gii (GIFTI)  .vtk\n"
        << "Output formats : .obj  .ply  .vtk\n\n"
        << "Options:\n"
        << "  -i <file>       Input mesh / point cloud\n"
        << "  -o <file>       Output reconstructed surface\n"
        << "  -m <file>       Output medial surface (optional)\n"
        << "  -r <value>      estimate_r  (default 0.6)\n"
        << "  --points <file> Legacy text point cloud (N, then N×\"X Y Z\" lines)\n"
        << "  -h / --help     Show this help\n\n"
        << "Examples:\n"
        << "  " << argv0 << " -i brain.gii   -o brain_crust.vtk\n"
        << "  " << argv0 << " -i scan.ply    -o result.ply   -m medial.vtk\n"
        << "  " << argv0 << " -i model.obj   -o model_crust.obj -r 0.5\n"
        << "  " << argv0 << " --points pts.txt -o surface.vtk\n";
}

// Read a legacy plain-text point cloud:  first line = N, then N "X Y Z" lines.
static vtkSmartPointer<vtkPolyData> read_text_points(const std::string& path)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open point file: " + path);

    unsigned int n = 0;
    f >> n;
    if (!f || n == 0) throw std::runtime_error("Invalid point-count in: " + path);

    auto pts     = vtkSmartPointer<vtkPoints>::New();
    auto polydata = vtkSmartPointer<vtkPolyData>::New();
    pts->SetNumberOfPoints(static_cast<vtkIdType>(n));

    double p[3];
    for (unsigned int i = 0; i < n; ++i) {
        f >> p[0] >> p[1] >> p[2];
        if (!f) throw std::runtime_error("Unexpected EOF in point file: " + path);
        pts->SetPoint(static_cast<vtkIdType>(i), p);
    }

    polydata->SetPoints(pts);
    return polydata;
}

int main(int argc, char* argv[])
{
    if (argc < 2) { print_usage(argv[0]); return EXIT_FAILURE; }

    std::string input_path, output_path, medial_path, points_path;
    double estimate_r = 0.6;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); return EXIT_SUCCESS;
        } else if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (std::strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            medial_path = argv[++i];
        } else if (std::strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            estimate_r = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--points") == 0 && i + 1 < argc) {
            points_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    // Legacy two-argument form for backwards compatibility:  reconstruct input output
    if (input_path.empty() && output_path.empty() && argc == 3) {
        points_path = argv[1];
        output_path = argv[2];
    }

    if (input_path.empty() && points_path.empty()) {
        std::cerr << "Error: no input specified (-i or --points).\n";
        print_usage(argv[0]); return EXIT_FAILURE;
    }
    if (output_path.empty()) {
        std::cerr << "Error: no output specified (-o).\n";
        print_usage(argv[0]); return EXIT_FAILURE;
    }

    // ── Load input ────────────────────────────────────────────────────────────
    vtkSmartPointer<vtkPolyData> input_data;
    try {
        if (!points_path.empty()) {
            std::cout << "Reading text point cloud: " << points_path << "\n";
            input_data = read_text_points(points_path);
        } else {
            std::cout << "Reading mesh: " << input_path << "\n";
            input_data = ReadMesh(input_path);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error reading input: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    vtkIdType npts = input_data->GetNumberOfPoints();
    if (npts == 0) {
        std::cerr << "Error: input contains no points.\n";
        return EXIT_FAILURE;
    }
    std::cout << "  " << npts << " points loaded.\n";

    // ── Run PowerCrust ────────────────────────────────────────────────────────
    auto reconstruct = vtkSmartPointer<vtkPowerCrustSurfaceReconstruction>::New();
    reconstruct->SetInputData(input_data);
    reconstruct->SetEstimate_r(estimate_r);

    std::cout << "Running PowerCrust (estimate_r=" << estimate_r << ")...\n";
    reconstruct->Update();
    std::cout << "Reconstruction done.\n";

    // ── Write reconstructed surface ───────────────────────────────────────────
    try {
        std::cout << "Writing surface: " << output_path << "\n";
        if (!WriteMesh(reconstruct->GetOutput(), output_path)) {
            std::cerr << "Error: failed to write " << output_path << "\n";
            return EXIT_FAILURE;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error writing output: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    // ── Optionally write medial surface ───────────────────────────────────────
    if (!medial_path.empty()) {
        try {
            std::cout << "Writing medial surface: " << medial_path << "\n";
            if (!WriteMesh(reconstruct->GetMedialSurface(), medial_path)) {
                std::cerr << "Warning: failed to write medial surface " << medial_path << "\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "Warning: " << ex.what() << "\n";
        }
    }

    std::cout << "Done.\n";
    return EXIT_SUCCESS;
}
