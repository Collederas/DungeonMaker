#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <map>

namespace geom {

// A simple 3D point structure
struct Point3D {
    double x, y, z;
};

// Represents a tetrahedron using indices to the original point list
struct Tetrahedron {
    size_t p1, p2, p3, p4;
};

class Tetrahedralizer {
public:
    // This function takes a list of 3D points and returns a list of tetrahedra that connect them.
    // NOTE: This is a placeholder implementation. A proper Delaunay tetrahedralization
    // algorithm is significantly more complex. This version creates a graph by connecting
    // a point to its nearest neighbors, which is a suitable approach for creating a plausible
    // network of rooms for a dungeon.
    static std::vector<Tetrahedron> Triangulate(const std::vector<Point3D>& points) {
        std::vector<Tetrahedron> tetrahedra;
        if (points.size() < 4) return tetrahedra;

        // 1. For each point, find its K nearest neighbors.
        const size_t K = 5; // Connect to 5 nearest neighbors
        std::vector<std::vector<size_t>> nearest_neighbors(points.size());

        for (size_t i = 0; i < points.size(); ++i) {
            std::vector<std::pair<double, size_t>> distances;
            for (size_t j = 0; j < points.size(); ++j) {
                if (i == j) continue;
                double dist_sq = std::pow(points[i].x - points[j].x, 2) +
                                 std::pow(points[i].y - points[j].y, 2) +
                                 std::pow(points[i].z - points[j].z, 2);
                distances.push_back({dist_sq, j});
            }
            std::sort(distances.begin(), distances.end());
            
            for (size_t k = 0; k < std::min(K, distances.size()); ++k) {
                nearest_neighbors[i].push_back(distances[k].second);
            }
        }
        
        // 2. Form tetrahedra from these connections to create a graph.
        std::map<std::vector<size_t>, bool> created_tetra;
        for (size_t i = 0; i < points.size(); ++i) {
            // Find triangles among neighbors: (i, j, k)
            for (size_t j_idx = 0; j_idx < nearest_neighbors[i].size(); ++j_idx) {
                for (size_t k_idx = j_idx + 1; k_idx < nearest_neighbors[i].size(); ++k_idx) {
                    size_t j = nearest_neighbors[i][j_idx];
                    size_t k = nearest_neighbors[i][k_idx];
                    
                    // Find a 4th point, l, to form the tetrahedron (i,j,k,l)
                    for (size_t l_idx = k_idx + 1; l_idx < nearest_neighbors[i].size(); ++l_idx) {
                         size_t l = nearest_neighbors[i][l_idx];
                         std::vector<size_t> p = {i, j, k, l};
                         std::sort(p.begin(), p.end());
                         if (created_tetra.find(p) == created_tetra.end()) {
                             tetrahedra.push_back({p[0], p[1], p[2], p[3]});
                             created_tetra[p] = true;
                         }
                    }
                }
            }
        }

        // Ensure at least one tetrahedron exists if possible for a connected graph
        if (tetrahedra.empty() && points.size() >= 4) {
            tetrahedra.push_back({0, 1, 2, 3});
        }
        
        return tetrahedra;
    }
};

} // namespace geom