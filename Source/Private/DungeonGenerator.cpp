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
}

void ADungeonGenerator::ClearDungeon()
{
	for (AActor* Actor : SpawnedActors)
	{
		if (Actor) Actor->Destroy();
	}
	SpawnedActors.Empty();
	DelaunayEdges.Empty();
	MstEdges.Empty();
	HallwayEdges.Empty();

	Grid.Clear();
	RoomTiles.Empty();
	HallwayTiles.Empty();
	DungeonRooms.Empty();

	RemovedCornerPositions.Empty();

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
	if (!RoomFloorMesh || !WallMesh || !HallwayFloorMesh)
	{
		return;
	}

	Grid = FGrid3D<ECellType>(FIntVector(GridSize.X, GridSize.Y, NumberOfFloors),
	                          [](const FIntVector& Pos) { return ECellType::None; }, FIntVector::ZeroValue);

	// 1. Place Rooms in 3D
	PlaceRooms();

	// 2. Calculate connectivity graph using 3D tetrahedralization
	CalculateDelaunayTetrahedralization();
	CalculateMst();
	CreateHallways();


	// 3. Spawn all the actors
	// UWorld* World = GetWorld();
	// if (!World) return;
	// for (int32 i = 0; i < PlacedRooms.Num(); ++i)
	// {
	// 	SpawnRoomActors(World, PlacedRooms[i], i);
	// }
	// SpawnHallwayWalls(); // Spawn hallway walls after rooms are built
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

		const int32 StartFloor = FMath::RandRange(0, NumberOfFloors - 1);
		int32 NumFloors = FMath::RandRange(RoomHeightInFloorsMinMax.X, RoomHeightInFloorsMinMax.Y);
		if (StartFloor + NumFloors > NumberOfFloors) NumFloors = NumberOfFloors - StartFloor;

		FDungeonRoomBox NewRoomBox(
			FIntVector(RoomX, RoomY, StartFloor),
			FIntVector(RoomX + RoomWidth, RoomY + RoomDepth, StartFloor + NumFloors)
		);

		// Check for overlaps with existing rooms
		bool bOverlaps = false;
		for (const FDungeonRoom& Room : DungeonRooms)
		{
			FDungeonRoomBox PaddedBox = Room.Bounds;
			PaddedBox.Min -= FIntVector(1, 1, 1);
			PaddedBox.Max += FIntVector(1, 1, 1);
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

		PathCost.Cost = 100 + FVector::Dist(FVector(Neighbor->Position), FVector(GoalRoom.GridCenter));
		//base cost + heuristic

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


void ADungeonGenerator::SpawnRoomActors(UWorld* World, const FDungeonRoomBox& RoomBox, int32 RoomIndex)
{
	const FVector GeneratorOffset = GetActorLocation();

	auto SpawnMesh = [&](UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FString& Name)
	{
		if (!Mesh) return;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->SetActorLocation(Location);
			Actor->SetActorRotation(Rotation);
			Actor->SetActorScale3D(Scale);
			Actor->SetActorLabel(Name);
			SpawnedActors.Add(Actor);
		}
	};

	const FIntVector RoomGridSize(RoomBox.Max.X - RoomBox.Min.X, RoomBox.Max.Y - RoomBox.Min.Y, RoomBox.Max.Z - RoomBox.Min.Z);

	// 1. Spawn Floors
	for (int32 z = RoomBox.Min.Z; z < RoomBox.Max.Z; ++z)
	{
		if (RoomBox.Max.Z - RoomBox.Min.Z == 1) // If multi-floor then we dont spawn floor
		{
			FVector FloorCenterLocation = FVector(
				(RoomBox.Min.X + RoomGridSize.X / 2.0f) * GridUnitSize,
				(RoomBox.Min.Y + RoomGridSize.Y / 2.0f) * GridUnitSize,
				z * FloorHeight) + GeneratorOffset;
			SpawnMesh(RoomFloorMesh, FloorCenterLocation, FRotator::ZeroRotator, FVector(RoomGridSize.X, RoomGridSize.Y, 1.0f),
			          FString::Printf(TEXT("RoomFloor_%d_%d"), RoomIndex, z));
		}
	}

	// 2. Spawn Walls for each level in the room
	const FRotator HorizontalWallRotation = FRotator(0.0f, 90.0f, 0.0f);
	for (int32 z = RoomBox.Min.Z; z < RoomBox.Max.Z; ++z)
	{
		// Get the Z coordinate for the base of the current floor
		const float FloorBaseZ = z * FloorHeight;

		for (float h = 0.0f; h < FloorHeight; h += GridUnitSize)
		{
			const float CurrentWallZ = FloorBaseZ + h;

			// Bottom and Top Walls
			for (int32 x = RoomBox.Min.X; x < RoomBox.Max.X; ++x)
			{
				if (!HallwayTiles.Contains(FIntVector(x, RoomBox.Min.Y - 1, z)))
				{
					SpawnMesh(WallMesh, FVector((x + 0.5f) * GridUnitSize, RoomBox.Min.Y * GridUnitSize, CurrentWallZ) + GeneratorOffset, HorizontalWallRotation,
					          FVector::OneVector, TEXT("Wall"));
				}

				if (!HallwayTiles.Contains(FIntVector(x, RoomBox.Max.Y, z)))
				{
					SpawnMesh(WallMesh, FVector((x + 0.5f) * GridUnitSize, RoomBox.Max.Y * GridUnitSize, CurrentWallZ) + GeneratorOffset, HorizontalWallRotation,
					          FVector::OneVector, TEXT("Wall"));
				}
			}

			// Left and Right Walls
			for (int32 y = RoomBox.Min.Y; y < RoomBox.Max.Y; ++y)
			{
				if (!HallwayTiles.Contains(FIntVector(RoomBox.Min.X - 1, y, z)))
				{
					SpawnMesh(WallMesh, FVector(RoomBox.Min.X * GridUnitSize, (y + 0.5f) * GridUnitSize, CurrentWallZ) + GeneratorOffset, FRotator::ZeroRotator, FVector::OneVector,
					          TEXT("Wall"));
				}

				if (!HallwayTiles.Contains(FIntVector(RoomBox.Max.X, y, z)))
				{
					SpawnMesh(WallMesh, FVector(RoomBox.Max.X * GridUnitSize, (y + 0.5f) * GridUnitSize, CurrentWallZ) + GeneratorOffset, FRotator::ZeroRotator, FVector::OneVector,
					          TEXT("Wall"));
				}
			}
		}
	}
	// 3. Spawn Corners
	if (CornerMesh)
	{
		// This simplified logic spawns corners at the base of each vertical column of the room.
		const FIntVector MinGrid(RoomBox.Min);
		const FIntVector MaxGrid(RoomBox.Max);

		FVector CornerLocations[] = {
			FVector(MinGrid.X * GridUnitSize, MinGrid.Y * GridUnitSize, 0),
			FVector(MaxGrid.X * GridUnitSize, MinGrid.Y * GridUnitSize, 0),
			FVector(MinGrid.X * GridUnitSize, MaxGrid.Y * GridUnitSize, 0),
			FVector(MaxGrid.X * GridUnitSize, MaxGrid.Y * GridUnitSize, 0)
		};

		for (const FVector& CornerXY : CornerLocations)
		{
			for (int32 z = RoomBox.Min.Z; z < RoomBox.Max.Z; ++z)
			{
				SpawnMesh(CornerMesh, CornerXY + FVector(0, 0, z * FloorHeight) + GeneratorOffset, FRotator::ZeroRotator, FVector::OneVector, TEXT("Corner"));
			}
		}
	}
}

void ADungeonGenerator::SpawnHallwayWalls()
{
	UWorld* World = GetWorld();
	if (!World || !WallMesh) return;

	const FVector GeneratorOffset = GetActorLocation();
	const FRotator HorizontalWallRotation = FRotator(0.0f, 90.0f, 0.0f);
	const TSet<FIntVector> AllTiles = RoomTiles.Union(HallwayTiles);

	for (const FIntVector& Tile : HallwayTiles)
	{
		FIntVector Neighbors[4] = {
			FIntVector(Tile.X, Tile.Y + 1, Tile.Z), // North
			FIntVector(Tile.X, Tile.Y - 1, Tile.Z), // South
			FIntVector(Tile.X + 1, Tile.Y, Tile.Z), // East
			FIntVector(Tile.X - 1, Tile.Y, Tile.Z) // West
		};

		const float WallZ = Tile.Z * FloorHeight;

		auto SpawnWall = [&](const FVector& Location, const FRotator& Rotation)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
			if (Actor)
			{
				Actor->GetStaticMeshComponent()->SetStaticMesh(WallMesh);
				Actor->SetActorLocation(Location);
				Actor->SetActorRotation(Rotation);
				Actor->SetActorLabel(TEXT("HallwayWall"));
				SpawnedActors.Add(Actor);
			}
		};

		// Check North
		if (!AllTiles.Contains(Neighbors[0]))
		{
			FVector Location = FVector((Tile.X + 0.5f) * GridUnitSize, (Tile.Y + 1) * GridUnitSize, WallZ) + GeneratorOffset;
			SpawnWall(Location, HorizontalWallRotation);
		}
		// Check South
		if (!AllTiles.Contains(Neighbors[1]))
		{
			FVector Location = FVector((Tile.X + 0.5f) * GridUnitSize, Tile.Y * GridUnitSize, WallZ) + GeneratorOffset;
			SpawnWall(Location, HorizontalWallRotation);
		}
		// Check East
		if (!AllTiles.Contains(Neighbors[2]))
		{
			FVector Location = FVector((Tile.X + 1) * GridUnitSize, (Tile.Y + 0.5f) * GridUnitSize, WallZ) + GeneratorOffset;
			SpawnWall(Location, FRotator::ZeroRotator);
		}
		// Check West
		if (!AllTiles.Contains(Neighbors[3]))
		{
			FVector Location = FVector(Tile.X * GridUnitSize, (Tile.Y + 0.5f) * GridUnitSize, WallZ) + GeneratorOffset;
			SpawnWall(Location, FRotator::ZeroRotator);
		}
	}
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

void ADungeonGenerator::ProcessPath(TArray<FIntVector> Path)
{
	if (!Path.IsEmpty())
	{
		const FVector ActorLocation = GetActorLocation();
		const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, FloorHeight / 100.f);
		
		for (int32 i = 0; i < Path.Num(); i++)
		{
			FIntVector CurrentPathPoint = Path[i];

			if (Grid.InBounds(CurrentPathPoint) && Grid(CurrentPathPoint) == ECellType::None)
			{
				Grid(CurrentPathPoint) = ECellType::Hallway;
				HallwayTiles.Add(CurrentPathPoint);

				const FVector CellLocation = FVector(CurrentPathPoint.X * GridUnitSize, CurrentPathPoint.Y * GridUnitSize, CurrentPathPoint.Z * FloorHeight) + ActorLocation;
				const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
				HallwayISM->AddInstance(InstanceTransform);
			}

			if (i > 0)
			{
				FIntVector PreviousPathPoint = Path[i - 1];

				FIntVector Delta = CurrentPathPoint - PreviousPathPoint;

				if (Delta.Z != 0)
				{
					const int32 XDir = FMath::Clamp(Delta.X, -1, 1);
					const int32 YDir = FMath::Clamp(Delta.Y, -1, 1);
					const FIntVector VerticalOffset(0, 0, Delta.Z);
					const FIntVector HorizontalOffset(XDir, YDir, 0);

					// Define the 4 tiles that make up the 2x1 stairwell on both levels
					FIntVector StairTiles[] = {
						PreviousPathPoint + HorizontalOffset,
						PreviousPathPoint + HorizontalOffset * 2,
						PreviousPathPoint + VerticalOffset + HorizontalOffset,
						PreviousPathPoint + VerticalOffset + HorizontalOffset * 2
					};

					// Mark tiles as stairs, add to set, and spawn meshes
					for (const FIntVector& StairTile : StairTiles)
					{
						if (Grid.InBounds(StairTile))
						{
							Grid(StairTile) = ECellType::Stairs;
							HallwayTiles.Add(StairTile);
							const FVector CellLocation = FVector(StairTile.X * GridUnitSize, StairTile.Y * GridUnitSize, StairTile.Z * FloorHeight) + ActorLocation;
							const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
							StairsISM->AddInstance(InstanceTransform);
							// SpawnStairs(StairTile);
						}
					}
				}
			}
		}
	}
}

void ADungeonGenerator::PathfindHallways()
{
	if (DungeonRooms.Num() < 2) return;
	if (HallwayEdges.IsEmpty()) return;

	FIntVector GridSize3D = FIntVector(GridSize.X, GridSize.Y, NumberOfFloors);
	Pathfinder.Initialize(GridSize3D, this, [&](const FIntVector& Pos) -> FVector
	{
		return this->GridToWorld(Pos);
	});
	
	DebugDrawGrid();
	
	if (bVisualizePathfinding)
	{
		StartHallwayPathfinding_Async();
	}
	else
	{
		for (int32 i = 0; i < HallwayEdges.Num(); ++i)
		{
			PathfindHallwayEdge(i);
		}
		UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways instantly."));
	}
};

void ADungeonGenerator::PathfindHallwayEdge(int32 EdgeIndex)
{
	if (!HallwayEdges.IsValidIndex(EdgeIndex)) return;

	const TTuple<int32, int32>& Edge = HallwayEdges[EdgeIndex];
	const FDungeonRoom& RoomEdge1 = DungeonRooms[Edge.Get<0>()];
	const FDungeonRoom& RoomEdge2 = DungeonRooms[Edge.Get<1>()];

	TArray<FIntVector> Path = Pathfinder.FindPath(RoomEdge1.GridCenter, RoomEdge2.GridCenter,
	                                              [&](const FPathNode* Current, const FPathNode* Neighbor) -> FPathCost
	                                              {
	                                              	return this->CalculateHallwayPathCost(Current, Neighbor, RoomEdge2);
	                                              });

	ProcessPath(Path);
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
	const FVector CellScale(GridUnitSize / 100.f, GridUnitSize / 100.f, FloorHeight / 100.f);

	for (int32 z = 0; z < NumberOfFloors; ++z)
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
					const FVector CellLocation = FVector(x * GridUnitSize, y * GridUnitSize, z * FloorHeight) + ActorLocation;
					const FTransform InstanceTransform(FRotator::ZeroRotator, CellLocation, CellScale);
					TargetISM->AddInstance(InstanceTransform);
				}
			}
		}
	}
}