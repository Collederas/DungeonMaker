#include "FPCGDungeonMaker.h"

#include "DungeonGenerator.h"
#include "DungeonMaker.h"
#include "DungeonTypes.h"
#include "Grid3D.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "UObject/FastReferenceCollector.h"


FPCGElementPtr UDungeonPCGSettings::CreateElement() const
{
	return MakeShared<FDungeonPCGElement>();
}

bool FDungeonPCGElement::IsCellConnected(const FGrid3D<ECellType>& Grid, FIntVector CellCoord)
{
	if (!Grid.InBounds(CellCoord))
	{
		return false;
	}

	return Grid(CellCoord) != ECellType::None;
}


void FDungeonPCGElement::GeneratePCGAttributes(FGrid3D<ECellType>& Grid, FPCGContext* Context, const UDungeonPCGSettings* Settings)
{
	UPCGPointData* PointData = NewObject<UPCGPointData>();
	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
	UPCGMetadata* Metadata = PointData->MutableMetadata();
	if (!Metadata)
	{
		return;
	}
	auto* MeshTypeAttr = Metadata->FindOrCreateAttribute<FString>(TEXT("MeshType"), FString());
	auto* YawAttr = Metadata->FindOrCreateAttribute<float>(TEXT("Orientation_Yaw"), 0.0f);

	const FTransform& TargetActorTransform = Context->GetTargetActor(nullptr)->GetActorTransform();
	const FIntVector GridSize = Grid.GetGridSizeVector();

	for (int32 z = 0; z < GridSize.Z; ++z)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			for (int32 x = 0; x < GridSize.X; ++x)
			{
				const FIntVector CurrentCell(x, y, z);
				const ECellType CellType = GetCell(Grid, x, y, z);

				if (CellType == ECellType::None)
				{
					continue;
				}
				const FVector LocalPosition = FVector(CurrentCell) * Settings->GridUnitSize;
				const FTransform PointBaseTransform(LocalPosition);

				bool bIsStairRamp = (CellType == ECellType::Stairs && GetCell(Grid, x, y, z + 1) == ECellType::Stairs);

				if (GetCell(Grid, x, y, z - 1) == ECellType::None)
				{
					FPCGPoint& FloorPoint = Points.Emplace_GetRef();
					FloorPoint.Transform = PointBaseTransform * TargetActorTransform;
					FloorPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(FloorPoint.MetadataEntry, TEXT("Floor"));
				}

				if (GetCell(Grid, x, y, z + 1) == ECellType::None && !bIsStairRamp)
				{
					const FVector CeilingLocalPosition = LocalPosition + FVector(0, 0, Settings->GridUnitSize);
					FPCGPoint& CeilPoint = Points.Emplace_GetRef();
					CeilPoint.Transform = FTransform(CeilingLocalPosition) * TargetActorTransform;
					CeilPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(CeilPoint.MetadataEntry, TEXT("Ceiling"));
				}

				// Check North (+X)
				if (GetCell(Grid, x + 1, y, z) == ECellType::None)
				{
					const FRotator WallRotation(0.0f, 0.0f, 0.0f);
					FPCGPoint& WallPoint = Points.Emplace_GetRef();
					WallPoint.Transform = FTransform(WallRotation, LocalPosition) * TargetActorTransform;
					WallPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
				}
				// Check East (+Y)
				if (GetCell(Grid, x, y + 1, z) == ECellType::None)
				{
					const FRotator WallRotation(0.0f, 90.0f, 0.0f);
					FPCGPoint& WallPoint = Points.Emplace_GetRef();
					WallPoint.Transform = FTransform(WallRotation, LocalPosition) * TargetActorTransform;
					WallPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
				}
				// Check South (-X)
				if (GetCell(Grid, x - 1, y, z) == ECellType::None)
				{
					const FRotator WallRotation(0.0f, 180.0f, 0.0f);
					FPCGPoint& WallPoint = Points.Emplace_GetRef();
					WallPoint.Transform = FTransform(WallRotation, LocalPosition) * TargetActorTransform;
					WallPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
				}
				// Check West (-Y)
				if (GetCell(Grid, x, y - 1, z) == ECellType::None)
				{
					const FRotator WallRotation(0.0f, -90.0f, 0.0f);
					FPCGPoint& WallPoint = Points.Emplace_GetRef();
					WallPoint.Transform = FTransform(WallRotation, LocalPosition) * TargetActorTransform;
					WallPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
				}

				if (CellType == ECellType::Stairs)
				{
					float Yaw = 0.0f;
					if (IsCellConnected(Grid, FIntVector(x + 1, y, z)))
					{
						Yaw = 0.0f;
					}
					else if (IsCellConnected(Grid, FIntVector(x - 1, y, z)))
					{
						Yaw = 180.0f;
					}
					else if (IsCellConnected(Grid, FIntVector(x, y + 1, z)))
					{
						Yaw = 90.0f;
					}
					else if (IsCellConnected(Grid, FIntVector(x, y - 1, z)))
					{
						Yaw = -90.0f;
					}

					const FRotator StairRotation(0.0f, Yaw, 0.0f);
					FPCGPoint& StairPoint = Points.Emplace_GetRef();
					StairPoint.Transform = FTransform(StairRotation, LocalPosition) * TargetActorTransform;
					StairPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(StairPoint.MetadataEntry, TEXT("Stairs"));
				}
			}
		}
	}
	FPCGTaggedData& OutputData = Context->OutputData.TaggedData.Emplace_GetRef();
	OutputData.Data = PointData;
	OutputData.Pin = PCGPinConstants::DefaultOutputLabel;
}

ECellType FDungeonPCGElement::GetCell(const FGrid3D<ECellType>& Grid, int32 x, int32 y, int32 z)
{
	if (Grid.InBounds(FIntVector(x, y, z)))
	{
		return Grid(x, y, z);
	}
	return ECellType::None;
}

bool FDungeonPCGElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDungeonPCGElement::Execute);

	const UDungeonPCGSettings* Settings = Context->GetInputSettings<UDungeonPCGSettings>();
	if (!Settings)
	{
		UE_LOG(LogDungeonMaker, Warning, TEXT("No Input Settings provided for Dungeon Generation. Aborting."));
		return true;
	}

	UDungeonGenerator* DungeonGenerator = NewObject<UDungeonGenerator>();
	DungeonGenerator->Init(
		Context->GetTargetActor(nullptr),
		Settings->GridSize,
		Settings->GridUnitSize,
		Settings->NumberOfRooms,
		Settings->RoomSizeMinMax,
		Settings->RoomHeightInFloorsMinMax,
		Settings->ExtraConnectionChance);
	
	FGrid3D<ECellType> Grid = DungeonGenerator->GenerateDungeon();
	
	GeneratePCGAttributes(Grid, Context, Settings);
	return true;
}
