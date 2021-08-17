#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

TEST_CASE("Split empty mesh", "[its_split][its]") {
    using namespace Slic3r;

    indexed_triangle_set its;

    std::vector<indexed_triangle_set> res = its_split(its);

    REQUIRE(res.empty());
}

TEST_CASE("Split simple mesh consisting of one part", "[its_split][its]") {
    using namespace Slic3r;

    auto cube = its_make_cube(10., 10., 10.);

    std::vector<indexed_triangle_set> res = its_split(cube);

    REQUIRE(res.size() == 1);
    REQUIRE(res.front().indices.size() == cube.indices.size());
    REQUIRE(res.front().vertices.size() == cube.vertices.size());
}

void debug_write_obj(const std::vector<indexed_triangle_set> &res, const std::string &name)
{
#ifndef NDEBUG
    size_t part_idx = 0;
    for (auto &part_its : res) {
        its_write_obj(part_its, (name + std::to_string(part_idx++) + ".obj").c_str());
    }
#endif
}

TEST_CASE("Split two non-watertight mesh", "[its_split][its]") {
    using namespace Slic3r;

    auto cube1 = its_make_cube(10., 10., 10.);
    cube1.indices.pop_back();
    auto cube2 = cube1;

    its_transform(cube1, identity3f().translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(cube2, identity3f().translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(cube1, cube2);

    std::vector<indexed_triangle_set> res = its_split(cube1);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == cube2.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == cube2.vertices.size());

    debug_write_obj(res, "parts_non_watertight");
}

TEST_CASE("Split non-manifold mesh", "[its_split][its]") {
    using namespace Slic3r;

    auto cube = its_make_cube(10., 10., 10.), cube_low = cube;

    its_transform(cube_low, identity3f().translate(Vec3f{10.f, 10.f, 10.f}));
    its_merge(cube, cube_low);
    its_merge_vertices(cube);

    std::vector<indexed_triangle_set> res = its_split(cube);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == cube_low.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == cube_low.vertices.size());

    debug_write_obj(res, "cubes_non_manifold");
}

TEST_CASE("Split two watertight meshes", "[its_split][its]") {
    using namespace Slic3r;

    auto sphere1 = its_make_sphere(10., 2 * PI / 200.), sphere2 = sphere1;

    its_transform(sphere1, identity3f().translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(sphere2, identity3f().translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(sphere1, sphere2);

    std::vector<indexed_triangle_set> res = its_split(sphere1);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].indices.size() == res[1].indices.size());
    REQUIRE(res[0].indices.size() == sphere2.indices.size());
    REQUIRE(res[0].vertices.size() == res[1].vertices.size());
    REQUIRE(res[0].vertices.size() == sphere2.vertices.size());

    debug_write_obj(res, "parts_watertight");
}

#include <libslic3r/QuadricEdgeCollapse.hpp>
static float triangle_area(const Vec3f &v0, const Vec3f &v1, const Vec3f &v2)
{
    Vec3f ab = v1 - v0;
    Vec3f ac = v2 - v0;
    return ab.cross(ac).norm() / 2.f;
}

static float triangle_area(const Vec3crd &triangle_inices, const std::vector<Vec3f> &vertices)
{
    return triangle_area(vertices[triangle_inices[0]],
                         vertices[triangle_inices[1]],
                         vertices[triangle_inices[2]]);
}

static std::mt19937 create_random_generator() {
    std::random_device rd;
    std::mt19937 gen(rd());
    return gen;
}

std::vector<Vec3f> its_sample_surface(const indexed_triangle_set &its,
                                      double        sample_per_mm2,
                                      std::mt19937 random_generator = create_random_generator())
{
    std::vector<Vec3f> samples;
    std::uniform_real_distribution<float> rand01(0.f, 1.f);
    for (const auto &triangle_indices : its.indices) {
        float area = triangle_area(triangle_indices, its.vertices);
        float countf;
        float fractional = std::modf(area * sample_per_mm2, &countf);
        int count = static_cast<int>(countf);

        float generate = rand01(random_generator);
        if (generate < fractional) ++count;
        if (count == 0) continue;

        const Vec3f &v0 = its.vertices[triangle_indices[0]];
        const Vec3f &v1 = its.vertices[triangle_indices[1]];
        const Vec3f &v2 = its.vertices[triangle_indices[2]];
        for (int c = 0; c < count; c++) {
            // barycentric coordinate
            Vec3f b;
            b[0] = rand01(random_generator);
            b[1] = rand01(random_generator);
            if ((b[0] + b[1]) > 1.f) {
                b[0] = 1.f - b[0];
                b[1] = 1.f - b[1];
            }
            b[2] = 1.f - b[0] - b[1];
            Vec3f pos;
            for (int i = 0; i < 3; i++) {
                pos[i] = b[0] * v0[i] + b[1] * v1[i] + b[2] * v2[i];
            }
            samples.push_back(pos);
        }        
    }
    return samples;
}


#include "libslic3r/AABBTreeIndirect.hpp"

// return Average abs distance to original
float compare(const indexed_triangle_set &original,
              const indexed_triangle_set &simplified,
              double                      sample_per_mm2)
{
    // create ABBTree
    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        original.vertices, original.indices);

    unsigned int init = 0;
    std::mt19937 rnd(init);
    auto samples = its_sample_surface(simplified, sample_per_mm2, rnd);

    float sumDistance = 0;
    for (const Vec3f &sample : samples) { 
        size_t hit_idx;
        Vec3f  hit_point;
        float distance2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            original.vertices, original.indices, tree, sample, hit_idx,
            hit_point);
        sumDistance += sqrt(distance2);
    }
    return sumDistance / samples.size();
}

TEST_CASE("Reduce one edge by Quadric Edge Collapse", "[its]")
{
    indexed_triangle_set its;
    its.vertices = {Vec3f(-1.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f),
                    Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, 1.f),
                    // vertex to be removed
                    Vec3f(0.9f, .1f, -.1f)};
    its.indices  = {Vec3i(1, 0, 3), Vec3i(2, 1, 3), Vec3i(0, 2, 3),
                   Vec3i(0, 1, 4), Vec3i(1, 2, 4), Vec3i(2, 0, 4)};
    // edge to remove is between vertices 2 and 4 on trinagles 4 and 5

    indexed_triangle_set its_ = its; // copy
    // its_write_obj(its, "tetrhedron_in.obj");
    uint32_t wanted_count = its.indices.size() - 1;
    its_quadric_edge_collapse(its, wanted_count);
    // its_write_obj(its, "tetrhedron_out.obj");
    CHECK(its.indices.size() == 4);
    CHECK(its.vertices.size() == 4);

    for (size_t i = 0; i < 3; i++) { 
        CHECK(its.indices[i] == its_.indices[i]);
    }

    for (size_t i = 0; i < 4; i++) {
        if (i == 2) continue;
        CHECK(its.vertices[i] == its_.vertices[i]);
    }

    const Vec3f &v = its.vertices[2]; // new vertex
    const Vec3f &v2 = its_.vertices[2]; // moved vertex
    const Vec3f &v4 = its_.vertices[4]; // removed vertex
    for (size_t i = 0; i < 3; i++) { 
        bool is_between = (v[i] < v4[i] && v[i] > v2[i]) ||
                          (v[i] > v4[i] && v[i] < v2[i]);
        CHECK(is_between);
    }
    float avg_distance = compare(its_, its, 10);
    CHECK(avg_distance < 8e-3f);
}

#include "test_utils.hpp"
TEST_CASE("Simplify mesh by Quadric edge collapse to 5%", "[its]")
{
    TriangleMesh mesh = load_model("frog_legs.obj");
    double original_volume = its_volume(mesh.its);
    uint32_t wanted_count = mesh.its.indices.size() * 0.05;
    REQUIRE_FALSE(mesh.empty());
    indexed_triangle_set its = mesh.its; // copy
    float max_error = std::numeric_limits<float>::max();
    its_quadric_edge_collapse(its, wanted_count, &max_error);
    //its_write_obj(its, "frog_legs_qec.obj");
    CHECK(its.indices.size() <= wanted_count);
    double volume = its_volume(its);
    CHECK(fabs(original_volume - volume) < 33.);
    float avg_distance = compare(mesh.its, its, 10);
    CHECK(avg_distance < 0.022f); // 0.02022 | 0.0199614074
}