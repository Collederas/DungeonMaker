#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "Grid3D.h"
#include "Pathfinder3D.h"
#include "GameFramework/Actor.h"
#include "Math/IntVector.h"
#include "Tetrahedralizer.h"
#include "DungeonGenerator.generated.h"

// TODO: discard corridors between rooms too far (? how to ensure connectivity though? maybe just try to place rooms closer to each-other)


UCLASS()
class DUNGEONMAKER_API UDungeonGenerator : public UObject
{
	GENERATED_BODY()

public:
	UDungeonGenerator();

	void Init(AActor* InOwner, FIntVector InGridSize, float InGridUnitSize, float InNumOfRooms, FIntPoint InRoomSizeMinMax, FIntPoint InRoomHeightMinMax, float InExtraConnectionChance);
	
	FGrid3D<FCellData> GenerateDungeon();

	void ClearDungeon();
	
	FGrid3D<FCellData> GetGrid() { return Grid; };
	
public:
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "10"))
	FIntVector GridSize = FIntVector(20, 20, 3);
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "1"))
	float GridUnitSize = 400.0f;
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	int32 NumberOfRooms = 5;
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms")
	FIntPoint RoomSizeMinMax = FIntPoint(4, 10);

	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	FIntPoint RoomHeightInFloorsMinMax = FIntPoint(1, 2);
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExtraConnectionChance = 0.2f;
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Debug")
	FColor DelaunayDebugColor = FColor::Magenta;
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Debug")
	FColor MstDebugColor = FColor::Green;
	
	UPROPERTY(Editanywhere, Category = "Dungeon Settings | Debug")
	FColor HallwayDebugColor = FColor::White;

protected:
	Pathfinder3D Pathfinder;

	// Used to transform grid to world.
	// TODO: move away. shouldnt be this object's responsibility
	UPROPERTY()
	AActor* OwningActor;

protected:
	FVector GridToWorld(const FIntVector& GridPos) const
	{
		const FVector WorldPos(
			GridPos.X * GridUnitSize,
			GridPos.Y * GridUnitSize,
			GridPos.Z * GridUnitSize
		);
		return WorldPos + OwningActor->GetActorLocation();
	}
	
private:
	FPathCost CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom);

	void PlaceRooms();
	void CalculateDelaunayTetrahedralization(); 
	void CalculateMst();
	void CreateHallways();
	void PathfindHallways();

	void ProcessPath(TArray<FIntVector> Path);
	TArray<FIntVector> PathfindHallwayEdge(int32 Edge);

private:
	UPROPERTY()
	TArray<FDungeonRoom> DungeonRooms;
	
	FGrid3D<FCellData> Grid;

	TArray<TTuple<int32, int32>> DelaunayEdges;

	TArray<TTuple<int32, int32>> MstEdges;

	TArray<TTuple<int32, int32>> HallwayEdges;

	TSet<FIntVector> HallwayTiles;

	TSet<FIntVector> RoomTiles;
};