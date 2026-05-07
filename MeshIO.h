/*=========================================================================
  MeshIO.h – unified mesh reader/writer for PowerCrust CLI
  Supported formats (auto-detected from file extension):
    .obj   – Wavefront OBJ  (via vtkOBJReader / manual writer)
    .ply   – Stanford PLY   (via vtkPLYReader / vtkPLYWriter)
    .gii   – GIFTI surface  (built-in parser: ASCII + Base64Binary)
    .vtk   – VTK legacy     (via vtkPolyDataReader / vtkPolyDataWriter)
=========================================================================*/
#pragma once

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkOBJReader.h>
#include <vtkPLYReader.h>
#include <vtkPLYWriter.h>
#include <vtkPolyDataReader.h>
#include <vtkPolyDataWriter.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ── String helpers ────────────────────────────────────────────────────────────

static inline std::string meshio_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static inline std::string meshio_extension(const std::string& path)
{
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return meshio_lower(path.substr(pos));
}

// ── Base64 decoder (RFC 4648) ─────────────────────────────────────────────────

static const std::string B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline std::vector<uint8_t> base64_decode(const std::string& encoded)
{
    std::vector<uint8_t> out;
    out.reserve((encoded.size() / 4) * 3);

    int val = 0, bits = -8;
    for (unsigned char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        auto pos = B64_CHARS.find(static_cast<char>(c));
        if (pos == std::string::npos) continue;
        val = (val << 6) | static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ── GIFTI reader ─────────────────────────────────────────────────────────────
// Supports NIFTI_INTENT_POINTSET (Float32) and NIFTI_INTENT_TRIANGLE (Int32).
// Encodings: ASCII, Base64Binary.
// GZipBase64Binary requires zlib – not handled here; use VTK 9+ for that.

namespace gifti_detail {

// Extract the value of an XML attribute from a tag string.
// e.g. attr_value("<DataArray Intent=\"FOO\" ...>", "Intent") -> "FOO"
static std::string attr_value(const std::string& tag, const std::string& attr)
{
    auto pos = tag.find(attr);
    if (pos == std::string::npos) return {};
    pos = tag.find('"', pos + attr.size());
    if (pos == std::string::npos) return {};
    auto end = tag.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return tag.substr(pos + 1, end - pos - 1);
}

// Extract content between <Data> ... </Data>
static std::string data_content(const std::string& text, std::size_t from)
{
    auto open  = text.find("<Data>", from);
    auto close = text.find("</Data>", from);
    if (open == std::string::npos || close == std::string::npos) return {};
    open += 6; // len("<Data>")
    return text.substr(open, close - open);
}

static std::vector<float> parse_floats(const std::string& s)
{
    std::vector<float> v;
    std::istringstream ss(s);
    float f;
    while (ss >> f) v.push_back(f);
    return v;
}

static std::vector<int32_t> parse_ints(const std::string& s)
{
    std::vector<int32_t> v;
    std::istringstream ss(s);
    int32_t i;
    while (ss >> i) v.push_back(i);
    return v;
}

// Read all bytes from file
static std::string read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace gifti_detail

static vtkSmartPointer<vtkPolyData> ReadGIFTI(const std::string& path)
{
    using namespace gifti_detail;

    std::string xml = read_file(path);

    // We'll collect vertex and face data from the two expected DataArrays.
    std::vector<float>   coords;
    std::vector<int32_t> tris;
    int n_vertices = 0, n_faces = 0;

    std::size_t search_from = 0;
    while (true) {
        auto tag_start = xml.find("<DataArray", search_from);
        if (tag_start == std::string::npos) break;
        auto tag_end   = xml.find('>', tag_start);
        if (tag_end   == std::string::npos) break;

        std::string tag = xml.substr(tag_start, tag_end - tag_start + 1);
        std::string intent   = attr_value(tag, "Intent");
        std::string encoding = attr_value(tag, "Encoding");
        std::string dim0_s   = attr_value(tag, "Dim0");
        int dim0 = dim0_s.empty() ? 0 : std::stoi(dim0_s);

        std::string raw = data_content(xml, tag_start);

        bool is_points = (intent == "NIFTI_INTENT_POINTSET");
        bool is_tris   = (intent == "NIFTI_INTENT_TRIANGLE");

        if (is_points || is_tris) {
            std::string enc_lower = meshio_lower(encoding);

            if (enc_lower == "ascii") {
                if (is_points) { coords = parse_floats(raw); n_vertices = dim0; }
                else           { tris   = parse_ints(raw);   n_faces    = dim0; }

            } else if (enc_lower == "base64binary") {
                // Trim whitespace from raw
                raw.erase(std::remove_if(raw.begin(), raw.end(), ::isspace), raw.end());
                auto bytes = base64_decode(raw);

                if (is_points) {
                    n_vertices = dim0;
                    std::string dtype = attr_value(tag, "DataType");
                    bool is_f64 = (dtype == "NIFTI_TYPE_FLOAT64");
                    if (is_f64) {
                        size_t n = bytes.size() / sizeof(double);
                        coords.resize(n);
                        double tmp; std::memcpy(&tmp, bytes.data(), 0);
                        for (size_t i = 0; i < n; ++i) {
                            std::memcpy(&tmp, bytes.data() + i * sizeof(double), sizeof(double));
                            coords[i] = static_cast<float>(tmp);
                        }
                    } else {
                        size_t n = bytes.size() / sizeof(float);
                        coords.resize(n);
                        for (size_t i = 0; i < n; ++i)
                            std::memcpy(&coords[i], bytes.data() + i * sizeof(float), sizeof(float));
                    }
                } else {
                    n_faces = dim0;
                    size_t n = bytes.size() / sizeof(int32_t);
                    tris.resize(n);
                    for (size_t i = 0; i < n; ++i)
                        std::memcpy(&tris[i], bytes.data() + i * sizeof(int32_t), sizeof(int32_t));
                }
            } else {
                throw std::runtime_error(
                    "GIFTI encoding '" + encoding + "' is not supported. "
                    "Use ASCII or Base64Binary, or convert with wb_command / mris_convert.");
            }
        }

        search_from = tag_end + 1;
    }

    if (coords.empty())
        throw std::runtime_error("No NIFTI_INTENT_POINTSET DataArray found in " + path);

    auto polydata = vtkSmartPointer<vtkPolyData>::New();
    auto points   = vtkSmartPointer<vtkPoints>::New();

    int actual_verts = static_cast<int>(coords.size()) / 3;
    points->SetNumberOfPoints(actual_verts);
    for (int i = 0; i < actual_verts; ++i)
        points->SetPoint(i, coords[i*3], coords[i*3+1], coords[i*3+2]);
    polydata->SetPoints(points);

    if (!tris.empty()) {
        auto cells = vtkSmartPointer<vtkCellArray>::New();
        int actual_faces = static_cast<int>(tris.size()) / 3;
        for (int i = 0; i < actual_faces; ++i) {
            vtkIdType ids[3] = { tris[i*3], tris[i*3+1], tris[i*3+2] };
            cells->InsertNextCell(3, ids);
        }
        polydata->SetPolys(cells);
    }

    return polydata;
}

// ── OBJ writer (minimal – VTK's exporter writes multi-file, this is simpler) ─

static bool WriteOBJ(vtkPolyData* pd, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;

    f << "# PowerCrust surface reconstruction\n";
    auto* pts = pd->GetPoints();
    if (!pts) return false;

    vtkIdType np = pts->GetNumberOfPoints();
    double p[3];
    for (vtkIdType i = 0; i < np; ++i) {
        pts->GetPoint(i, p);
        f << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
    }

    auto* polys = pd->GetPolys();
    if (polys) {
        polys->InitTraversal();
        vtkIdType n; const vtkIdType* ids;
        while (polys->GetNextCell(n, ids)) {
            if (n < 3) continue;
            f << 'f';
            for (vtkIdType j = 0; j < n; ++j) f << ' ' << (ids[j] + 1); // OBJ is 1-indexed
            f << '\n';
        }
    }
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Read a mesh file; format is inferred from the extension.
static vtkSmartPointer<vtkPolyData> ReadMesh(const std::string& path)
{
    std::string ext = meshio_extension(path);

    if (ext == ".obj") {
        auto reader = vtkSmartPointer<vtkOBJReader>::New();
        reader->SetFileName(path.c_str());
        reader->Update();
        return reader->GetOutput();
    }

    if (ext == ".ply") {
        auto reader = vtkSmartPointer<vtkPLYReader>::New();
        reader->SetFileName(path.c_str());
        reader->Update();
        return reader->GetOutput();
    }

    if (ext == ".gii") {
        return ReadGIFTI(path);
    }

    if (ext == ".vtk") {
        auto reader = vtkSmartPointer<vtkPolyDataReader>::New();
        reader->SetFileName(path.c_str());
        reader->Update();
        return reader->GetOutput();
    }

    throw std::runtime_error("Unsupported input format '" + ext +
                             "'. Accepted: .obj .ply .gii .vtk");
}

// Write a mesh; format is inferred from the extension.
static bool WriteMesh(vtkPolyData* pd, const std::string& path)
{
    std::string ext = meshio_extension(path);

    if (ext == ".obj")
        return WriteOBJ(pd, path);

    if (ext == ".ply") {
        auto writer = vtkSmartPointer<vtkPLYWriter>::New();
        writer->SetFileName(path.c_str());
        writer->SetInputData(pd);
        return writer->Write() == 1;
    }

    if (ext == ".vtk") {
        auto writer = vtkSmartPointer<vtkPolyDataWriter>::New();
        writer->SetFileName(path.c_str());
        writer->SetInputData(pd);
        return writer->Write() == 1;
    }

    throw std::runtime_error("Unsupported output format '" + ext +
                             "'. Accepted: .obj .ply .vtk");
}
