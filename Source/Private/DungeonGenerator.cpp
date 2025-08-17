#include "DungeonGenerator.h"
#include "Engine/StaticMeshActor.h"
#include "DrawDebugHelpers.h"
#include "DungeonMaker.h"
#include "MeshAttributes.h"
#include "Pathfinder3D.h"
#include "Components/InstancedStaticMeshComponent.h"

ADungeonGenerator::ADungeonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	RoomISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RoomISM"));
	HallwayISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HallwayISM"));
	StairsISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StairsISM"));

	// Attach them to the root component
	RoomISM->SetupAttachment(RootComponent);
	HallwayISM->SetupAttachment(RootComponent);
	StairsISM->SetupAttachment(RootComponent);

	DebugCurrentPathISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DebugCurrentPathISM"));
	DebugCurrentPathISM->SetupAttachment(RootComponent);
}

void ADungeonGenerator::ClearDungeon()
{

	DelaunayEdges.Empty();
	MstEdges.Empty();
	HallwayEdges.Empty();

	Grid.Clear();
	RoomTiles.Empty();
	HallwayTiles.Empty();
	DungeonRooms.Empty();
	
	RoomISM->ClearInstances();
	HallwayISM->ClearInstances();
	StairsISM->ClearInstances();
	
	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
}

void ADungeonGenerator::GenerateDungeon()
{
	ClearDungeon();
	Grid = FGrid3D<ECellType>(FIntVector(GridSize.X, GridSize.Y, GridSize.Z),
	                          [](const FIntVector& Pos) { return ECellType::None; }, FIntVector::ZeroValue);
	PlaceRooms();
	CalculateDelaunayTetrahedralization();
	CalculateMst();
	CreateHallways();

	UE_LOG(LogDungeonMaker, Verbose, TEXT("Created dungeon with: %d rooms on %d floors"), DungeonRooms.Num(), GridSize.Z);
};

void ADungeonGenerator::PlaceRooms()
{
	DungeonRooms.Empty();
	RoomTiles.Empty();

	// Validate min/max values
	if (RoomSizeMinMax.X > RoomSizeMinMax.Y) Swap(RoomSizeMinMax.X, RoomSizeMinMax.Y);
	if (RoomHeightInFloorsMinMax.X > RoomHeightInFloorsMinMax.Y) Swap(RoomHeightInFloorsMinMax.X, RoomHeightInFloorsMinMax.Y);

	for (int32 i = 0; i < NumberOfRooms; ++i)
	{
		// Generate random properties for a potential room
		const int32 RoomWidth = FMath::RandRange(RoomSizeMinMax.X, RoomSizeMinMax.Y);
		const int32 RoomDepth = FMath::RandRange(RoomSizeMinMax.X, RoomSizeMinMax.Y);
		const int32 RoomX = FMath::RandRange(0, GridSize.X - RoomWidth);
		const int32 RoomY = FMath::RandRange(0, GridSize.Y - RoomDepth);

		const int32 StartFloor = FMath::RandRange(0, GridSize.Z - 1);
		int32 NumFloors = FMath::RandRange(RoomHeightInFloorsMinMax.X, RoomHeightInFloorsMinMax.Y);
		if (StartFloor + NumFloors > GridSize.Z) NumFloors = GridSize.Z - StartFloor;

		FDungeonRoomBox NewRoomBox(
			FIntVector(RoomX, RoomY, StartFloor),
			FIntVector(RoomX + RoomWidth, RoomY + RoomDepth, StartFloor + NumFloors)
		);

		// Check for overlaps with existing rooms
		bool bOverlaps = false;
		for (const FDungeonRoom& Room : DungeonRooms)
		{
			FDungeonRoomBox PaddedBox = Room.Bounds;
			PaddedBox.Min -= FIntVector(1, 1, 0);
			PaddedBox.Max += FIntVector(1, 1, 0);
			if (PaddedBox.Intersect(NewRoomBox))
			{
				bOverlaps = true;
				break;
			}
		}

		if (!bOverlaps)
		{
			// Create and configure the new room struct
			FDungeonRoom NewRoom;
			NewRoom.Bounds = NewRoomBox;

			const FVector BoxCenterGrid(
				(NewRoomBox.Min.X + NewRoomBox.Max.X - 1) / 2.0f,
				(NewRoomBox.Min.Y + NewRoomBox.Max.Y - 1) / 2.0f,
				(NewRoomBox.Min.Z + NewRoomBox.Max.Z - 1) / 2.0f // Correctly uses inclusive max boundary
			);

			NewRoom.GridCenter = FIntVector(FMath::RoundToInt(BoxCenterGrid.X), FMath::RoundToInt(BoxCenterGrid.Y), FMath::RoundToInt(BoxCenterGrid.Z));
			NewRoom.WorldCenter = GridToWorld(NewRoom.GridCenter);

			DungeonRooms.Add(NewRoom);

			for (int32 z = NewRoom.Bounds.Min.Z; z < NewRoom.Bounds.Max.Z; ++z)
			{
				for (int32 y = NewRoom.Bounds.Min.Y; y < NewRoom.Bounds.Max.Y; ++y)
				{
					for (int32 x = NewRoom.Bounds.Min.X; x < NewRoom.Bounds.Max.X; ++x)
					{
						Grid(x, y, z) = ECellType::Room;
						RoomTiles.Add(FIntVector(x, y, z));
					}
				}
			}
		}
	}
	UE_LOG(LogDungeonMaker, Log, TEXT("Generated %d rooms."), DungeonRooms.Num());
}

FPathCost ADungeonGenerator::CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom)
{
	FPathCost PathCost;
	const FIntVector Delta = Neighbor->Position - Current->Position;

	if (Delta.Z == 0) // Flat hallway
	{
		PathCost.Cost = FVector::Dist(FVector(Neighbor->Position), FVector(GoalRoom.GridCenter));
		const ECellType NeighborNodeType = Grid(Neighbor->Position);

		if (NeighborNodeType == ECellType::Stairs)
		{
			return PathCost;
		}

		switch (NeighborNodeType)
		{
		case ECellType::None:
		case ECellType::Hallway: // Explicitly handle hallways
			PathCost.Cost += 1; // Standard cost
			PathCost.bTraversable = true;
			break;
		case ECellType::Room:
			PathCost.Cost += 5; // Higher cost to discourage pathing through rooms
			PathCost.bTraversable = true;
			break;
		default:
			// Any other type (like Stairs, or other future types) is not traversable this way.
			PathCost.bTraversable = false;
			break;
		}
		PathCost.bTraversable = true;
	}
	else
	{
		// if Delta is vertical, then we have a staircase
		const ECellType CurrentNodeType = Grid(Current->Position);
		const ECellType NeighborNodeType = Grid(Neighbor->Position);
		if ((CurrentNodeType != ECellType::None && CurrentNodeType != ECellType::Hallway) ||
			(NeighborNodeType != ECellType::None && NeighborNodeType != ECellType::Hallway))
		{
			return PathCost; // Not traversable
		}

		PathCost.Cost = 100 + FVector::Dist(FVector(Neighbor->Position), FVector(GoalRoom.GridCenter)); //base cost + heuristic

		// Check if there's enough space for a 2x2 stairwell landing
		const int32 XDir = FMath::Clamp(Delta.X, -1, 1);
		const int32 YDir = FMath::Clamp(Delta.Y, -1, 1);
		const FIntVector VerticalOffset(0, 0, Delta.Z);
		const FIntVector HorizontalOffset(XDir, YDir, 0);

		if (!Grid.InBounds(Current->Position + VerticalOffset) ||
			!Grid.InBounds(Current->Position + HorizontalOffset) ||
			!Grid.InBounds(Current->Position + VerticalOffset + HorizontalOffset))
		{
			return PathCost;
		}

		// Ensure the landing area is clear
		if (Grid(Current->Position + HorizontalOffset) != ECellType::None ||
			Grid(Current->Position + HorizontalOffset * 2) != ECellType::None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset) != ECellType::None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset * 2) != ECellType::None)
		{
			return PathCost;
		}
		PathCost.bTraversable = true;
		PathCost.bIsStairs = true;
	}
	return PathCost;
}

void ADungeonGenerator::CalculateDelaunayTetrahedralization()
{
	DelaunayEdges.Empty();
	if (DungeonRooms.Num() < 4) return;

	// 1. Convert room centers to the library's point format.
	std::vector<geom::Point3D> Points;
	Points.reserve(DungeonRooms.Num());
	for (const FDungeonRoom& Room : DungeonRooms)
	{
		const FVector Center = Room.WorldCenter;
		Points.push_back({Center.X, Center.Y, Center.Z});
	}

	// 2. Perform the 3D triangulation.
	const std::vector<geom::Tetrahedron> Tetrahedra = geom::Tetrahedralizer::Triangulate(Points);

	// 3. Extract unique edges from the tetrahedra.
	// Using a TSet ensures each edge is added only once.
	TSet<TTuple<int32, int32>> UniqueEdges;
	for (const geom::Tetrahedron& Tetra : Tetrahedra)
	{
		const int32 P1 = Tetra.p1;
		const int32 P2 = Tetra.p2;
		const int32 P3 = Tetra.p3;
		const int32 P4 = Tetra.p4;

		// A tetrahedron has 6 edges
		UniqueEdges.Add(TTuple<int32, int32>(P1, P2));
		UniqueEdges.Add(TTuple<int32, int32>(P1, P3));
		UniqueEdges.Add(TTuple<int32, int32>(P1, P4));
		UniqueEdges.Add(TTuple<int32, int32>(P2, P3));
		UniqueEdges.Add(TTuple<int32, int32>(P2, P4));
		UniqueEdges.Add(TTuple<int32, int32>(P3, P4));
	}

	DelaunayEdges = UniqueEdges.Array();
}

void ADungeonGenerator::CalculateMst()
{
	MstEdges.Empty();
	const int32 NumRooms = DungeonRooms.Num();
	if (NumRooms < 2) return;

	TMap<int32, TArray<TTuple<int32, float>>> AdjacencyList;
	for (const TTuple<int32, int32>& Edge : DelaunayEdges)
	{
		const int32 P1Index = Edge.Get<0>();
		const int32 P2Index = Edge.Get<1>();
		const FDungeonRoom& Room1 = DungeonRooms[P1Index];
		const FDungeonRoom& Room2 = DungeonRooms[P2Index];

		const float DistanceSq = FVector::DistSquared(Room1.WorldCenter, Room2.WorldCenter);

		AdjacencyList.FindOrAdd(P1Index).Emplace(P2Index, DistanceSq);
		AdjacencyList.FindOrAdd(P2Index).Emplace(P1Index, DistanceSq);
	}

	TSet<int32> VisitedNodes;
	TArray<float> MinCost;
	TArray<int32> ParentNode;

	MinCost.Init(FLT_MAX, NumRooms);
	ParentNode.Init(-1, NumRooms);
	MinCost[0] = 0.f;

	for (int32 i = 0; i < NumRooms; ++i)
	{
		int32 U = -1;
		float MinEdgeCost = FLT_MAX;
		for (int32 j = 0; j < NumRooms; ++j)
		{
			if (!VisitedNodes.Contains(j) && MinCost[j] < MinEdgeCost)
			{
				MinEdgeCost = MinCost[j];
				U = j;
			}
		}

		if (U == -1) break;
		VisitedNodes.Add(U);

		if (AdjacencyList.Contains(U))
		{
			for (const TTuple<int32, float>& Neighbor : AdjacencyList[U])
			{
				const int32 V = Neighbor.Get<0>();
				const float Weight = Neighbor.Get<1>();

				if (!VisitedNodes.Contains(V) && Weight < MinCost[V])
				{
					ParentNode[V] = U;
					MinCost[V] = Weight;
				}
			}
		}
	}

	for (int32 i = 1; i < NumRooms; ++i)
	{
		if (ParentNode[i] != -1)
		{
			MstEdges.Emplace(ParentNode[i], i);
		}
	}
}

void ADungeonGenerator::CreateHallways()
{
	HallwayEdges.Empty();
	if (DungeonRooms.Num() < 2) return;

	// 1. Add all edges from the MST to guarantee connectivity.
	HallwayEdges = MstEdges;

	// 2. Create a set of the MST edges for fast lookup.
	TSet<TTuple<int32, int32>> MstEdgeSet;
	for (const auto& Edge : MstEdges)
	{
		MstEdgeSet.Add(TTuple<int32, int32>(FMath::Min(Edge.Get<0>(), Edge.Get<1>()), FMath::Max(Edge.Get<0>(), Edge.Get<1>())));
	}

	// 3. Randomly add some extra edges from the Delaunay graph to create loops.
	for (const auto& Edge : DelaunayEdges)
	{
		const TTuple<int32, int32> CanonicalEdge(FMath::Min(Edge.Get<0>(), Edge.Get<1>()), FMath::Max(Edge.Get<0>(), Edge.Get<1>()));
		if (!MstEdgeSet.Contains(CanonicalEdge))
		{
			if (FMath::FRand() < ExtraConnectionChance)
			{
				HallwayEdges.Add(Edge);
			}
		}
	}
}

void ADungeonGenerator::ProcessPathTile(const FIntVector& CurrentPathPoint, const FIntVector& PreviousPathPoint, bool bIsFirstTile)
{
    const FVector ActorLocation = GetActorLocation();
    const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, GridUnitSize / 100.f);

    // Process the current tile as a hallway
    if (Grid.InBounds(CurrentPathPoint) && Grid(CurrentPathPoint) == ECellType::None)
    {
        Grid(CurrentPathPoint) = ECellType::Hallway;
        HallwayTiles.Add(CurrentPathPoint);

        const FVector CellLocation = FVector(CurrentPathPoint.X * GridUnitSize, CurrentPathPoint.Y * GridUnitSize, CurrentPathPoint.Z * GridUnitSize) + ActorLocation;
        const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
        HallwayISM->AddInstance(InstanceTransform);
    }

    // Process stairs if there's a vertical change from the previous tile
    if (!bIsFirstTile)
    {
        FIntVector Delta = CurrentPathPoint - PreviousPathPoint;
        if (Delta.Z != 0)
        {
            // This stair logic is the same as what was in ProcessPath before
            const int32 XDir = FMath::Clamp(Delta.X, -1, 1);
            const int32 YDir = FMath::Clamp(Delta.Y, -1, 1);
            const FIntVector VerticalOffset(0, 0, Delta.Z);
            const FIntVector HorizontalOffset(XDir, YDir, 0);

            FIntVector StairTiles[] = {
               PreviousPathPoint + HorizontalOffset,
               PreviousPathPoint + HorizontalOffset * 2,
               PreviousPathPoint + VerticalOffset + HorizontalOffset,
               PreviousPathPoint + VerticalOffset + HorizontalOffset * 2
            };

            for (const FIntVector& StairTile : StairTiles)
            {
               if (Grid.InBounds(StairTile))
               {
                  Grid(StairTile) = ECellType::Stairs;
                  HallwayTiles.Add(StairTile);
                  const FVector CellLocation = FVector(StairTile.X * GridUnitSize, StairTile.Y * GridUnitSize, StairTile.Z * GridUnitSize) + ActorLocation;
                  const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
                  StairsISM->AddInstance(InstanceTransform);
               }
            }
        }
    }
}

void ADungeonGenerator::ProcessPath(TArray<FIntVector> Path)
{
	if (!Path.IsEmpty())
	{
		const FVector ActorLocation = GetActorLocation();
		const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, GridUnitSize / 100.f);
		
		for (int32 i = 0; i < Path.Num(); i++)
		{
			FIntVector PreviousPoint = (i>0) ? Path[i-1] : Path[i];
			ProcessPathTile(Path[i], PreviousPoint, i==0);
		}
	}
}

void ADungeonGenerator::PathfindHallways()
{
	if (DungeonRooms.Num() < 2) return;
	if (HallwayEdges.IsEmpty()) return;
	
	FinalPathsToDraw_Async.Empty();
	HallwayISM->ClearInstances();
	StairsISM->ClearInstances();
	
	FIntVector GridSize3D = FIntVector(GridSize.X, GridSize.Y, GridSize.Z);
	Pathfinder.Initialize(GridSize3D, this, [&](const FIntVector& Pos) -> FVector
	{
		return this->GridToWorld(Pos);
	});
	
	DebugDrawGrid();
	
	switch (PathfindDebugMode)
	{
		case EPathfindDebugMode::StepByStep:
			{
				// Use the slow, async method that visualizes the A* algorithm at work
				StartHallwayPathfinding_Async();
				break;
			}

		case EPathfindDebugMode::AnimateFinalPath:
			{
				for (int32 i = 0; i < HallwayEdges.Num(); ++i)
				{
					TArray<FIntVector> Path = PathfindHallwayEdge(i);
					if (!Path.IsEmpty())
					{
						FinalPathsToDraw_Async.Enqueue(Path);
					}
				}
				UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways instantly."));

				StartDrawingFinalPaths_Async();
				break;
			}

		case EPathfindDebugMode::None:
			default:
			{
				// Calculate and draw everything instantly with no visualization
				for (int32 i = 0; i < HallwayEdges.Num(); ++i)
				{
					TArray<FIntVector> Path = PathfindHallwayEdge(i);
					ProcessPath(Path); // Process and draw immediately
				}
				UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways instantly."));
				break;
			}
	}
};

TArray<FIntVector> ADungeonGenerator::PathfindHallwayEdge(int32 EdgeIndex)
{
	if (!HallwayEdges.IsValidIndex(EdgeIndex)) return TArray<FIntVector>();

	const TTuple<int32, int32>& Edge = HallwayEdges[EdgeIndex];
	const FDungeonRoom& RoomEdge1 = DungeonRooms[Edge.Get<0>()];
	const FDungeonRoom& RoomEdge2 = DungeonRooms[Edge.Get<1>()];

	TArray<FIntVector> Path = Pathfinder.FindPath(RoomEdge1.GridCenter, RoomEdge2.GridCenter,
	                                              [&](const FPathNode* Current, const FPathNode* Neighbor) -> FPathCost
	                                              {
	                                              	return this->CalculateHallwayPathCost(Current, Neighbor, RoomEdge2);
	                                              });

	return Path;
}

void ADungeonGenerator::StartHallwayPathfinding_Async()
{
	HallwayISM->ClearInstances();
	StairsISM->ClearInstances();
	
	CurrentHallwayEdgeIndex = 0;
	bIsPathfinderRunning = false;

	PathfindNextHallway_Async();
}

void ADungeonGenerator::PathfindNextHallway_Async()
{
	if (!HallwayEdges.IsValidIndex(CurrentHallwayEdgeIndex))
	{
		UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways for visualization."));
		GetWorld()->GetTimerManager().ClearTimer(HallwayPathfindTimer);
		return;
	}
	
	if (!bIsPathfinderRunning)
	{
		const TTuple<int32, int32>& Edge = HallwayEdges[CurrentHallwayEdgeIndex];
		const FDungeonRoom& RoomEdge1 = DungeonRooms[Edge.Get<0>()];
		const FDungeonRoom& RoomEdge2 = DungeonRooms[Edge.Get<1>()];
		Pathfinder.StartPath(RoomEdge1.GridCenter,
			RoomEdge2.GridCenter,
			[&](const FPathNode* Current, const FPathNode* Neighbor) -> FPathCost
			{
				return this->CalculateHallwayPathCost(Current, Neighbor, RoomEdge2);
			});

		UE_LOG(LogDungeonMaker, Log, TEXT("Start pathfinding hallway nr %d/%d. Start %s, Goal %s"), CurrentHallwayEdgeIndex, HallwayEdges.Num(), *RoomEdge1.GridCenter.ToString(), *RoomEdge2.GridCenter.ToString());

		bIsPathfinderRunning = true;
	}

	const EPathStatus Status = Pathfinder.Step();

	if (Status == EPathStatus::InProgress)
	{
		// Path not found yet, set a timer to run the next step
		GetWorld()->GetTimerManager().SetTimer(HallwayPathfindTimer, this, &ADungeonGenerator::PathfindNextHallway_Async, 0.01f, false);
	}
	else
	{
		// Path Succeeded or Failed, we are done with this hallway
		if (Status == EPathStatus::Succeeded)
		{
			TArray<FIntVector> Path = Pathfinder.GetFinalPath();
			ProcessPath(Path);
		}

		bIsPathfinderRunning = false;
		CurrentHallwayEdgeIndex++; // Move to the next hallway
		GetWorld()->GetTimerManager().SetTimer(
			HallwayPathfindTimer,
			this,
			&ADungeonGenerator::PathfindNextHallway_Async,
			0.01f,
			false
		);
	}
}

void ADungeonGenerator::UpdateCurrentPathVisual()
{
	DebugCurrentPathISM->ClearInstances();

	const FPathNode* CurrentNode = Pathfinder.GetCurrentNode();

	while (CurrentNode != nullptr)
	{
		AddDebugInstance(DebugCurrentPathISM, CurrentNode->Position);
		CurrentNode = CurrentNode->Previous;
	}
}

void ADungeonGenerator::AddDebugInstance(UInstancedStaticMeshComponent* ISM, const FIntVector& GridPos)
{
	if (!ISM) return;
	const FVector ActorLocation = GetActorLocation();
	const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, GridUnitSize / 100.f);
	const FVector CellLocation = FVector(GridPos.X * GridUnitSize, GridPos.Y * GridUnitSize, GridPos.Z * GridUnitSize) + ActorLocation;
	ISM->AddInstance(FTransform(FRotator::ZeroRotator, CellLocation, CellScale));
}

void ADungeonGenerator::StartDrawingFinalPaths_Async()
{
	CurrentPathDrawIndex = 0;
	if (!FinalPathsToDraw_Async.IsEmpty())
	{
		// Kick off the timer to draw the first segment
		GetWorld()->GetTimerManager().SetTimer(
			FinalPathDrawTimer,
			this,
			&ADungeonGenerator::DrawNextPathSegment_Async,
			0.05f, // You can adjust this speed
			false
		);
	}
}

void ADungeonGenerator::DrawNextPathSegment_Async()
{
	TArray<FIntVector> CurrentPath;
	if (!FinalPathsToDraw_Async.Peek(CurrentPath))
	{
		// No paths left in the queue, we're done.
		UE_LOG(LogDungeonMaker, Log, TEXT("Finished drawing all paths."));
		GetWorld()->GetTimerManager().ClearTimer(FinalPathDrawTimer);
		return;
	}

	if (CurrentPath.IsValidIndex(CurrentPathDrawIndex))
	{
		// Get the current and previous points to process
		const FIntVector CurrentPoint = (CurrentPath)[CurrentPathDrawIndex];
		const FIntVector PreviousPoint = (CurrentPathDrawIndex > 0) ? (CurrentPath)[CurrentPathDrawIndex - 1] : CurrentPoint;
        
		// Draw the tile
		ProcessPathTile(CurrentPoint, PreviousPoint, CurrentPathDrawIndex == 0);

		// Move to the next index and set the timer again
		CurrentPathDrawIndex++;
		GetWorld()->GetTimerManager().SetTimer(FinalPathDrawTimer, this, &ADungeonGenerator::DrawNextPathSegment_Async, 0.05f, false);
	}
	else
	{
		// We've finished drawing the current path, so dequeue it and start the next one.
		FinalPathsToDraw_Async.Dequeue(CurrentPath);
		CurrentPathDrawIndex = 0;

		// Immediately try to draw the next path (or finish if the queue is empty)
		DrawNextPathSegment_Async();
	}
}

void ADungeonGenerator::DrawDelaunayTetrahedralization()
{
	UWorld* World = GetWorld();
	if (!World || DelaunayEdges.IsEmpty()) return;

	FlushPersistentDebugLines(World);

	for (const TTuple<int32, int32>& Edge : DelaunayEdges)
	{
		DrawDebugLine(World, DungeonRooms[Edge.Get<0>()].WorldCenter, DungeonRooms[Edge.Get<1>()].WorldCenter,
		              DelaunayDebugColor, true, -1.f, 0, 15.0f);
	}
}

void ADungeonGenerator::DrawHallways()
{
	UWorld* World = GetWorld();
	if (!World || HallwayEdges.IsEmpty()) return;
	FlushPersistentDebugLines(World);
	for (const auto& Edge : HallwayEdges)
	{
		DrawDebugLine(World, DungeonRooms[Edge.Get<0>()].WorldCenter, DungeonRooms[Edge.Get<1>()].WorldCenter, HallwayDebugColor, true, -1.f, 0, 35.0f);
	}
}

void ADungeonGenerator::DrawMstGraph()
{
	UWorld* World = GetWorld();
	if (!World || MstEdges.IsEmpty()) return;

	FlushPersistentDebugLines(World);

	for (const TTuple<int32, int32>& Edge : MstEdges)
	{
		DrawDebugLine(World, DungeonRooms[Edge.Get<0>()].WorldCenter, DungeonRooms[Edge.Get<1>()].WorldCenter,
		              MstDebugColor, true, -1.f, 0, 25.0f);
	}
}

void ADungeonGenerator::DebugDrawGrid()
{
	if (!Grid.IsInitialized()) return;

	// Clear all previously drawn instances
	RoomISM->ClearInstances();
	HallwayISM->ClearInstances();
	StairsISM->ClearInstances();

	const FVector ActorLocation = GetActorLocation();
	const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, GridUnitSize / 100.f);

	for (int32 z = 0; z < GridSize.Z; ++z)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			for (int32 x = 0; x < GridSize.X; ++x)
			{
				const ECellType CellType = Grid(x, y, z);
				UInstancedStaticMeshComponent* TargetISM = nullptr;

				switch (CellType)
				{
				case ECellType::Room:    TargetISM = RoomISM;    break;
				case ECellType::Hallway: TargetISM = HallwayISM; break;
				case ECellType::Stairs:  TargetISM = StairsISM;  break;
				}

				if (TargetISM)
				{
					const FVector CellLocation = FVector(x * GridUnitSize, y * GridUnitSize, z * GridUnitSize) + ActorLocation;
					const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
					TargetISM->AddInstance(InstanceTransform);
				}
			}
		}
	}
}