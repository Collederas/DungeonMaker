#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "Pathfinder3D.h"
#include "PCGSettings.h"

#include "FPCGDungeonMaker.generated.h"


UCLASS(MinimalAPI, BlueprintType, ClassGroup=(Procedural))
class UDungeonPCGSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DungeonLayoutGenerator")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("DungeonPCG", "NodeTitle", "Dungeon Layout Generator"); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
	//~End UPCGSettings interface

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "10"))
	FIntVector GridSize = FIntVector(20, 20, 3);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "1"))
	float GridUnitSize = 400.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	int32 NumberOfRooms = 5;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms")
	FIntPoint RoomSizeMinMax = FIntPoint(4, 10);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	FIntPoint RoomHeightInFloorsMinMax = FIntPoint(1, 2);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExtraConnectionChance = 0.2f;
};

class FDungeonPCGElement : public IPCGElement
{
protected:
	static FVector GridToWorld(const FIntVector& GridPos, float GridUnitSize);
	static FPathCost CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom, FGrid3D<ECellType>& Grid);
	
	static void PlaceRooms(FGrid3D<ECellType>& Grid, TArray<FDungeonRoom>& DungeonRooms, const UDungeonPCGSettings* Settings, FRandomStream& InRandomStream);
	static void CalculateDelaunayTetrahedralization(TArray<TTuple<int32, int32>>& DelaunayEdges, const TArray<FDungeonRoom>& DungeonRooms);
	static void CalculateMst(TArray<TTuple<int32, int32>>& MstEdges, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& DelaunayEdges);
	static void CreateHallwayEdges(TArray<TTuple<int32, int32>>& HallwayEdges, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& MstEdges,
		const TArray<TTuple<int32, int32>>& DelaunayEdges, const UDungeonPCGSettings* Settings, FRandomStream& InRandomStream);

	static void PathfindHallways(FGrid3D<ECellType>& Grid, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& HallwayEdges, const UDungeonPCGSettings* Settings);
	static void ProcessPath(FGrid3D<ECellType>& Grid, const TArray<FIntVector>& Path);
	static void ProcessPathTile(FGrid3D<ECellType>& Grid, const FIntVector& CurrentPathPoint, const FIntVector& PreviousPathPoint, bool bIsFirstTile);

	static void GeneratePCGAttributes(FGrid3D<ECellType>& Grid, FPCGContext* Context, const UDungeonPCGSettings* Settings);
	static ECellType GetCell(const FGrid3D<ECellType>& Grid, int32 x, int32 y, int32 z);

	virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* Settings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
