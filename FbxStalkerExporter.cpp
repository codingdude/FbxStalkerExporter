#include <fbxsdk.h>

#include "xray_re/xr_envelope.h"
#include "xray_re/xr_file_system.h"
#include "xray_re/xr_ini_file.h"
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
	const int Pos = FilePath.ReverseFind('\\');

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

FbxSurfaceMaterial* FbxStalkerExportMaterial(
	const xray_re::xr_file_system& Filesystem,
	const FbxString& MaterialPath,
	FbxScene* Scene)
{
	const FbxString Name = FbxStalkerGetBaseFilename(MaterialPath);
	FbxSurfaceMaterial* Material = Scene->GetMaterial(Name);
	if (Material)
	{
		return Material;
	}

	if (!Name.IsEmpty() && !Scene->GetTexture(Name))
	{
		Material = FbxSurfacePhong::Create(Scene, Name);
		if (!Material)
		{
			return nullptr;
		}

		auto ColorProfile = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (ColorProfile.IsValid())
		{
			const char* Ext = ".png";

			std::string TexturePath;
			if (Filesystem.resolve_path("$game_textures$", MaterialPath, TexturePath))
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

	return Material;
}

FbxNode* FbxStalkerExportBone(
	const xray_re::xr_bone* Bone,
	FbxScene* Scene,
	FbxSkeleton::EType Type)
{
	auto* Limb = FbxSkeleton::Create(Scene, "");
	auto* Node = FbxNode::Create(Scene, Bone->name().c_str());

	Limb->SetSkeletonType(Type);
	Limb->Size.Set(0.5);

	Node->SetNodeAttribute(Limb);
	Node->SetUserDataPtr(
		const_cast<xray_re::xr_bone*>(Bone));

	Node->LclTranslation.Set(
		FbxDouble3(
			Bone->bind_offset().x,
			Bone->bind_offset().y,
			Bone->bind_offset().z
		));

	xray_re::fmatrix Xform;
	Xform.set_xyz_i(
		Bone->bind_rotate().x,
		Bone->bind_rotate().y,
		Bone->bind_rotate().z);

	float X, Y, Z;
	Xform.get_euler_xyz(X, Y, Z);
	const float RadToDeg = static_cast<float>(180.0 / M_PI);
	Node->LclRotation.Set(FbxDouble3(X * RadToDeg, Y * RadToDeg, Z * RadToDeg));

	return Node;
}

FbxNode* FbxStalkerExportSkeleton(
	const xray_re::xr_bone_vec& Bones,
	FbxScene* Scene,
	FbxNode* Root = nullptr)
{
	if (Root == nullptr)
	{
		for (const auto* Bone : Bones)
		{
			if (Bone->is_root())
			{
				Root = FbxStalkerExportBone(
					Bone, Scene, FbxSkeleton::eRoot);
				break;
			}
		}

		if (Root == nullptr)
		{
			return Root;
		}
	}

	const auto& Bone = static_cast<xray_re::xr_bone*>(
		Root->GetUserDataPtr());
	for (const auto* Child : Bone->children())
	{
		auto Node = FbxStalkerExportBone(
			Child, Scene, FbxSkeleton::eLimbNode);
		auto Skeleton = FbxStalkerExportSkeleton(
			Bones, Scene, Node);
		Root->AddChild(Skeleton);
	}

	return Root;
}

FbxNode* FbxStalkerGetBone(FbxNode* Skeleton, int BoneId)
{
	auto Bone = reinterpret_cast<xray_re::xr_bone*>(
		Skeleton->GetUserDataPtr());
	if (Bone && Bone->id() == BoneId)
	{
		return Skeleton;
	}

	for (int ChildId = 0; ChildId < Skeleton->GetChildCount(); ++ChildId)
	{
		auto Child = Skeleton->GetChild(ChildId);
		if (auto Bone = FbxStalkerGetBone(Child, BoneId))
		{
			return Bone;
		}
	}

	return nullptr;
}

void FbxStalkerInitClusters(FbxNode* Skeleton, FbxSkin* Skin, FbxScene* Scene)
{
	auto Matrix = Skeleton->EvaluateGlobalTransform();
	auto Cluster = FbxCluster::Create(Scene, "");

	Cluster->SetLink(Skeleton);
	Cluster->SetLinkMode(FbxCluster::eTotalOne);
	Cluster->SetTransformLinkMatrix(Matrix);
	Skin->AddCluster(Cluster);

	for (int ChildId = 0; ChildId < Skeleton->GetChildCount(); ++ChildId)
	{
		auto Child = Skeleton->GetChild(ChildId);
		FbxStalkerInitClusters(Child, Skin, Scene);
	}
}

void FbxStalkerCreateSkin(
	FbxNode* Skeleton,
	FbxNode* Visual,
	FbxScene* Scene,
	const xray_re::xr_vbuf& Verts)
{
	auto Mesh = Visual->GetMesh();
	if (Mesh == nullptr)
	{
		FBXSDK_printf(
			"Unable to apply skin to scene visual '%s'.\n",
			Visual->GetName());
		return;
	}

	auto Skin = FbxSkin::Create(Scene, "");
	
	FbxStalkerInitClusters(Skeleton, Skin, Scene);
	for (int VertId = 0; VertId < Verts.size(); ++VertId)
	{
		const auto& Influences = Verts.w(VertId);
		for (int InfluenceId = 0; InfluenceId < static_cast<int>(Influences.count); ++InfluenceId)
		{
			const auto& Influence = Influences[InfluenceId];

			FbxNode* Bone = FbxStalkerGetBone(
				Skeleton, Influence.bone);
			if (Bone == nullptr)
			{
				FBXSDK_printf(
					"Unexpected bone index #%d used while trying to export skin",
					Influence.bone);
				Skin->Destroy();
				return;
			}

			FbxCluster* Cluster = Skin->GetCluster(Influence.bone);
			if (Cluster == nullptr)
			{
				FBXSDK_printf(
					"Unexpected bone index #%d used while trying to export skin",
					Influence.bone);
				Skin->Destroy();
				return;
			}

			Cluster->AddControlPointIndex(VertId, Influence.weight);
		}
	}

	Mesh->AddDeformer(Skin);
}

FbxNode* FbxStalkerExportSkinnedVisual(
	const xray_re::xr_file_system& Filesystem,
	const xray_re::xr_ogf* Ogf,
	FbxScene* Scene,
	const char* Name)
{
	FbxNode* Node = FbxNode::Create(Scene, Name);
	FbxMesh* Mesh = FbxMesh::Create(Scene, Name);

	if (!FbxStalkerExportStaticMesh(Ogf, Mesh))
	{
		Mesh->Destroy();
		Node->Destroy();
		return nullptr;
	}
	Node->AddNodeAttribute(Mesh);

	const auto MaterialName = Ogf->texture();
	if (!MaterialName.empty())
	{
		FbxSurfaceMaterial* Material =
			FbxStalkerExportMaterial(
				Filesystem,
				MaterialName.c_str(),
				Scene);

		if (Material)
		{
			Node->AddMaterial(Material);
		}
	}

	return Node;
}

void FbxStalkerExportSkinnedVisuals(
	const xray_re::xr_file_system& Filesystem,
	const xray_re::xr_ogf* Ogf,
	FbxScene* Scene)
{
	int Count = 0;
	char Buffer[1024];

	if (!Ogf->skeletal())
	{
		FBXSDK_printf(
			"Trying to export non-skinned model as dynamic '%s'.\n",
			Scene->GetName());
		return;
	}

	FbxNode* Skeleton = FbxStalkerExportSkeleton(Ogf->bones(), Scene);
	if (!Skeleton)
	{
		FBXSDK_printf("Can't export skeleton hierarchy");
		return;
	}
	Scene->GetRootNode()->AddChild(Skeleton);

	for (const auto Body : Ogf->children())
	{
		std::snprintf(Buffer, sizeof(Buffer), "%s_%d", Scene->GetName(), Count++);
		auto Node = FbxStalkerExportSkinnedVisual(Filesystem, Body, Scene, Buffer);
		if (Node == nullptr)
		{
			FBXSDK_printf("Can't export skinned mesh '%s'.\n", Buffer);
			continue;
		}
		Scene->GetRootNode()->AddChild(Node);
		FbxStalkerCreateSkin(Skeleton, Node, Scene, Body->vb());
	}
}

void FbxStalkerExportMotion(
	const xray_re::xr_skl_motion* Motion,
	FbxNode* Skeleton,
	FbxScene* Scene)
{
	const float RadToDeg = static_cast<float>(180.0 / M_PI);

	const char* const CurveComponents[] = {
		FBXSDK_CURVENODE_COMPONENT_X,
		FBXSDK_CURVENODE_COMPONENT_Y,
		FBXSDK_CURVENODE_COMPONENT_Z
	};

	FbxTime Time;

	auto AnimStack = FbxAnimStack::Create(Scene, Motion->name().c_str());
	auto AnimLayer = FbxAnimLayer::Create(Scene, "Base Layer");
	AnimStack->AddMember(AnimLayer);

	const auto& BoneMotions = Motion->bone_motions();
	for (int BoneId = 0; BoneId < BoneMotions.size(); ++BoneId)
	{
		auto BoneMotion = BoneMotions[BoneId];
		auto Bone = FbxStalkerGetBone(Skeleton, BoneId);

		FbxSet<float> Timeline;
		for (int EnvelopeId = 3; EnvelopeId < 6; ++EnvelopeId)
		{
			const auto Envelope = BoneMotion->envelopes()[EnvelopeId];
			for (auto Key : Envelope->keys())
			{
				Timeline.Insert(Key->time);
			}
		}

		FbxMap<float, FbxDouble3> RotationKeys;
		for (auto Time = Timeline.Begin(); Time != Timeline.End(); ++Time)
		{
			xray_re::fmatrix Xform;
			xray_re::fvector3 Translation, Rotation;
			BoneMotion->evaluate(Time->GetValue(), Translation, Rotation);
			Xform.set_xyz_i(Rotation);
			Xform.get_euler_xyz(Rotation);
			RotationKeys.Insert(
				Time->GetValue(),
				{ Rotation.x,  Rotation.y, Rotation.z });
		}

		for (int EnvId = 0; EnvId < 6; ++EnvId)
		{
			FbxAnimCurve* Curve;
			const auto Component = CurveComponents[EnvId % 3];
			const auto& Envelope = BoneMotion->envelopes()[EnvId];

			if (EnvId < 3)
			{
				Curve = Bone->LclTranslation.GetCurve(AnimLayer, Component, true);
				Curve->KeyModifyBegin();
				for (const auto& Key : Envelope->keys())
				{
					Time.SetSecondDouble(Key->time);
					int KeyIndex = Curve->KeyAdd(Time);
					Curve->KeySetValue(KeyIndex, Key->value);
					Curve->KeySetInterpolation(KeyIndex, FbxAnimCurveDef::eInterpolationCubic);
				}
				Curve->KeyModifyEnd();
			}
			else
			{
				Curve = Bone->LclRotation.GetCurve(AnimLayer, Component, true);
				Curve->KeyModifyBegin();
				for (auto Key = RotationKeys.Begin(); Key != RotationKeys.End(); ++Key)
				{
					Time.SetSecondDouble(Key->GetKey());
					int KeyIndex = Curve->KeyAdd(Time);
					Curve->KeySetValue(KeyIndex, static_cast<float>(Key->GetValue()[EnvId % 3]) * RadToDeg);
					Curve->KeySetInterpolation(KeyIndex, FbxAnimCurveDef::eInterpolationCubic);
				}
				Curve->KeyModifyEnd();
			}
		}
	}

	FbxAnimCurveFilterResample().Apply(AnimStack);
}

void FbxStalkerExportMotions(
	const xray_re::xr_file_system& Filesystem,
	xray_re::xr_ogf* Ogf,
	FbxScene* Scene)
{
	auto RootNode = Scene->GetRootNode();
	if (RootNode == nullptr)
	{
		return;
	}

	FbxNode* Skeleton = nullptr;
	for (int ChildId = 0; ChildId < RootNode->GetChildCount(); ++ChildId)
	{
		auto Node = RootNode->GetChild(ChildId);
		if (Node->GetSkeleton() != nullptr)
		{
			Skeleton = Node;
		}
	}

	if (Skeleton == nullptr)
	{
		return;
	}

	xray_re::xr_skl_motion_vec motions = Ogf->motions();
	if (auto OgfV4 = static_cast<xray_re::xr_ogf_v4*>(Ogf))
	{
		const char* Span = ",";
		const char* Ext = ".omf";

		const FbxString MotionRefs = OgfV4->motion_refs().c_str();
		for (int TokenId = 0; TokenId < MotionRefs.GetTokenCount(Span); ++TokenId)
		{
			std::string Path;
			auto MotionRef = MotionRefs.GetToken(TokenId, Span);
			Filesystem.resolve_path(xray_re::PA_GAME_MESHES, MotionRef, Path);
			OgfV4->load_omf((Path + Ext).c_str());
			motions.insert(motions.end(), Ogf->motions().begin(), Ogf->motions().end());
		}
	}

	for (const auto& Motion : motions)
	{
		FbxStalkerExportMotion(Motion, Skeleton, Scene);
	}
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
	for (const auto& RelativePath : Shaders->textures())
	{
		FbxStalkerExportMaterial(
			Filesystem,
			RelativePath.c_str(),
			Scene);
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

	const auto Name = FbxString(Scene->GetName()) + "_cform";
	if (Scene->FindNodeByName(Name))
	{
		FBXSDK_printf("Collision form already exists.\n");
		return;
	}

	auto* Mesh = FbxMesh::Create(Scene, Name);
	auto* Node = FbxNode::Create(Scene, Name);

	Mesh->InitControlPoints(static_cast<int>(Verts.size()));

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

FbxScene* FbxStalkerBeginExportScene(
	FbxManager* SdkManager,
	const char* SceneName)
{
	FbxScene* Scene = FbxScene::Create(SdkManager, SceneName);
	if (Scene == nullptr)
	{
		FBXSDK_printf("Call to FbxScene::Create failed.\n");
		return nullptr;
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

	return Scene;
}

void FbxStalkerEndExportScene(
	FbxManager* SdkManager,
	const char* TargetPath,
	FbxScene* Scene)
{
	char FileName[1024];
	int FileFormat = 0;

	std::snprintf(FileName, sizeof(FileName), "%s\\%s.fbx", TargetPath, Scene->GetName());

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

void FbxStalkerExportLevel(
	FbxManager* SdkManager,
	const char* LevelName,
	const char* XrayPathSpec,
	const char* TargetPath)
{
	xray_re::xr_file_system& Filesystem = xray_re::xr_file_system::instance();
	if (!Filesystem.initialize(XrayPathSpec))
	{
		FBXSDK_printf("Can't initialize xray path spec.\n");
		return;
	}

	xray_re::xr_level Level;
	if (!Level.load(xray_re::PA_GAME_LEVELS, LevelName))
	{
		FBXSDK_printf("Failed to load game level '%s'.\n", LevelName);
		return;
	}

	FbxScene* Scene = FbxStalkerBeginExportScene(SdkManager, LevelName);
	if (!Scene)
	{
		FBXSDK_printf("Failed to create FBX level '%s'.\n", LevelName);
		return;
	}

	FbxStalkerExportLevelMaterials(Filesystem, Level.shaders(), Scene);
	FbxStalkerExportLevelVisuals(Level.visuals(), Level.shaders(), Scene);
	FbxStalkerExportLevelCollision(Level.cform(), Scene);

	FbxStalkerEndExportScene(SdkManager, TargetPath, Scene);
}

void FbxStalkerExportActor(
	FbxManager* SdkManager,
	const char* ActorName,
	const char* XrayPathSpec,
	const char* TargetPath)
{
	xray_re::xr_file_system& Filesystem = xray_re::xr_file_system::instance();
	if (!Filesystem.initialize(XrayPathSpec))
	{
		FBXSDK_printf("Can't initialize xray path spec.\n");
		return;
	}

	std::string VisualPath;
	Filesystem.resolve_path(xray_re::PA_GAME_MESHES, ActorName, VisualPath);
	const auto Ogf = xray_re::xr_ogf::load_ogf(VisualPath + ".ogf");
	if (!Ogf)
	{
		FBXSDK_printf("Can't load game visual '%s'\n", VisualPath.c_str());
		return;
	}

	const auto Name = FbxStalkerGetBaseFilename(ActorName);
	FbxScene* Scene = FbxStalkerBeginExportScene(SdkManager, Name);
	if (!Scene)
	{
		FBXSDK_printf("Failed to create FBX actor '%s'.\n", Name.Buffer());
		return;
	}

	FbxStalkerExportSkinnedVisuals(Filesystem, Ogf, Scene);
	FbxStalkerExportMotions(Filesystem, Ogf, Scene);
	FbxStalkerEndExportScene(SdkManager, TargetPath, Scene);
}

} // anonymous namespace

int main()
{
	FbxManager* SdkManager = FbxManager::Create();
	FbxIOSettings* IOSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
	IOSettings->SetBoolProp(EXP_FBX_EMBEDDED, IOSEnabled);
	SdkManager->SetIOSettings(IOSettings);

	// FIXME: must be replaced with if-else statement when command line parser will be present
#if 0
	FbxStalkerExportLevel(
		SdkManager,
		"l11_pripyat", "D:\\projects\\stalker\\fsgame.ltx",
		"D:\\Projects\\fbxgame");
#else
	FbxStalkerExportActor(
		SdkManager,
		"monsters\\krovosos\\krovosos",
		"D:\\projects\\stalker\\fsgame.ltx",
		"D:\\Projects\\fbxgame");

	SdkManager->Destroy();
#endif
}
