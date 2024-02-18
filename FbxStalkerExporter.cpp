#include <fbxsdk.h>

#include "xray_re/xr_file_system.h"
#include "xray_re/xr_level.h"
#include "xray_re/xr_level_cform.h"
#include "xray_re/xr_level_shaders.h"
#include "xray_re/xr_level_visuals.h"
#include "xray_re/xr_ogf.h"
#include "xray_re/xr_ogf_v4.h"

namespace {

inline FbxString FbxStalkerGetBaseFilename(const char* Path)
{
	const FbxString FilePath = Path;
	const int Pos = FilePath.Find('\\');

	if (Pos != -1)
	{
		return FilePath.Mid(Pos + 1);
	}

	return FilePath;
}

bool FbxStalkerExportStaticMesh(
	const xray_re::xr_ogf* Ogf,
	FbxMesh* Mesh)
{
	const auto& VertexBuffer = Ogf->vb();
	const auto& IndexBuffer = Ogf->ib();

	const int ChannelId = 0;
	const int VertsPerFace = 3;
	const int NumVerts = static_cast<int>(VertexBuffer.size());
	const int NumFaces = static_cast<int>(IndexBuffer.size()) / VertsPerFace;
	const int NumChannels = 1;

	const auto* Vert = VertexBuffer.p();
	const auto* Norm = VertexBuffer.n();
	const auto* UV = VertexBuffer.tc();

	if (!Vert || !Norm || !UV || !NumFaces)
	{
		return false;
	}

	Mesh->InitControlPoints(NumVerts);

	FbxGeometryElementNormal* GeometryElementNormal = Mesh->CreateElementNormal();
	GeometryElementNormal->SetMappingMode(FbxGeometryElement::eByControlPoint);

	FbxLayerElementUV* LayerElementDiffuseUV = FbxLayerElementUV::Create(Mesh, "");
	LayerElementDiffuseUV->SetMappingMode(FbxLayerElement::eByControlPoint);

	FbxLayerElementMaterial* LayerElementMaterial = FbxLayerElementMaterial::Create(Mesh, "");
	LayerElementMaterial->SetMappingMode(FbxLayerElement::eByPolygon);
	LayerElementMaterial->SetReferenceMode(FbxLayerElement::eIndexToDirect);

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	for (int VertId = 0; VertId < NumVerts; ++VertId)
	{
		ControlPoints[VertId].Set(
			Vert[VertId].x,
			Vert[VertId].y,
			Vert[VertId].z
		);
		GeometryElementNormal->GetDirectArray().Add(
			FbxVector4(
				Norm[VertId].x,
				Norm[VertId].y,
				Norm[VertId].z
			)
		);
		LayerElementDiffuseUV->GetDirectArray().Add(
			FbxVector2(
				UV[VertId].u,
				UV[VertId].v
			)
		);
	}

	if (Mesh->GetLayerCount() == 0)
	{
		Mesh->CreateLayer();
	}
	
	FbxLayer* Layer = Mesh->GetLayer(0);
	Layer->SetUVs(LayerElementDiffuseUV, FbxLayerElement::eTextureDiffuse);
	Layer->SetMaterials(LayerElementMaterial);

	for (int FaceId = 0; FaceId < NumFaces; ++FaceId)
	{
		Mesh->BeginPolygon(0);
		for (int VertexId = 0; VertexId < VertsPerFace; ++VertexId)
		{
			const int Index = FaceId * VertsPerFace + VertexId;
			Mesh->AddPolygon(IndexBuffer[Index]);
		}
		Mesh->EndPolygon();
	}

	return true;
}

void FbxStalkerExportLevelVisuals(
	const xray_re::xr_level_visuals* LevelVisuals,
	const xray_re::xr_level_shaders* Shaders,
	FbxScene* Scene)
{
	char Name[128];

	const auto& Ogfs = LevelVisuals->ogfs();
	for (std::size_t OgfId = 0; OgfId < Ogfs.size(); ++OgfId)
	{
		const auto* Ogf = Ogfs[OgfId];

		std::snprintf(Name, static_cast<int>(sizeof(Name)), "level_visual_%zu", OgfId);

		FbxNode* Node = FbxNode::Create(Scene, Name);
		FbxMesh* Mesh = FbxMesh::Create(Scene, Name);
		if (!FbxStalkerExportStaticMesh(Ogf, Mesh))
		{
			FBXSDK_printf("Can't export static mesh '%s'.\n", Name);
			Mesh->Destroy();
			continue;
		}
		Node->AddNodeAttribute(Mesh);

		const int TextureId = Ogf->texture_l();
		const auto& Textures = Shaders->textures();
		if (TextureId < Textures.size())
		{
			const auto Name = Textures[TextureId].c_str();
			if (auto* Material = Scene->GetMaterial(FbxStalkerGetBaseFilename(Name)))
			{
				Node->AddMaterial(Material);
			}
		}

		if (const auto* OgfV4 = dynamic_cast<const xray_re::xr_ogf_v4*>(Ogf))
		{
			const auto& Xform = OgfV4->xform();
			if (!Xform.is_identity())
			{
				float Rx, Ry, Rz;

				const float RadToDeg = static_cast<float>(180.0 / M_PI);

				Xform.get_euler_xyz(Rx, Ry, Rz);
				Node->LclRotation.Set(FbxVector4(Rx * RadToDeg, Ry * RadToDeg, Rz * RadToDeg));
				Node->LclTranslation.Set(FbxVector4(Xform._41, Xform._42, Xform._43));
			}
		}

		Scene->GetRootNode()->AddChild(Node);
	}
}

void FbxStalkerExportLevelMaterials(
	const xray_re::xr_file_system& Filesystem,
	const xray_re::xr_level_shaders* Shaders,
	FbxScene* Scene)
{
	for (const auto& ResourcePath : Shaders->textures())
	{
		const FbxString RelativePath = ResourcePath.c_str();
		if (Scene->GetMaterial(RelativePath))
		{
			continue;
		}

		if (!RelativePath.IsEmpty() && !Scene->GetTexture(RelativePath))
		{
			const FbxString Name = FbxStalkerGetBaseFilename(RelativePath);
			auto* Material = FbxSurfacePhong::Create(Scene, Name);
			if (!Material)
			{
				continue;
			}

			auto ColorProfile = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
			if (ColorProfile.IsValid())
			{
				const char* Ext = ".png";

				std::string TexturePath;
				if (Filesystem.resolve_path("$game_textures$", RelativePath, TexturePath))
				{
					TexturePath += Ext;
					if (auto* Texture = FbxFileTexture::Create(Scene, Name + Ext))
					{
						Texture->SetFileName(TexturePath.c_str());
						Texture->SetTextureUse(FbxTexture::eStandard);
						Texture->SetMappingType(FbxTexture::eUV);
						Texture->SetScale(1.0, -1.0);
						Texture->ConnectDstProperty(ColorProfile);
					}
				}
			}
		}
	}
}

void FbxStalkerExportLevelCollision(
	xray_re::xr_level_cform* Cform,
	FbxScene* Scene)
{
	const auto& Verts = Cform->vertices();
	const auto& Faces = Cform->faces();

	if (Verts.empty() || Faces.empty())
	{
		return;
	}

	const auto Name = FbxString("UCX_") + Scene->GetName();

	auto* Mesh = FbxMesh::Create(Scene, Name);
	auto* Node = FbxNode::Create(Scene, Name);

	Mesh->InitControlPoints(Verts.size());

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	for (std::size_t VertId = 0; VertId < Verts.size(); ++VertId)
	{
		ControlPoints[VertId].Set(
			Verts[VertId].p.x,
			Verts[VertId].p.y,
			Verts[VertId].p.z
		);
	}

	for (std::size_t FaceId = 0; FaceId < Faces.size(); ++FaceId)
	{
		Mesh->BeginPolygon(0);
		Mesh->AddPolygon(Faces[FaceId].v0);
		Mesh->AddPolygon(Faces[FaceId].v1);
		Mesh->AddPolygon(Faces[FaceId].v2);
		Mesh->EndPolygon();
	}

	Node->AddNodeAttribute(Mesh);
	Scene->GetRootNode()->AddChild(Node);
}

void FbxStalkerExportScene(
	FbxManager* SdkManager,
	const char* LevelName,
	const char* XrayPathSpec,
	const char* TargetPath)
{
	char FileName[1024];
	int FileFormat = 0;

	xray_re::xr_file_system& Filesystem = xray_re::xr_file_system::instance();
	if (!Filesystem.initialize(XrayPathSpec))
	{
		FBXSDK_printf("Can't initialize xray path spec.\n");
		return;
	}

	xray_re::xr_level Level;
	if (!Level.load("$game_levels$", LevelName))
	{
		FBXSDK_printf("Failed to load game level '%s'.\n", LevelName);
		return;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, LevelName);
	if (Scene == nullptr)
	{
		FBXSDK_printf("Call to FbxScene::Create failed.\n");
		return;
	}

	// Switch to the x-ray coordinate system which is left handed,
	// the y axis points upward and the z axis towards to the look direction

	Scene->GetGlobalSettings().SetAxisSystem(
		FbxAxisSystem(
			FbxAxisSystem::EUpVector::eYAxis,
			FbxAxisSystem::EFrontVector::eParityOdd,
			FbxAxisSystem::ECoordSystem::eLeftHanded
		)
	);

	// Convert scene system unit size since x-ray unit equal to one meter

	FbxSystemUnit::m.ConvertScene(Scene);

	FbxStalkerExportLevelMaterials(Filesystem, Level.shaders(), Scene);
	FbxStalkerExportLevelVisuals(Level.visuals(), Level.shaders(), Scene);
//	FIXME: Must be covered by command line option
//	FbxStalkerExportLevelCollision(Level.cform(), Scene);

	std::snprintf(FileName, sizeof(FileName), "%s\\%s.fbx", TargetPath, LevelName);

	FbxExporter* Exporter = FbxExporter::Create(SdkManager, "");
	FileFormat = SdkManager->GetIOPluginRegistry()->GetNativeWriterFormat();
	if (Exporter->Initialize(FileName, FileFormat, SdkManager->GetIOSettings()) == false)
	{
		FBXSDK_printf("Call to FbxExporter::Initialize() failed.\n");
		FBXSDK_printf("Error returned: %s\n\n", Exporter->GetStatus().GetErrorString());
		return;
	}

	// Convert the entire scene back to the fbx coordinate system

	FbxAxisSystem(
		FbxAxisSystem::EUpVector::eYAxis,
		FbxAxisSystem::EFrontVector::eParityOdd,
		FbxAxisSystem::ECoordSystem::eRightHanded
	).DeepConvertScene(Scene);

	Exporter->Export(Scene);
	Exporter->Destroy();
	Scene->Destroy();
}

} // anonymous namespace

int main()
{
	FbxManager* SdkManager = FbxManager::Create();
	FbxIOSettings* IOSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
	IOSettings->SetBoolProp(EXP_FBX_EMBEDDED, IOSEnabled);
	SdkManager->SetIOSettings(IOSettings);

	FbxStalkerExportScene(
		SdkManager,
		"l11_pripyat", "D:\\projects\\stalker\\fsgame.ltx",
		"D:\\Projects\\fbxgame");

	SdkManager->Destroy();
}
