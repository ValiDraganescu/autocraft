#include "AcModelCatalog.h"

#include "AcLog.h"
#include "AcSpace.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UStaticMesh* FAcModelMesh::LoadMesh() const
{
	return Cast<UStaticMesh>(Mesh.TryLoad());
}

UMaterialInterface* FAcModelMesh::LoadMaterial() const
{
	return Cast<UMaterialInterface>(MaterialInstance.TryLoad());
}

const FAcModelPart* FAcModelInfo::FindPart(const FName Part) const
{
	const int32* I = PartIndex.Find(Part);
	return I ? &Parts[*I] : nullptr;
}

FTransform FAcModelInfo::RestToModel(int32 Part) const
{
	FTransform T = FTransform::Identity;
	while (Parts.IsValidIndex(Part))
	{
		T = T * Parts[Part].Rest;
		Part = Parts[Part].Parent;
	}
	return T;
}

int32 FAcModelInfo::ExportTeam() const
{
	return Team == TEXT("red") ? 1 : 0;
}

FString FAcModelCatalog::DefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Models/ModelCatalog.json");
}

const FAcModelCatalog& FAcModelCatalog::Get()
{
	static FAcModelCatalog Catalog;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Catalog.Load(DefaultPath());
	}
	return Catalog;
}

const FAcModelInfo* FAcModelCatalog::Find(const FName Model) const
{
	const int32* I = Index.Find(Model);
	return I ? &Models[*I] : nullptr;
}

const FAcModelPart* FAcModelCatalog::FindPart(const FName Model, const FName Part) const
{
	const FAcModelInfo* M = Find(Model);
	return M ? M->FindPart(Part) : nullptr;
}

const FAcModelMesh* FAcModelCatalog::FindMesh(const FName Model, const FName Part, const FName Material) const
{
	const FAcModelPart* P = FindPart(Model, Part);
	if (!P) return nullptr;
	return P->Meshes.FindByPredicate([&](const FAcModelMesh& M) { return M.Material == Material; });
}

FTransform FAcModelCatalog::TransformFromSceneKitMatrix(const TArray<double>& M)
{
	if (M.Num() != 16) return FTransform::Identity;
	// v' = v · M with rows X, Y, Z, T. Unreal = P · M · P with P swapping
	// Y and Z: row i of the result is SceneKit row p(i), its components
	// swapped the same way (AcSpace::AxesFromSceneKit), the translation in cm.
	auto Row = [&](const int32 R) { return AcSpace::AxesFromSceneKit(M[R * 4], M[R * 4 + 1], M[R * 4 + 2]); };
	const FVector X = Row(0), Y = Row(2), Z = Row(1);
	const FVector T = AcSpace::FromSceneKit(M[12], M[13], M[14]);
	return FTransform(FMatrix(FPlane(X, 0), FPlane(Y, 0), FPlane(Z, 0), FPlane(T, 1)));
}

namespace
{
	FVector Vec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() < 3) return Default;
		return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		TArray<FString> Out;
		O->TryGetStringArrayField(Field, Out);
		return Out;
	}

	EAcTeamChannel TeamChannels(const TArray<FString>& Names)
	{
		EAcTeamChannel C = EAcTeamChannel::None;
		for (const FString& N : Names)
		{
			if (N == TEXT("TeamTint")) C |= EAcTeamChannel::Tint;
			else if (N == TEXT("PaintBase")) C |= EAcTeamChannel::PaintBase;
			else if (N == TEXT("PaintEmissive")) C |= EAcTeamChannel::PaintEmissive;
			else if (N == TEXT("TeamGlow")) C |= EAcTeamChannel::Glow;
		}
		return C;
	}
}

bool FAcModelCatalog::Load(const FString& File)
{
	Models.Reset();
	Index.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogAutocraft, Error, TEXT("models: no catalog at %s (run unreal/Tools/Editor/import_models.py)"), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogAutocraft, Error, TEXT("models: %s is not JSON"), *File);
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* ModelValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("models"), ModelValues)) return false;

	for (const TSharedPtr<FJsonValue>& MV : *ModelValues)
	{
		const TSharedPtr<FJsonObject> MO = MV->AsObject();
		FAcModelInfo& M = Models.AddDefaulted_GetRef();
		M.Name = FName(MO->GetStringField(TEXT("name")));
		M.Base = FName(MO->GetStringField(TEXT("base")));
		MO->TryGetStringField(TEXT("team"), M.Team);
		MO->TryGetStringField(TEXT("category"), M.Category);
		MO->TryGetStringField(TEXT("facing"), M.Facing);
		MO->TryGetNumberField(TEXT("triangles"), M.Triangles);
		M.Notes = Strings(MO, TEXT("notes"));
		const FVector Lo = Vec3(MO, TEXT("boundsMin"), FVector::ZeroVector);
		const FVector Hi = Vec3(MO, TEXT("boundsMax"), FVector::ZeroVector);
		const FVector A = AcSpace::FromSceneKit(Lo.X, Lo.Y, Lo.Z), B = AcSpace::FromSceneKit(Hi.X, Hi.Y, Hi.Z);
		M.Bounds = FBox(A.ComponentMin(B), A.ComponentMax(B));

		const TArray<TSharedPtr<FJsonValue>>* PartValues = nullptr;
		MO->TryGetArrayField(TEXT("parts"), PartValues);
		TArray<FString> ParentNames;
		for (const TSharedPtr<FJsonValue>& PV : PartValues ? *PartValues : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject> PO = PV->AsObject();
			FAcModelPart& P = M.Parts.AddDefaulted_GetRef();
			P.Name = FName(PO->GetStringField(TEXT("name")));
			PO->TryGetStringField(TEXT("path"), P.Path);
			PO->TryGetStringField(TEXT("sceneKitName"), P.SceneKitName);
			P.Handles = Strings(PO, TEXT("handles"));
			TArray<double> Matrix;
			if (const TArray<TSharedPtr<FJsonValue>>* MA = nullptr; PO->TryGetArrayField(TEXT("localTransform"), MA))
			{
				for (const TSharedPtr<FJsonValue>& V : *MA) Matrix.Add(V->AsNumber());
			}
			P.Rest = TransformFromSceneKitMatrix(Matrix);
			P.SkPosition = Vec3(PO, TEXT("position"), FVector::ZeroVector);
			P.SkEuler = Vec3(PO, TEXT("eulerAngles"), FVector::ZeroVector);
			P.SkScale = Vec3(PO, TEXT("scale"), FVector::OneVector);
			PO->TryGetBoolField(TEXT("hidden"), P.bHidden);
			double Opacity = 1.0;
			if (PO->TryGetNumberField(TEXT("opacity"), Opacity)) P.Opacity = float(Opacity);
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (PO->TryGetObjectField(TEXT("driver"), Obj)) P.Driver = *Obj;
			if (PO->TryGetObjectField(TEXT("light"), Obj)) P.Light = *Obj;
			const TArray<TSharedPtr<FJsonValue>>* Particles = nullptr;
			if (PO->TryGetArrayField(TEXT("particles"), Particles)) P.Particles = *Particles;
			const TArray<TSharedPtr<FJsonValue>>* Rays = nullptr;
			if (PO->TryGetArrayField(TEXT("rays"), Rays)) P.Rays = *Rays;
			FString ParentName;
			PO->TryGetStringField(TEXT("parent"), ParentName);
			ParentNames.Add(ParentName);

			const TArray<TSharedPtr<FJsonValue>>* MeshValues = nullptr;
			PO->TryGetArrayField(TEXT("meshes"), MeshValues);
			for (const TSharedPtr<FJsonValue>& XV : MeshValues ? *MeshValues : TArray<TSharedPtr<FJsonValue>>())
			{
				const TSharedPtr<FJsonObject> XO = XV->AsObject();
				FAcModelMesh& X = P.Meshes.AddDefaulted_GetRef();
				X.Material = FName(XO->GetStringField(TEXT("material")));
				X.Prim = FName(XO->GetStringField(TEXT("prim")));
				X.Mesh = FSoftObjectPath(XO->GetStringField(TEXT("mesh")));
				X.MaterialInstance = FSoftObjectPath(XO->GetStringField(TEXT("materialInstance")));
				X.Blend = FName(XO->GetStringField(TEXT("blend")));
				X.Team = TeamChannels(Strings(XO, TEXT("team")));
				XO->TryGetNumberField(TEXT("triangles"), X.Triangles);
			}
			M.PartIndex.Add(P.Name, M.Parts.Num() - 1);
		}
		for (int32 I = 0; I < M.Parts.Num(); ++I)
		{
			if (ParentNames[I].IsEmpty()) continue;
			const int32* Parent = M.PartIndex.Find(FName(ParentNames[I]));
			if (!Parent)
			{
				UE_LOG(LogAutocraft, Warning, TEXT("models: %s/%s has an unknown parent %s"), *M.Name.ToString(), *M.Parts[I].Name.ToString(), *ParentNames[I]);
				continue;
			}
			M.Parts[I].Parent = *Parent;
		}
		Index.Add(M.Name, Models.Num() - 1);
	}
	UE_LOG(LogAutocraft, Log, TEXT("models: %d models from %s"), Models.Num(), *File);
	return Models.Num() > 0;
}
