#include "DungeonGenerator.h"
#include "Engine/StaticMeshActor.h"
#include "DungeonMaker.h"
#include "MeshAttributes.h"
#include "Pathfinder3D.h"
#include "Components/InstancedStaticMeshComponent.h"

UDungeonGenerator::UDungeonGenerator(): OwningActor(nullptr)
{
}

void UDungeonGenerator::Init(AActor* InOwner, FIntVector InGridSize, float InGridUnitSize, float InNumOfRooms,
                             FIntPoint InRoomSizeMinMax, FIntPoint InRoomHeightMinMax, float InExtraConnectionChance)
{
	OwningActor = InOwner;
	GridSize = InGridSize;
	GridUnitSize = InGridUnitSize;
	NumberOfRooms = InNumOfRooms;
	RoomSizeMinMax = InRoomSizeMinMax;
	RoomHeightInFloorsMinMax = InRoomHeightMinMax;
	ExtraConnectionChance = InExtraConnectionChance;
}

void UDungeonGenerator::ClearDungeon()
{
	DelaunayEdges.Empty();
	MstEdges.Empty();
	HallwayEdges.Empty();

	Grid.Clear();
	DungeonRooms.Empty();
}

FGrid3D<FCellData> UDungeonGenerator::GenerateDungeon()
{
	ClearDungeon();
	Grid = FGrid3D<FCellData>(FIntVector(GridSize.X, GridSize.Y, GridSize.Z),
	                          [](const FIntVector& Pos) { return FCellData(); }, FIntVector::ZeroValue);
	PlaceRooms();
	CalculateDelaunayTetrahedralization();
	CalculateMst();
	CreateHallways();
	PathfindHallways();
	
	UE_LOG(LogDungeonMaker, Verbose, TEXT("Created dungeon with: %d rooms on %d floors"), DungeonRooms.Num(), GridSize.Z);

	return Grid;
};

void UDungeonGenerator::PlaceRooms()
{
	DungeonRooms.Empty();

	// Validate min/max values
	if (RoomSizeMinMax.X > RoomSizeMinMax.Y)
	{
		Swap(RoomSizeMinMax.X, RoomSizeMinMax.Y);
	}
	if (RoomHeightInFloorsMinMax.X > RoomHeightInFloorsMinMax.Y)
	{
		Swap(RoomHeightInFloorsMinMax.X, RoomHeightInFloorsMinMax.Y);
	}

	for (int32 i = 0; i < NumberOfRooms; ++i)
	{
		// Generate random properties for a potential room
		const int32 RoomWidth = FMath::RandRange(RoomSizeMinMax.X, RoomSizeMinMax.Y);
		const int32 RoomDepth = FMath::RandRange(RoomSizeMinMax.X, RoomSizeMinMax.Y);
		const int32 RoomX = FMath::RandRange(0, GridSize.X - RoomWidth);
		const int32 RoomY = FMath::RandRange(0, GridSize.Y - RoomDepth);

		const int32 StartFloor = FMath::RandRange(0, GridSize.Z - 1);
		int32 NumFloors = FMath::RandRange(RoomHeightInFloorsMinMax.X, RoomHeightInFloorsMinMax.Y);
		if (StartFloor + NumFloors > GridSize.Z)
		{
			NumFloors = GridSize.Z - StartFloor;
		}

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
				(NewRoomBox.Min.Z + NewRoomBox.Max.Z - 1) / 2.0f
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
						FCellData RoomCell(ECellType::Room, FRotator(0, 0, 0));
						Grid(x, y, z) = RoomCell;
					}
				}
			}
		}
	}
	UE_LOG(LogDungeonMaker, Log, TEXT("Generated %d rooms."), DungeonRooms.Num());
}

FPathCost UDungeonGenerator::CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom)
{
	FPathCost PathCost;
	const FIntVector Delta = Neighbor->Position - Current->Position;

	if (Delta.Z == 0) // Flat hallway
	{
		PathCost.Cost = FVector::Dist(FVector(Neighbor->Position), FVector(GoalRoom.GridCenter));
		const FCellData NeighborNodeType = Grid(Neighbor->Position);

		if (NeighborNodeType.Type == ECellType::Stairs)
		{
			return PathCost;
		}

		switch (NeighborNodeType.Type)
		{
		case ECellType::None:
		case ECellType::Hallway:
			PathCost.Cost += 1;
			break;
		case ECellType::Room:
			PathCost.Cost += 5; // Higher cost to discourage pathing through rooms
			break;
		default:
			break;
		}
		PathCost.bTraversable = true;
	}
	else
	{
		// if Delta is vertical, then we have a staircase
		const ECellType CurrentNodeType = Grid(Current->Position).Type;
		const ECellType NeighborNodeType = Grid(Neighbor->Position).Type;
		if ((CurrentNodeType != ECellType::None && CurrentNodeType != ECellType::Hallway) ||
			(NeighborNodeType != ECellType::None && NeighborNodeType != ECellType::Hallway))
		{
			return PathCost;
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
		if (Grid(Current->Position + HorizontalOffset).Type != ECellType::None ||
			Grid(Current->Position + HorizontalOffset * 2).Type != ECellType::None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset).Type != ECellType::None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset * 2).Type != ECellType::None)
		{
			return PathCost;
		}
		PathCost.bTraversable = true;
		PathCost.bIsStairs = true;
	}
	return PathCost;
}

void UDungeonGenerator::CalculateDelaunayTetrahedralization()
{
	DelaunayEdges.Empty();
	if (DungeonRooms.Num() < 4)
	{
		return;
	}

	// Convert room centers to the library's point format.
	std::vector<geom::Point3D> Points;
	Points.reserve(DungeonRooms.Num());
	for (const FDungeonRoom& Room : DungeonRooms)
	{
		const FVector Center = Room.WorldCenter;
		Points.push_back({Center.X, Center.Y, Center.Z});
	}

	const std::vector<geom::Tetrahedron> Tetrahedra = geom::Tetrahedralizer::Triangulate(Points);

	// Extract unique edges from the tetrahedra.
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

void UDungeonGenerator::CalculateMst()
{
	MstEdges.Empty();
	const int32 NumRooms = DungeonRooms.Num();
	if (NumRooms < 2)
	{
		return;
	}

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

		if (U == -1)
		{
			break;
		}
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

void UDungeonGenerator::CreateHallways()
{
	HallwayEdges.Empty();
	if (DungeonRooms.Num() < 2)
	{
		return;
	}

	HallwayEdges = MstEdges;

	TSet<TTuple<int32, int32>> MstEdgeSet;
	for (const auto& Edge : MstEdges)
	{
		MstEdgeSet.Add(TTuple<int32, int32>(FMath::Min(Edge.Get<0>(), Edge.Get<1>()), FMath::Max(Edge.Get<0>(), Edge.Get<1>())));
	}

	// Randomly add some extra edges from the Delaunay graph to create loops.
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

void UDungeonGenerator::PathfindHallways()
{
	if (DungeonRooms.Num() < 2)
	{
		return;
	}
	if (HallwayEdges.IsEmpty())
	{
		return;
	}
	

	FIntVector GridSize3D = FIntVector(GridSize.X, GridSize.Y, GridSize.Z);
	Pathfinder.Initialize(GridSize3D, this, [&](const FIntVector& Pos) -> FVector
	{
		return this->GridToWorld(Pos);
	});
			
	// Calculate and draw everything instantly with no visualization
	for (int32 i = 0; i < HallwayEdges.Num(); ++i)
	{
		TArray<FIntVector> Path = PathfindHallwayEdge(i);
		ProcessPath(Path);
	}
	UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways."));
}

TArray<FIntVector> UDungeonGenerator::PathfindHallwayEdge(int32 EdgeIndex)
{
	if (!HallwayEdges.IsValidIndex(EdgeIndex))
	{
		return TArray<FIntVector>();
	}

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


void UDungeonGenerator::ProcessPath(TArray<FIntVector> Path)
{
	if (!Path.IsEmpty())
	{
		for (int32 i = 0; i < Path.Num(); i++)
		{
			FIntVector CurrentPathPoint = Path[i];
			FIntVector PreviousPoint = (i > 0) ? Path[i - 1] : Path[i];

			// Process the current tile as a hallway
			if (Grid.InBounds(CurrentPathPoint) && Grid(CurrentPathPoint).Type == ECellType::None)
			{
				FCellData HallwayData;
				HallwayData.Type = ECellType::Hallway;
				Grid(CurrentPathPoint) = HallwayData;
			}

			if (i == 0) continue;
			
			FIntVector Delta = CurrentPathPoint - PreviousPoint;

			if (Delta.Z != 0)
			{
				// (delta.y, delta.x) instead of (delta.x, delta.y) considers X as the "forward" axis instead of Y. This means the mesh
				// should face X as foreard dir (and pivot at center as the other meshes).
				
				float Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Delta.Y), static_cast<float>(Delta.X)));
				const bool bIsAscending = Delta.Z > 0;

				if (!bIsAscending)
				{
					Yaw += 180.0f;
				}
				
				const FRotator StairRotation(0.0f, FMath::UnwindDegrees(Yaw), 0.0f);
            
				const int32 XDir = FMath::Clamp(Delta.X, -1, 1);
				const int32 YDir = FMath::Clamp(Delta.Y, -1, 1);
				const FIntVector HorizontalOffset(XDir, YDir, 0);
				
            
				// Define the tile locations for the stair parts
				FIntVector Lower1 = PreviousPoint + HorizontalOffset;
				FIntVector Lower2 = PreviousPoint + HorizontalOffset * 2;
				FIntVector Upper1 = PreviousPoint + FIntVector(0, 0, Delta.Z) + HorizontalOffset;
				FIntVector Upper2 = PreviousPoint + FIntVector(0, 0, Delta.Z) + HorizontalOffset * 2;
                        
				
				if (Grid.InBounds(Lower1))
				{
					FCellData& Cell = Grid(Lower1);
					Cell.Type = ECellType::Stairs;
					Cell.Rotation = StairRotation;
					Cell.StairPart = bIsAscending ? EStairPart::LowerMesh : EStairPart::UpperMesh;
				}
				
				if (Grid.InBounds(Upper2))
				{
					FCellData& Cell = Grid(Upper2);
					Cell.Type = ECellType::Stairs;
					Cell.Rotation = StairRotation;
					Cell.StairPart = bIsAscending ? EStairPart::UpperMesh : EStairPart::LowerMesh;
				}
                        
				// prevent floor/ceiling generation
				if (Grid.InBounds(Lower2))
				{
					FCellData& Cell = Grid(Lower2);
					Cell.Type = ECellType::Stairs;
					Cell.Rotation = StairRotation;
					Cell.StairPart = EStairPart::EmptySpace;
				}
				if (Grid.InBounds(Upper1))
				{
					FCellData& Cell = Grid(Upper1);
					Cell.Type = ECellType::Stairs;
					Cell.Rotation = StairRotation;
					Cell.StairPart = EStairPart::EmptySpace;
				}
			}
		}
	}
}
