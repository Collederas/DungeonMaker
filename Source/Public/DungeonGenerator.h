#pragma once

#include "CoreMinimal.h"
#include "Grid3D.h"
#include "Pathfinder3D.h"
#include "GameFramework/Actor.h"
#include "Math/IntVector.h"
#include "Tetrahedralizer.h"
#include "DungeonGenerator.generated.h"

// TODO: setting floorheight to gridunitsize makes flat dungeons (having it double or more makes the pathfinding cubes be too high (2/1 height/base)


class AStaticMeshActor;

UENUM()
enum ECellType
{
	None,
	Room,
	Hallway,
	Stairs
};

USTRUCT(BlueprintType)
struct FDungeonRoomBox
{
	GENERATED_BODY()

	UPROPERTY()
	FIntVector Min;

	UPROPERTY()
	FIntVector Max;

	FDungeonRoomBox()
		: Min(INT32_MAX), Max(INT32_MIN) {}

	FDungeonRoomBox(const FIntVector& InMin, const FIntVector& InMax)
		: Min(InMin), Max(InMax) {}

	/** Checks if this box intersects with another. */
	bool Intersect(const FDungeonRoomBox& Other) const
	{
		if (Min.X >= Other.Max.X || Other.Min.X >= Max.X) return false;
		if (Min.Y >= Other.Max.Y || Other.Min.Y >= Max.Y) return false;
		if (Min.Z >= Other.Max.Z || Other.Min.Z >= Max.Z) return false;
		return true;
	}
};

USTRUCT(BlueprintType)
struct FDungeonRoom
{
	GENERATED_BODY()

	UPROPERTY()
	FDungeonRoomBox Bounds;

	UPROPERTY()
	FIntVector GridCenter;

	UPROPERTY()
	FVector WorldCenter;
};

UENUM()
enum class EWallDirection : uint8
{
	Horizontal,
	Vertical
};

// Struct to uniquely identify a wall's position and orientation on the grid
USTRUCT()
struct FWallPosition
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Position;

	UPROPERTY()
	EWallDirection Direction;

	bool operator==(const FWallPosition& Other) const
	{
		return Position == Other.Position && Direction == Other.Direction;
	}
};

FORCEINLINE uint32 GetTypeHash(const FWallPosition& WallPos)
{
	return HashCombine(GetTypeHash(WallPos.Position), GetTypeHash((uint8)WallPos.Direction));
}

FORCEINLINE uint32 GetTypeHash(const TTuple<int32, int32>& Tuple)
{
	return HashCombine(GetTypeHash(FMath::Min(Tuple.Get<0>(), Tuple.Get<1>())), GetTypeHash(FMath::Max(Tuple.Get<0>(), Tuple.Get<1>())));
}

UCLASS()
class DUNGEONMAKER_API ADungeonGenerator : public AActor
{
	GENERATED_BODY()

public:
	ADungeonGenerator();

public:
	UFUNCTION(CallInEditor, Category = "Dungeon Generation")
	void GenerateDungeon();
	
	UFUNCTION(CallInEditor, Category = "Dungeon Generation|Debug")
	void DrawDelaunayTetrahedralization();
	
	UFUNCTION(CallInEditor, Category = "Dungeon Generation|Debug")
	void DrawMstGraph();
	
	UFUNCTION(CallInEditor, Category = "Dungeon Generation|Debug")
	void DrawHallways();

	UFUNCTION(CallInEditor, Category = "Dungeon Generation")
	void PathfindHallways();

	UFUNCTION(CallInEditor, Category = "Dungeon Generation|Debug")
	void DebugDrawGrid();
	
	UFUNCTION(CallInEditor, Category = "Dungeon Generation")
	void ClearDungeon();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding")
	bool bVisualizePathfinding = true; 

	FVector GridToWorld(const FIntVector& GridPos) const
	{
		const FVector WorldPos(
			GridPos.X * GridUnitSize,
			GridPos.Y * GridUnitSize,
			GridPos.Z * FloorHeight
		);
		return WorldPos + GetActorLocation();
	}

protected:
	Pathfinder3D Pathfinder;

	UPROPERTY(VisibleAnywhere)
	UInstancedStaticMeshComponent* RoomISM;

	UPROPERTY(VisibleAnywhere)
	UInstancedStaticMeshComponent* HallwayISM;

	UPROPERTY(VisibleAnywhere)
	UInstancedStaticMeshComponent* StairsISM;
	
	
protected:
	// Iterate the grid 
	void PlaceRooms();
private:
	FPathCost CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom);
	
	void CalculateDelaunayTetrahedralization(); 
	void CalculateMst();
	void CreateHallways();

	void ProcessPath(TArray<FIntVector> Path);
	void PathfindHallwayEdge(int32 Edge);

	void StartHallwayPathfinding_Async();
	void PathfindNextHallway_Async();

	void SpawnHallwayWalls();
	void SpawnRoomActors(UWorld* World, const FDungeonRoomBox& RoomBox, int32 RoomIndex);

	//~ UProperties
	//=================================================================================================
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Grid", meta = (ClampMin = "10"))
	FIntPoint GridSize = FIntPoint(20, 20);
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Grid", meta = (ClampMin = "1"))
	float GridUnitSize = 400.0f;
	// New 3D Properties
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Grid", meta = (ClampMin = "1"))
	int32 NumberOfFloors = 3;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Grid", meta = (ClampMin = "100.0"))
	float FloorHeight = 800.0f;
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Rooms", meta = (ClampMin = "1"))
	int32 NumberOfRooms = 25;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Rooms")
	FIntPoint RoomSizeMinMax = FIntPoint(4, 10);
	// New 3D Property
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Rooms", meta = (ClampMin = "1"))
	FIntPoint RoomHeightInFloorsMinMax = FIntPoint(1, 2);
	
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExtraConnectionChance = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Visuals")
	TObjectPtr<UStaticMesh> RoomFloorMesh;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Visuals")
	TObjectPtr<UStaticMesh> WallMesh;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Visuals")
	TObjectPtr<UStaticMesh> CornerMesh;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Visuals")
	TObjectPtr<UStaticMesh> HallwayFloorMesh;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Debug")
	FColor DelaunayDebugColor = FColor::Magenta;
	UPROPERTY(EditAnywhere, Category = "Dungeon Settings|Debug")
	FColor MstDebugColor = FColor::Green;
	UPROPERTY(Editanywhere, Category = "Dungeon Settings|Debug")
	FColor HallwayDebugColor = FColor::White;

private:	
	FGrid3D<ECellType> Grid;
	
	//~ Graph Data
	//=================================================================================================

	TArray<TObjectPtr<AStaticMeshActor>> SpawnedActors;

	UPROPERTY()
	TArray<FDungeonRoom> DungeonRooms;

	TArray<TTuple<int32, int32>> DelaunayEdges;

	TArray<TTuple<int32, int32>> MstEdges;

	TArray<TTuple<int32, int32>> HallwayEdges;


	TSet<FWallPosition> DoorwayPositions;

	// Changed to FIntVector for 3D coordinates
	TSet<FIntVector> RemovedCornerPositions;

	// Changed to FIntVector for 3D coordinates
	TSet<FIntVector> HallwayTiles;

	// Changed to FIntVector for 3D coordinates
	TSet<FIntVector> RoomTiles;

	// DEBUG TOOLS
	
	// Tracks which hallway we are currently processing - used only for debugging
	int32 CurrentHallwayEdgeIndex;

	// For debug, you can run generation on a timer so you can see each step of the algo at work
	FTimerHandle HallwayPathfindTimer;
	bool bIsPathfinderRunning;
};
