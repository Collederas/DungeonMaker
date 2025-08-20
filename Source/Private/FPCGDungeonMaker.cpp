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

ECellType FDungeonPCGElement::GetCellTypeAt(const FGrid3D<FCellData>& Grid, int32 x, int32 y, int32 z)
{
	if (Grid.InBounds(FIntVector(x, y, z)))
	{
		return Grid(x, y, z).Type;
	}
	return ECellType::None;
}

void FDungeonPCGElement::GeneratePCGAttributes(const FGrid3D<FCellData>& Grid, FPCGContext* Context, const UDungeonPCGSettings* Settings)
{
    UPCGPointData* PointData = NewObject<UPCGPointData>();
    TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
    UPCGMetadata* Metadata = PointData->MutableMetadata();
    if (!Metadata) return;

    auto* MeshTypeAttr = Metadata->FindOrCreateAttribute<FString>(TEXT("MeshType"), FString());

    const FTransform& TargetActorTransform = Context->GetTargetActor(nullptr)->GetActorTransform();
    const FIntVector GridSize = Grid.GetGridSizeVector();

    for (int32 z = 0; z < GridSize.Z; ++z)
    {
        for (int32 y = 0; y < GridSize.Y; ++y)
        {
            for (int32 x = 0; x < GridSize.X; ++x)
            {
                const FIntVector CurrentCell(x, y, z);
                const FCellData& Cell = Grid(CurrentCell);
                const ECellType CellType = Cell.Type;

                if (CellType == ECellType::None)
                {
                    continue;
                }

                const FVector LocalPosition = FVector(CurrentCell) * Settings->GridUnitSize;

            	switch (CellType)
                {
                    case ECellType::Room:
                    case ECellType::Hallway:
                    {
                        // For Rooms and Hallways, we generate the boundary surfaces by checking neighbors. The
                    	// DungeonGenerator only generates full tiles (room, hallway) not ceiling, floors, walls
                    	// so we have to do some calculations here to identify them.

                        // Check BELOW for Floor
                        if (GetCellTypeAt(Grid, x, y, z - 1) == ECellType::None || CellType == ECellType::Hallway)
                        {
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(LocalPosition) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Floor"));
                        }

                        // Check ABOVE for Ceiling (+ check to not spawn ceiling if above we have stairs)
                        bool bIsStairRamp = (GetCellTypeAt(Grid, x, y, z + 1) == ECellType::Stairs);
                        if (!bIsStairRamp && GetCellTypeAt(Grid, x, y, z + 1) == ECellType::None)
                        {
                            const FVector CeilingLocalPos = LocalPosition + FVector(0, 0, Settings->GridUnitSize);
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(CeilingLocalPos) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Ceiling"));
                        }

                        // Check horizontal neighbors for walls
                        if (GetCellTypeAt(Grid, x + 1, y, z) == ECellType::None) // North
                        {
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(FRotator(0, 0, 0), LocalPosition) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Wall"));
                        }
                        if (GetCellTypeAt(Grid, x, y + 1, z) == ECellType::None) // East
                        {
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(FRotator(0, 90, 0), LocalPosition) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Wall"));
                        }
                        if (GetCellTypeAt(Grid, x - 1, y, z) == ECellType::None) // South
                        {
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(FRotator(0, 180, 0), LocalPosition) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Wall"));
                        }
                        if (GetCellTypeAt(Grid, x, y - 1, z) == ECellType::None) // West
                        {
                            FPCGPoint& Point = Points.Emplace_GetRef();
                            Point.Transform = FTransform(FRotator(0, -90, 0), LocalPosition) * TargetActorTransform;
                            Point.MetadataEntry = Metadata->AddEntry();
                            MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("Wall"));
                        }
                        break;
                    }
                    case ECellType::Stairs:
                    {
                    	switch (Cell.StairPart)
                    	{
                    		case EStairPart::UpperMesh:
                    			{
                    				FPCGPoint& Point = Points.Emplace_GetRef();
                    				Point.Transform = FTransform(Cell.Rotation, LocalPosition - FVector(0,0, Settings->GridUnitSize / 2)) * TargetActorTransform;
                    				Point.MetadataEntry = Metadata->AddEntry();
                    				MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("StairsUpper"));
                    				break;
                    			}
                    		case EStairPart::LowerMesh:
                    			{
                    				FPCGPoint& Point = Points.Emplace_GetRef();
                    				Point.Transform = FTransform(Cell.Rotation, LocalPosition) * TargetActorTransform;
                    				Point.MetadataEntry = Metadata->AddEntry();
                    				MeshTypeAttr->SetValue(Point.MetadataEntry, TEXT("StairsLower"));
                    				break;
                    		}
                    		case EStairPart::EmptySpace:
                    		case EStairPart::None:
                    		default:
                    			// Do nothing for empty parts of the stairwell
                    			break;
                    	}
                    }
                    default:
                        break;
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
	
	FGrid3D<FCellData> Grid = DungeonGenerator->GenerateDungeon();
	
	GeneratePCGAttributes(Grid, Context, Settings);
	return true;
}
