#include "model/ModelLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using deskpet::model::HumanBone;
using deskpet::model::loadModelFromMemory;
using deskpet::model::LoadResult;
using deskpet::model::ModelFormat;

namespace {

// 최소 ASCII FBX 7.4: 삼각형 메시 1개(cm 단위, Y 위), 머티리얼 1개, Mixamo 이름의 본 2개
constexpr std::string_view kAsciiFbx = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 100, "Geometry::Tri", "Mesh" {
		Vertices: *9 {
			a: 0,0,0,100,0,0,0,200,0
		}
		PolygonVertexIndex: *3 {
			a: 0,1,-3
		}
		GeometryVersion: 124
		LayerElementNormal: 0 {
			Version: 102
			Name: ""
			MappingInformationType: "ByPolygonVertex"
			ReferenceInformationType: "Direct"
			Normals: *9 {
				a: 0,0,1,0,0,1,0,0,1
			}
		}
		LayerElementUV: 0 {
			Version: 101
			Name: "UVMap"
			MappingInformationType: "ByPolygonVertex"
			ReferenceInformationType: "Direct"
			UV: *6 {
				a: 0,1,1,1,0,0
			}
		}
		LayerElementMaterial: 0 {
			Version: 101
			Name: ""
			MappingInformationType: "AllSame"
			ReferenceInformationType: "IndexToDirect"
			Materials: *1 {
				a: 0
			}
		}
		Layer: 0 {
			Version: 100
			LayerElement:  {
				Type: "LayerElementNormal"
				TypedIndex: 0
			}
			LayerElement:  {
				Type: "LayerElementUV"
				TypedIndex: 0
			}
			LayerElement:  {
				Type: "LayerElementMaterial"
				TypedIndex: 0
			}
		}
	}
	Model: 200, "Model::Tri", "Mesh" {
		Version: 232
	}
	Model: 300, "Model::mixamorig:Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,100,0
		}
	}
	NodeAttribute: 301, "NodeAttribute::", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Model: 400, "Model::mixamorig:LeftArm", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",20,50,0
		}
	}
	NodeAttribute: 401, "NodeAttribute::", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Material: 500, "Material::Skin", "" {
		Version: 102
		ShadingModel: "phong"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1,0.5,0.25
			P: "DiffuseFactor", "Number", "", "A",1
		}
	}
}
Connections:  {
	C: "OO",100,200
	C: "OO",200,0
	C: "OO",500,200
	C: "OO",300,0
	C: "OO",301,300
	C: "OO",400,300
	C: "OO",401,400
}
)";

LoadResult load(std::string_view text) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    return loadModelFromMemory(std::span(bytes, text.size()), ".fbx");
}

}  // namespace

TEST(FbxImporter, AsciiTriangle_IsConvertedToMeters) {
    const LoadResult result = load(kAsciiFbx);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;

    EXPECT_EQ(model.format, ModelFormat::Fbx);
    ASSERT_EQ(model.vertices.size(), 3U);
    ASSERT_EQ(model.indices.size(), 3U);
    // cm → m (UnitScaleFactor 1 = 1cm)
    EXPECT_NEAR(model.bounds.max.x, 1.0f, 1e-4f);
    EXPECT_NEAR(model.bounds.max.y, 2.0f, 1e-4f);
    EXPECT_NEAR(model.vertices[0].normal.z, 1.0f, 1e-4f);
}

TEST(FbxImporter, UvOrigin_IsFlippedToTopLeft) {
    const LoadResult result = load(kAsciiFbx);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    // FBX UV 원점은 왼쪽 아래 → v' = 1 − v. 첫 정점(0,0,0)의 UV (0,1) → (0,0)
    for (const auto& v : result.model->vertices) {
        if (v.position.x == 0.0f && v.position.y == 0.0f) {
            EXPECT_NEAR(v.v, 0.0f, 1e-5f);
        }
    }
}

TEST(FbxImporter, Material_ReadsDiffuseColor) {
    const LoadResult result = load(kAsciiFbx);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    ASSERT_EQ(model.materials.size(), 1U);
    EXPECT_EQ(model.materials[0].name, "Skin");
    EXPECT_NEAR(model.materials[0].baseColor.y, 0.5f, 1e-4f);
    ASSERT_EQ(model.primitives.size(), 1U);
    EXPECT_EQ(model.primitives[0].material, 0);
}

TEST(FbxImporter, MixamoBones_AreMappedToHumanoid) {
    const LoadResult result = load(kAsciiFbx);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;

    const int hips = model.findBone(HumanBone::Hips);
    const int arm = model.findBone(HumanBone::LeftUpperArm);
    ASSERT_GE(hips, 0);
    ASSERT_GE(arm, 0);
    EXPECT_EQ(model.bones[static_cast<std::size_t>(arm)].parent, hips);
    // 부모(0,1,0) + 자식 로컬(0.2,0.5,0)
    EXPECT_NEAR(model.bones[static_cast<std::size_t>(arm)].position.y, 1.5f, 1e-4f);
    EXPECT_NEAR(model.bones[static_cast<std::size_t>(arm)].position.x, 0.2f, 1e-4f);
}

TEST(FbxImporter, Garbage_ReturnsError) {
    const LoadResult result = load("; FBX 7.4.0 project file\nnot really fbx {{{");
    EXPECT_FALSE(result.model.has_value());
    EXPECT_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// 스킨 가중치 (애니메이션, ADR-0010)
// ---------------------------------------------------------------------------

namespace {

// 위 삼각형에 스킨을 붙인 버전: 정점 0·1은 Hips, 정점 2는 Hips 0.25 + LeftArm 0.75
std::string skinnedFbx() {
    std::string text(kAsciiFbx);
    const std::string deformers = R"(	Deformer: 600, "Deformer::Skin", "Skin" {
		Version: 101
	}
	Deformer: 601, "SubDeformer::Hips", "Cluster" {
		Version: 100
		Indexes: *3 {
			a: 0,1,2
		}
		Weights: *3 {
			a: 1,1,0.25
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,-100,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,100,0,1
		}
	}
	Deformer: 602, "SubDeformer::LeftArm", "Cluster" {
		Version: 100
		Indexes: *1 {
			a: 2
		}
		Weights: *1 {
			a: 0.75
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,-20,-150,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,20,150,0,1
		}
	}
)";
    const std::string connections = R"(	C: "OO",600,100
	C: "OO",601,600
	C: "OO",602,600
	C: "OO",300,601
	C: "OO",400,602
)";
    text.insert(text.find("\tMaterial: 500"), deformers);
    text.insert(text.find("\tC: \"OO\",100,200"), connections);
    return text;
}

}  // namespace

TEST(FbxImporter, SkinClusters_BecomeVertexWeights) {
    const std::string text = skinnedFbx();
    const LoadResult result = load(text);
    ASSERT_TRUE(result.model.has_value()) << result.error;
    const auto& model = *result.model;
    const int hips = model.findBone(HumanBone::Hips);
    const int arm = model.findBone(HumanBone::LeftUpperArm);
    ASSERT_GE(hips, 0);
    ASSERT_GE(arm, 0);

    bool sawSplit = false;
    for (const auto& v : model.vertices) {
        if (v.position.y > 1.5f) {  // 정점 2 (y = 2m)
            sawSplit = true;
            // 가중치 큰 순서로 정렬되어 있음
            EXPECT_EQ(v.joints[0], arm);
            EXPECT_NEAR(v.weights[0], 0.75f, 1e-5f);
            EXPECT_EQ(v.joints[1], hips);
            EXPECT_NEAR(v.weights[1], 0.25f, 1e-5f);
        } else {
            EXPECT_EQ(v.joints[0], hips);
            EXPECT_NEAR(v.weights[0], 1.0f, 1e-5f);
        }
    }
    EXPECT_TRUE(sawSplit);
}
