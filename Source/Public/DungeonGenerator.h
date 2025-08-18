// #pragma once
//
// #include "CoreMinimal.h"
// #include "Grid3D.h"
// #include "Pathfinder3D.h"
// #include "GameFramework/Actor.h"
// #include "Math/IntVector.h"
// #include "Tetrahedralizer.h"
// #include "DungeonGenerator.generated.h"
//
// // TODO: discard corridors between rooms too far (? how to ensure connectivity though? maybe just try to place rooms closer to each-other)
//
//
// UENUM(BlueprintType)
// enum class EPathfindDebugMode : uint8
// {
// 	None UMETA(DisplayName="None"),
// 	StepByStep UMETA(DisplayName="Step-by-Step (A*)"),
// 	AnimateFinalPath UMETA(DisplayName="Animate Final Path (Cinematic)")
// };
//
//
// UCLASS(PrioritizeCategories = ("Dungeon Generation"))
// class DUNGEONMAKER_API ADungeonGenerator : public AActor
// {
// 	GENERATED_BODY()
//
// public:
// 	ADungeonGenerator();
//
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation")
// 	void GenerateDungeon();
//
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation")
// 	void ClearDungeon();
//
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation | Debug")
// 	void StopPathfindingAsync();
//
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation | Debug")
// 	void DrawDelaunayTetrahedralization();
// 	
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation | Debug")
// 	void DrawMstGraph();
// 	
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation | Debug")
// 	void DrawHallways();
//
// 	UFUNCTION(CallInEditor, Category = "Dungeon Generation | Debug")
// 	void DebugDrawGrid();
//
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Generation | Debug")
// 	EPathfindDebugMode PathfindDebugMode;
//
// 	FVector GridToWorld(const FIntVector& GridPos) const
// 	{
// 		const FVector WorldPos(
// 			GridPos.X * GridUnitSize,
// 			GridPos.Y * GridUnitSize,
// 			GridPos.Z * GridUnitSize
// 		);
// 		return WorldPos + GetActorLocation();
// 	}
// 	
// public:
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "10"))
// 	FIntVector GridSize = FIntVector(20, 20, 3);
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "1"))
// 	float GridUnitSize = 400.0f;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
// 	int32 NumberOfRooms = 5;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms")
// 	FIntPoint RoomSizeMinMax = FIntPoint(4, 10);
//
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
// 	FIntPoint RoomHeightInFloorsMinMax = FIntPoint(1, 2);
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
// 	float ExtraConnectionChance = 0.2f;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Debug")
// 	FColor DelaunayDebugColor = FColor::Magenta;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Dungeon Settings | Debug")
// 	FColor MstDebugColor = FColor::Green;
// 	
// 	UPROPERTY(Editanywhere, Category = "Dungeon Settings | Debug")
// 	FColor HallwayDebugColor = FColor::White;
//
// protected:
// 	Pathfinder3D Pathfinder;
//
// 	UPROPERTY()
// 	UInstancedStaticMeshComponent* RoomISM;
//
// 	UPROPERTY()
// 	UInstancedStaticMeshComponent* HallwayISM;
//
// 	UPROPERTY()
// 	UInstancedStaticMeshComponent* StairsISM;
// 	
// 	UPROPERTY()
// 	TObjectPtr<UInstancedStaticMeshComponent> DebugCurrentPathISM;
// 	
// private:
// 	FPathCost CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom);
//
// 	void PlaceRooms();
// 	
// 	void CalculateDelaunayTetrahedralization(); 
// 	void CalculateMst();
// 	void CreateHallways();
// 	void PathfindHallways();
// 	
// 	// This instances the static meshes that represent each tile (hallway, stairs).
// 	void ProcessPathTile(const FIntVector& CurrentPathPoint, const FIntVector& PreviousPathPoint, bool bIsFirstTile);
//
// 	void ProcessPath(TArray<FIntVector> Path);
// 	TArray<FIntVector> PathfindHallwayEdge(int32 Edge);
//
// 	void StartHallwayPathfinding_Async();
// 	void PathfindNextHallway_Async();
//
// 	void UpdateCurrentPathVisual();
// 	void AddDebugInstance(UInstancedStaticMeshComponent* ISM, const FIntVector& GridPos);
// 	
// 	void StartDrawingFinalPaths_Async();
// 	void DrawNextPathSegment_Async();
//
// private:
// 	UPROPERTY()
// 	TArray<FDungeonRoom> DungeonRooms;
// 	
// 	FGrid3D<ECellType> Grid;
//
// 	TArray<TTuple<int32, int32>> DelaunayEdges;
//
// 	TArray<TTuple<int32, int32>> MstEdges;
//
// 	TArray<TTuple<int32, int32>> HallwayEdges;
//
// 	TSet<FIntVector> HallwayTiles;
//
// 	TSet<FIntVector> RoomTiles;
//
// 	
// 	// DEBUG TOOLS
// 	
// 	// Tracks which hallway we are currently processing - used only for debugging
// 	int32 CurrentHallwayEdgeIndex;
//
// 	// For debug, you can run generation on a timer so you can see each step of the algo at work
// 	FTimerHandle HallwayPathfindTimer;
// 	bool bIsPathfinderRunning;
//
// 	TQueue<TArray<FIntVector>> FinalPathsToDraw_Async;
//
// 	int32 CurrentPathDrawIndex;
//
// 	FTimerHandle FinalPathDrawTimer;
// 	bool bIsAsyncProcessActive = false;
// };
