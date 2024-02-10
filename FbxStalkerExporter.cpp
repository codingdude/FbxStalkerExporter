#include <fbxsdk.h>

#include "xray_re/xr_file_system.h"
#include "xray_re/xr_level.h"
#include "xray_re/xr_level_visuals.h"
#include "xray_re/xr_ogf.h"
#include "xray_re/xr_ogf_v4.h"

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
	Mesh->InitNormals(NumVerts);

	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	FbxGeometryElementNormal* ElementNormal = Mesh->CreateElementNormal();
	ElementNormal->SetMappingMode(FbxGeometryElement::eByControlPoint);

	FbxGeometryElementUV* UVDiffuseElement = Mesh->CreateElementUV("DiffuseUV");
	UVDiffuseElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
	UVDiffuseElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

	for (int VertId = 0; VertId < NumVerts; ++VertId)
	{
		ControlPoints[VertId].Set(
			Vert[VertId].x,
			Vert[VertId].y,
			Vert[VertId].z
		);
		ElementNormal->GetDirectArray().Add(
			FbxVector4(
				Norm[VertId].x,
				Norm[VertId].y,
				Norm[VertId].z
			)
		);
		UVDiffuseElement->GetDirectArray().Add(
			FbxVector2(
				UV[VertId].u,
				UV[VertId].v
			)
		);
	}

	UVDiffuseElement->GetIndexArray().SetCount(NumFaces * VertsPerFace);
	for (int FaceId = 0; FaceId < NumFaces; ++FaceId)
	{
		Mesh->BeginPolygon(-1, -1, -1, false);
		for (int VertexId = 0; VertexId < VertsPerFace; ++VertexId)
		{
			const int Index = FaceId * VertsPerFace + VertexId;
			Mesh->AddPolygon(IndexBuffer[Index]);
			UVDiffuseElement->GetIndexArray().SetAt(Index, VertexId);
		}
		Mesh->EndPolygon();
	}

	return true;
}

void FbxStalkerExportLevelVisuals(
	const xray_re::xr_level_visuals* LevelVisuals,
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

void FbxStalkerExportScene(
	FbxManager* SdkManager,
	const char* LevelName,
	const char* XrayPathSpec,
	const char* TargetPath)
{
	char FileName[1024];
	int FileFormat = 0;

	xray_re::xr_file_system& fs = xray_re::xr_file_system::instance();
	if (!fs.initialize(XrayPathSpec))
	{
		FBXSDK_printf("Can't initialize xray path spec.\n");
		return;
	}

	xray_re::xr_level level;
	if (!level.load("$game_levels$", LevelName))
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

	FbxStalkerExportLevelVisuals(level.visuals(), Scene);

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

int main()
{
	FbxManager* SdkManager = FbxManager::Create();
	FbxStalkerExportScene(
		SdkManager,
		"l01_escape", "D:\\projects\\stalker\\fsgame.ltx",
		"D:\\Projects\\fbxgame");
	SdkManager->Destroy();
}
