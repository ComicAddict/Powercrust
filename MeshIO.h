/*=========================================================================
  MeshIO.h  –  VTK-free mesh reader/writer for PowerCrust
  Supported formats (auto-detected from file extension):
    .obj  – Wavefront OBJ  (read + write)
    .ply  – Stanford PLY   (ASCII, binary little-endian, binary big-endian;
                            read + write)
    .gii  – GIFTI surface  (read: ASCII + Base64Binary encodings)
    .off  – Object File Format (read + write)
=========================================================================*/
#pragma once

#include "powercrust.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

// ── OBJ reader ────────────────────────────────────────────────────────────────

static PCMesh ReadOBJ(const std::string& path)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    PCMesh mesh;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v") {
            double x, y, z;
            if (ss >> x >> y >> z) mesh.points.push_back({x, y, z});
        } else if (tok == "f") {
            std::vector<int> face;
            std::string s;
            while (ss >> s) {
                int idx = std::stoi(s.substr(0, s.find('/')));
                face.push_back(idx < 0 ? (int)mesh.points.size() + idx : idx - 1);
            }
            if (face.size() >= 3) mesh.faces.push_back(face);
        }
    }
    return mesh;
}

// ── OBJ writer ────────────────────────────────────────────────────────────────

static bool WriteOBJ(const PCMesh& mesh, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "# PowerCrust\n";
    for (const auto& p : mesh.points)
        f << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
    for (const auto& face : mesh.faces) {
        f << 'f';
        for (int idx : face) f << ' ' << (idx + 1);
        f << '\n';
    }
    return true;
}

// ── PLY reader (ASCII + binary little/big-endian) ────────────────────────────

namespace ply_detail {

enum class Fmt { ASCII, BIN_LE, BIN_BE };

// Scalar type sizes used in binary PLY properties.
static int ply_type_size(const std::string& t)
{
    if (t=="char"||t=="uchar"||t=="int8"||t=="uint8") return 1;
    if (t=="short"||t=="ushort"||t=="int16"||t=="uint16") return 2;
    if (t=="int"||t=="uint"||t=="int32"||t=="uint32"||t=="float"||t=="float32") return 4;
    if (t=="double"||t=="float64"||t=="int64"||t=="uint64") return 8;
    return 4; // safe fallback
}

// Read bytes from stream and byte-swap if big-endian.
template<typename T>
static T ply_read(std::istream& s, bool swap)
{
    T v;
    s.read(reinterpret_cast<char*>(&v), sizeof(T));
    if (swap) {
        auto* b = reinterpret_cast<uint8_t*>(&v);
        std::reverse(b, b + sizeof(T));
    }
    return v;
}

// Read a PLY scalar of given type, return as double.
static double ply_read_scalar(std::istream& s, const std::string& t, bool swap)
{
    if (t=="float"||t=="float32") return ply_read<float>(s, swap);
    if (t=="double"||t=="float64") return ply_read<double>(s, swap);
    if (t=="int"||t=="int32")   return ply_read<int32_t>(s, swap);
    if (t=="uint"||t=="uint32") return ply_read<uint32_t>(s, swap);
    if (t=="short"||t=="int16") return ply_read<int16_t>(s, swap);
    if (t=="ushort"||t=="uint16") return ply_read<uint16_t>(s, swap);
    if (t=="char"||t=="int8")   return ply_read<int8_t>(s, swap);
    if (t=="uchar"||t=="uint8") return ply_read<uint8_t>(s, swap);
    return ply_read<float>(s, swap);
}

// Read a PLY scalar as int32.
static int32_t ply_read_int(std::istream& s, const std::string& t, bool swap)
{
    return static_cast<int32_t>(ply_read_scalar(s, t, swap));
}

struct PLYProp {
    std::string type, name;
    bool is_list = false;
    std::string count_type, elem_type;
};

} // namespace ply_detail

static PCMesh ReadPLY(const std::string& path)
{
    using namespace ply_detail;

    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    // ── Parse header ─────────────────────────────────────────────────────────
    Fmt fmt = Fmt::ASCII;
    int n_verts = 0, n_faces = 0;
    bool in_vertex = false, in_face = false;
    std::vector<PLYProp> vert_props, face_props;
    std::string line;

    while (std::getline(f, line)) {
        // Strip carriage-return for Windows-style line endings.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "end_header") break;

        std::istringstream ss(line);
        std::string tok; ss >> tok;

        if (tok == "format") {
            std::string fmtstr; ss >> fmtstr;
            if (fmtstr == "binary_little_endian") fmt = Fmt::BIN_LE;
            else if (fmtstr == "binary_big_endian") fmt = Fmt::BIN_BE;
        } else if (tok == "element") {
            std::string ename; int ecount; ss >> ename >> ecount;
            in_vertex = (ename == "vertex");
            in_face   = (ename == "face");
            if (in_vertex) n_verts = ecount;
            if (in_face)   n_faces = ecount;
        } else if (tok == "property") {
            std::string ptype; ss >> ptype;
            PLYProp prop;
            if (ptype == "list") {
                prop.is_list = true;
                ss >> prop.count_type >> prop.elem_type >> prop.name;
            } else {
                prop.type = ptype; ss >> prop.name;
            }
            if (in_vertex) vert_props.push_back(prop);
            if (in_face)   face_props.push_back(prop);
        }
    }

    PCMesh mesh;
    mesh.points.reserve(n_verts);
    mesh.faces.reserve(n_faces);

    if (fmt == Fmt::ASCII) {
        // ── ASCII path ───────────────────────────────────────────────────────
        for (int i = 0; i < n_verts && std::getline(f, line); ++i) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ss2(line);
            double x = 0, y = 0, z = 0;
            // Parse named properties so extra fields (nx, rgb…) are ignored.
            bool gx = false, gy = false, gz = false;
            for (const auto& p : vert_props) {
                double v; ss2 >> v;
                if (p.name == "x") { x = v; gx = true; }
                else if (p.name == "y") { y = v; gy = true; }
                else if (p.name == "z") { z = v; gz = true; }
            }
            if (gx && gy && gz) mesh.points.push_back({x, y, z});
        }
        for (int i = 0; i < n_faces && std::getline(f, line); ++i) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ss2(line);
            int n; ss2 >> n;
            std::vector<int> face(n);
            for (int j = 0; j < n; ++j) ss2 >> face[j];
            mesh.faces.push_back(face);
        }
    } else {
        // ── Binary path ──────────────────────────────────────────────────────
        const bool swap = (fmt == Fmt::BIN_BE);

        // Find byte offsets for x, y, z among vertex properties.
        int x_off = -1, y_off = -1, z_off = -1;
        int vert_stride = 0;
        for (const auto& p : vert_props) {
            int sz = ply_type_size(p.type);
            if (p.name == "x") x_off = vert_stride;
            else if (p.name == "y") y_off = vert_stride;
            else if (p.name == "z") z_off = vert_stride;
            vert_stride += sz;
        }
        if (x_off < 0 || y_off < 0 || z_off < 0)
            throw std::runtime_error("PLY file has no x/y/z vertex properties: " + path);

        // Determine vertex property types for x, y, z.
        std::string xt, yt, zt;
        int running = 0;
        for (const auto& p : vert_props) {
            if (running == x_off) xt = p.type;
            if (running == y_off) yt = p.type;
            if (running == z_off) zt = p.type;
            running += ply_type_size(p.type);
        }

        auto buf_read = [&](const char* buf, const std::string& t) -> double {
            double val = 0;
            if (t=="float"||t=="float32")   { float    v; std::memcpy(&v,buf,4); if(swap){auto*b=reinterpret_cast<uint8_t*>(&v);std::reverse(b,b+4);} val=v; }
            else if (t=="double"||t=="float64") { double v; std::memcpy(&v,buf,8); if(swap){auto*b=reinterpret_cast<uint8_t*>(&v);std::reverse(b,b+8);} val=v; }
            else if (t=="int"||t=="int32")  { int32_t  v; std::memcpy(&v,buf,4); if(swap){auto*b=reinterpret_cast<uint8_t*>(&v);std::reverse(b,b+4);} val=v; }
            else if (t=="uint"||t=="uint32"){ uint32_t v; std::memcpy(&v,buf,4); if(swap){auto*b=reinterpret_cast<uint8_t*>(&v);std::reverse(b,b+4);} val=v; }
            return val;
        };

        std::vector<char> vbuf(vert_stride);
        for (int i = 0; i < n_verts; ++i) {
            f.read(vbuf.data(), vert_stride);
            mesh.points.push_back({
                buf_read(vbuf.data() + x_off, xt),
                buf_read(vbuf.data() + y_off, yt),
                buf_read(vbuf.data() + z_off, zt)
            });
        }

        // Find the face list property (vertex_indices / vertex_index).
        std::string fc_type, fe_type;
        int face_pre = 0; // bytes before the list property
        for (const auto& p : face_props) {
            if (p.is_list) { fc_type = p.count_type; fe_type = p.elem_type; break; }
            face_pre += ply_type_size(p.type);
        }

        for (int i = 0; i < n_faces; ++i) {
            if (face_pre) f.seekg(face_pre, std::ios::cur);
            int count = ply_read_int(f, fc_type, swap);
            std::vector<int> face(count);
            for (int j = 0; j < count; ++j)
                face[j] = ply_read_int(f, fe_type, swap);
            mesh.faces.push_back(face);
        }
    }

    return mesh;
}

// ── PLY writer (ASCII) ────────────────────────────────────────────────────────

static bool WritePLY(const PCMesh& mesh, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "ply\nformat ascii 1.0\n"
      << "element vertex " << mesh.points.size() << "\n"
      << "property float x\nproperty float y\nproperty float z\n";
    if (!mesh.point_scalars.empty())
        f << "property float weight\n";
    f << "element face " << mesh.faces.size() << "\n"
      << "property list uchar int vertex_indices\nend_header\n";
    for (size_t i = 0; i < mesh.points.size(); ++i) {
        const auto& p = mesh.points[i];
        f << p[0] << ' ' << p[1] << ' ' << p[2];
        if (i < mesh.point_scalars.size()) f << ' ' << mesh.point_scalars[i];
        f << '\n';
    }
    for (const auto& face : mesh.faces) {
        f << face.size();
        for (int idx : face) f << ' ' << idx;
        f << '\n';
    }
    return true;
}

// ── OFF reader/writer ─────────────────────────────────────────────────────────

static PCMesh ReadOFF(const std::string& path)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string header; f >> header;
    if (header != "OFF") throw std::runtime_error("Not an OFF file: " + path);
    int nv, nf, ne; f >> nv >> nf >> ne;
    PCMesh mesh;
    mesh.points.reserve(nv); mesh.faces.reserve(nf);
    for (int i = 0; i < nv; ++i) {
        double x, y, z; f >> x >> y >> z;
        mesh.points.push_back({x, y, z});
    }
    for (int i = 0; i < nf; ++i) {
        int n; f >> n;
        std::vector<int> face(n);
        for (int j = 0; j < n; ++j) f >> face[j];
        mesh.faces.push_back(face);
    }
    return mesh;
}

static bool WriteOFF(const PCMesh& mesh, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "OFF\n" << mesh.points.size() << ' ' << mesh.faces.size() << " 0\n";
    for (const auto& p : mesh.points)
        f << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
    for (const auto& face : mesh.faces) {
        f << face.size();
        for (int idx : face) f << ' ' << idx;
        f << '\n';
    }
    return true;
}

// ── GIFTI reader ──────────────────────────────────────────────────────────────
// Supports NIFTI_INTENT_POINTSET (Float32/Float64) and NIFTI_INTENT_TRIANGLE (Int32).
// Encodings: ASCII, Base64Binary.
// GZipBase64Binary requires zlib – not handled here.

namespace gifti_detail {

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

static std::string data_content(const std::string& text, std::size_t from)
{
    auto open  = text.find("<Data>", from);
    auto close = text.find("</Data>", from);
    if (open == std::string::npos || close == std::string::npos) return {};
    return text.substr(open + 6, close - open - 6);
}

static const std::string B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<uint8_t> b64_decode(const std::string& enc)
{
    std::vector<uint8_t> out;
    out.reserve((enc.size() / 4) * 3);
    int val = 0, bits = -8;
    for (unsigned char c : enc) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        auto p = B64.find(static_cast<char>(c));
        if (p == std::string::npos) continue;
        val = (val << 6) | static_cast<int>(p);
        bits += 6;
        if (bits >= 0) { out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

} // namespace gifti_detail

static PCMesh ReadGIFTI(const std::string& path)
{
    using namespace gifti_detail;
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string xml{std::istreambuf_iterator<char>(f), {}};

    std::vector<float>   coords;
    std::vector<int32_t> tris;

    std::size_t pos = 0;
    while (true) {
        auto ts = xml.find("<DataArray", pos);
        if (ts == std::string::npos) break;
        auto te = xml.find('>', ts);
        if (te == std::string::npos) break;
        std::string tag    = xml.substr(ts, te - ts + 1);
        std::string intent = attr_value(tag, "Intent");
        std::string enc    = meshio_lower(attr_value(tag, "Encoding"));
        std::string raw    = data_content(xml, ts);
        bool is_pts  = (intent == "NIFTI_INTENT_POINTSET");
        bool is_tris = (intent == "NIFTI_INTENT_TRIANGLE");
        if (is_pts || is_tris) {
            if (enc == "ascii") {
                std::istringstream ss(raw);
                if (is_pts) { float v; while (ss >> v) coords.push_back(v); }
                else        { int32_t v; while (ss >> v) tris.push_back(v); }
            } else if (enc == "base64binary") {
                raw.erase(std::remove_if(raw.begin(), raw.end(), ::isspace), raw.end());
                auto bytes = b64_decode(raw);
                if (is_pts) {
                    bool f64 = (attr_value(tag, "DataType") == "NIFTI_TYPE_FLOAT64");
                    if (f64) {
                        size_t n = bytes.size() / 8; coords.resize(n);
                        for (size_t i = 0; i < n; ++i) {
                            double d; std::memcpy(&d, bytes.data() + i*8, 8);
                            coords[i] = static_cast<float>(d);
                        }
                    } else {
                        size_t n = bytes.size() / 4; coords.resize(n);
                        for (size_t i = 0; i < n; ++i) std::memcpy(&coords[i], bytes.data()+i*4, 4);
                    }
                } else {
                    size_t n = bytes.size() / 4; tris.resize(n);
                    for (size_t i = 0; i < n; ++i) std::memcpy(&tris[i], bytes.data()+i*4, 4);
                }
            } else {
                throw std::runtime_error(
                    "GIFTI encoding '" + attr_value(tag, "Encoding") + "' not supported. "
                    "Use ASCII or Base64Binary (or convert with wb_command/mris_convert).");
            }
        }
        pos = te + 1;
    }
    if (coords.empty())
        throw std::runtime_error("No NIFTI_INTENT_POINTSET DataArray found in " + path);

    PCMesh mesh;
    int nv = (int)coords.size() / 3;
    mesh.points.resize(nv);
    for (int i = 0; i < nv; ++i)
        mesh.points[i] = {coords[i*3], coords[i*3+1], coords[i*3+2]};

    int nf = (int)tris.size() / 3;
    mesh.faces.resize(nf);
    for (int i = 0; i < nf; ++i)
        mesh.faces[i] = {tris[i*3], tris[i*3+1], tris[i*3+2]};

    return mesh;
}

// ── Public API ────────────────────────────────────────────────────────────────

static PCMesh ReadMesh(const std::string& path)
{
    std::string ext = meshio_extension(path);
    if (ext == ".obj") return ReadOBJ(path);
    if (ext == ".ply") return ReadPLY(path);
    if (ext == ".gii") return ReadGIFTI(path);
    if (ext == ".off") return ReadOFF(path);
    throw std::runtime_error("Unsupported input format '" + ext +
                             "'. Accepted: .obj .ply .gii .off");
}

static bool WriteMesh(const PCMesh& mesh, const std::string& path)
{
    std::string ext = meshio_extension(path);
    if (ext == ".obj") return WriteOBJ(mesh, path);
    if (ext == ".ply") return WritePLY(mesh, path);
    if (ext == ".off") return WriteOFF(mesh, path);
    throw std::runtime_error("Unsupported output format '" + ext +
                             "'. Accepted: .obj .ply .off");
}
