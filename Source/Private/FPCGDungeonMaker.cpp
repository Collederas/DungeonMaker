#include "FPCGDungeonMaker.h"

#include <vector>  // For converting to delaunay lib types

#include "DungeonMaker.h"
#include "DungeonTypes.h"
#include "Grid3D.h"
#include "Pathfinder3D.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "Tetrahedralizer.h"
#include "Data/PCGPointData.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "UObject/FastReferenceCollector.h"




FPCGElementPtr UDungeonPCGSettings::CreateElement() const
{
	return MakeShared<FDungeonPCGElement>();
}





FPathCost FDungeonPCGElement::CalculateHallwayPathCost(const FPathNode* Current, const FPathNode* Neighbor, const FDungeonRoom& GoalRoom, FGrid3D<ECellType>& Grid)
{
	FPathCost PathCost;
	const FIntVector Delta = Neighbor->Position - Current->Position;

	if (Delta.Z == 0) // Flat hallway
	{
		PathCost.Cost = FVector::Dist(FVector(Neighbor->Position), FVector(GoalRoom.GridCenter));
		const ECellType NeighborNodeType = Grid(Neighbor->Position);

		if (NeighborNodeType == Stairs)
		{
			return PathCost;
		}

		switch (NeighborNodeType)
		{
		case None:
		case Hallway:
			PathCost.Cost += 1;
			break;
		case Room:
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
		const ECellType CurrentNodeType = Grid(Current->Position);
		const ECellType NeighborNodeType = Grid(Neighbor->Position);
		if ((CurrentNodeType != None && CurrentNodeType != Hallway) ||
			(NeighborNodeType != None && NeighborNodeType != Hallway))
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
		if (Grid(Current->Position + HorizontalOffset) != None ||
			Grid(Current->Position + HorizontalOffset * 2) != None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset) != None ||
			Grid(Current->Position + VerticalOffset + HorizontalOffset * 2) != None)
		{
			return PathCost;
		}
		PathCost.bTraversable = true;
		PathCost.bIsStairs = true;
	}
	return PathCost;
}

FVector FDungeonPCGElement::GridToWorld(const FIntVector& GridPos, float GridUnitSize)
{
	const FVector WorldPos(
	   GridPos.X * GridUnitSize,
	   GridPos.Y * GridUnitSize,
	   GridPos.Z * GridUnitSize
	);
	return WorldPos;
}

void FDungeonPCGElement::PlaceRooms(FGrid3D<ECellType>& Grid, TArray<FDungeonRoom>& DungeonRooms, const UDungeonPCGSettings* Settings, FRandomStream& InRandomStream)
{
	FIntPoint RoomSize = Settings->RoomSizeMinMax;
	FIntPoint RoomHeight = Settings->RoomHeightInFloorsMinMax;
    
	// Validate min/max values
	if (RoomSize.X > RoomSize.Y)
	{
		Swap(RoomSize.X, RoomSize.Y);
	}
	if (RoomHeight.X > RoomHeight.Y)
	{
		Swap(RoomHeight.X, RoomHeight.Y);
	}

	for (int32 i = 0; i < Settings->NumberOfRooms; ++i)
	{
		// Generate random properties for a potential room using the provided stream
		const int32 RoomWidth = InRandomStream.RandRange(RoomSize.X, RoomSize.Y);
		const int32 RoomDepth = InRandomStream.RandRange(RoomSize.X, RoomSize.Y);
		const int32 RoomX = InRandomStream.RandRange(0, Settings->GridSize.X - RoomWidth);
		const int32 RoomY = InRandomStream.RandRange(0, Settings->GridSize.Y - RoomDepth);

		const int32 StartFloor = InRandomStream.RandRange(0, Settings->GridSize.Z - 1);
		int32 NumFloors = InRandomStream.RandRange(RoomHeight.X, RoomHeight.Y);
		if (StartFloor + NumFloors > Settings->GridSize.Z)
		{
			NumFloors = Settings->GridSize.Z - StartFloor;
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
			FDungeonRoom NewRoom;
			NewRoom.Bounds = NewRoomBox;

			const FVector BoxCenterGrid(
				(NewRoomBox.Min.X + NewRoomBox.Max.X - 1) / 2.0f,
				(NewRoomBox.Min.Y + NewRoomBox.Max.Y - 1) / 2.0f,
				(NewRoomBox.Min.Z + NewRoomBox.Max.Z - 1) / 2.0f
			);

			NewRoom.GridCenter = FIntVector(FMath::RoundToInt(BoxCenterGrid.X), FMath::RoundToInt(BoxCenterGrid.Y), FMath::RoundToInt(BoxCenterGrid.Z));
			NewRoom.WorldCenter = GridToWorld(NewRoom.GridCenter, Settings->GridUnitSize);

			DungeonRooms.Add(NewRoom);

			for (int32 z = NewRoom.Bounds.Min.Z; z < NewRoom.Bounds.Max.Z; ++z)
			{
				for (int32 y = NewRoom.Bounds.Min.Y; y < NewRoom.Bounds.Max.Y; ++y)
				{
					for (int32 x = NewRoom.Bounds.Min.X; x < NewRoom.Bounds.Max.X; ++x)
					{
						Grid(x, y, z) = Room;
					}
				}
			}
		}
	}
	UE_LOG(LogDungeonMaker, Log, TEXT("Generated %d rooms."), DungeonRooms.Num());
}

void FDungeonPCGElement::CalculateDelaunayTetrahedralization(TArray<TTuple<int32, int32>>& DelaunayEdges, const TArray<FDungeonRoom>& DungeonRooms)
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

void FDungeonPCGElement::CalculateMst(TArray<TTuple<int32, int32>>& MstEdges, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& DelaunayEdges)
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

void FDungeonPCGElement::CreateHallwayEdges(TArray<TTuple<int32, int32>>& HallwayEdges, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& MstEdges, const TArray<TTuple<int32, int32>>& DelaunayEdges, const UDungeonPCGSettings* Settings, FRandomStream& InRandomStream)
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

	for (const auto& Edge : DelaunayEdges)
	{
		const TTuple<int32, int32> CanonicalEdge(FMath::Min(Edge.Get<0>(), Edge.Get<1>()), FMath::Max(Edge.Get<0>(), Edge.Get<1>()));
		if (!MstEdgeSet.Contains(CanonicalEdge))
		{
			// Use the provided stream's FRand() method
			if (InRandomStream.FRand() < Settings->ExtraConnectionChance)
			{
				HallwayEdges.Add(Edge);
			}
		}
	}
}

// This is a new helper function that replaces the old ProcessPathTile
void FDungeonPCGElement::ProcessPathTile(FGrid3D<ECellType>& Grid, const FIntVector& CurrentPathPoint, const FIntVector& PreviousPathPoint, bool bIsFirstTile)
{
	// Process the current tile as a hallway
	if (Grid.InBounds(CurrentPathPoint) && Grid(CurrentPathPoint) == ECellType::None)
	{
		Grid(CurrentPathPoint) = ECellType::Hallway;
	}

	// Process stairs if there's a vertical change from the previous tile
	if (!bIsFirstTile)
	{
		FIntVector Delta = CurrentPathPoint - PreviousPathPoint;
		if (Delta.Z != 0)
		{
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
				}
			}
		}
	}
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
				const int32 GraphSeed = Context->GetSeed();
				const FVector LocalPosition = FVector(CurrentCell) * Settings->GridUnitSize;
				const FTransform PointTransform = FTransform(LocalPosition) * TargetActorTransform;

				if (GetCell(Grid, x, y, z - 1) == ECellType::None) // Check if a floor is needed
	            {
	                FPCGPoint& FloorPoint = Points.Emplace_GetRef();
	                FloorPoint.Transform = PointTransform;
	                FloorPoint.Seed = PCGHelpers::ComputeSeed(GraphSeed); // Use an offset for unique seeds
	                FloorPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(FloorPoint.MetadataEntry, TEXT("Floor"));
	                YawAttr->SetValue(FloorPoint.MetadataEntry, 0.0f);
	            }

	            // --- Generate a point for the CEILING ---
	            if (GetCell(Grid, x, y, z + 1) == ECellType::None)
	            {
	                FPCGPoint& CeilPoint = Points.Emplace_GetRef();
	                CeilPoint.Transform = PointTransform;
	                CeilPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(CeilPoint.MetadataEntry, TEXT("Ceiling"));
	                YawAttr->SetValue(CeilPoint.MetadataEntry, 0.0f);
	            }
	            
	            // --- Generate a separate point for EACH required WALL ---
	            // Check North (+X)
	            if (GetCell(Grid, x + 1, y, z) == ECellType::None)
	            {
	                FPCGPoint& WallPoint = Points.Emplace_GetRef();
	                WallPoint.Transform = PointTransform;
	                WallPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
	                YawAttr->SetValue(WallPoint.MetadataEntry, 0.0f); // North
	            }
	            // Check East (+Y)
	            if (GetCell(Grid, x, y + 1, z) == ECellType::None)
	            {
	                FPCGPoint& WallPoint = Points.Emplace_GetRef();
	                WallPoint.Transform = PointTransform;
	                WallPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
	                YawAttr->SetValue(WallPoint.MetadataEntry, 90.0f); // East
	            }
	            // Check South (-X)
	            if (GetCell(Grid, x - 1, y, z) == ECellType::None)
	            {
	                FPCGPoint& WallPoint = Points.Emplace_GetRef();
	                WallPoint.Transform = PointTransform;
	                WallPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
	                YawAttr->SetValue(WallPoint.MetadataEntry, 180.0f); // South
	            }
	            // Check West (-Y)
	            if (GetCell(Grid, x, y - 1, z) == ECellType::None)
	            {
	                FPCGPoint& WallPoint = Points.Emplace_GetRef();
	                WallPoint.Transform = PointTransform;
	                WallPoint.MetadataEntry = Metadata->AddEntry();
	                MeshTypeAttr->SetValue(WallPoint.MetadataEntry, TEXT("Wall"));
	                YawAttr->SetValue(WallPoint.MetadataEntry, -90.0f); // West
	            }
                
				if (CellType == ECellType::Stairs)
				{
					// Corrected Stair Yaw Logic
					float Yaw = 0.0f;
					if (GetCell(Grid, x + 1, y, z) != ECellType::None) Yaw = 0.0f;    // Connects North (+X)
					else if (GetCell(Grid, x - 1, y, z) != ECellType::None) Yaw = 180.0f; // Connects South (-X)
					else if (GetCell(Grid, x, y + 1, z) != ECellType::None) Yaw = 90.0f;  // Connects East (+Y)
					else if (GetCell(Grid, x, y - 1, z) != ECellType::None) Yaw = -90.0f; // Connects West (-Y)
                    
					FPCGPoint& StairPoint = Points.Emplace_GetRef();
					StairPoint.Transform = PointTransform;
					StairPoint.MetadataEntry = Metadata->AddEntry();
					MeshTypeAttr->SetValue(StairPoint.MetadataEntry, TEXT("Stairs"));
					YawAttr->SetValue(StairPoint.MetadataEntry, Yaw);
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

// A new helper to process a single path and update the grid
void FDungeonPCGElement::ProcessPath(FGrid3D<ECellType>& Grid, const TArray<FIntVector>& Path)
{
    if (!Path.IsEmpty())
	{
		for (int32 i = 0; i < Path.Num(); i++)
		{
			FIntVector PreviousPoint = (i > 0) ? Path[i - 1] : Path[i];
			ProcessPathTile(Grid, Path[i], PreviousPoint, i == 0);
		}
	}
}


// The new main pathfinding function. All async and debug code is removed.
void FDungeonPCGElement::PathfindHallways(FGrid3D<ECellType>& Grid, const TArray<FDungeonRoom>& DungeonRooms, const TArray<TTuple<int32, int32>>& HallwayEdges, const UDungeonPCGSettings* Settings)
{
    if (DungeonRooms.Num() < 2 || HallwayEdges.IsEmpty())
    {
        return;
    }
    
    Pathfinder3D Pathfinder;
	const float GridUnitSize = Settings->GridUnitSize;
	
	Pathfinder.Initialize(Settings->GridSize, nullptr, [GridUnitSize](const FIntVector& Pos) -> FVector
		{
			return FDungeonPCGElement::GridToWorld(Pos, GridUnitSize);
		}
    );

    for (const auto& Edge : HallwayEdges)
    {
        const FDungeonRoom& RoomEdge1 = DungeonRooms[Edge.Get<0>()];
        const FDungeonRoom& RoomEdge2 = DungeonRooms[Edge.Get<1>()];

        TArray<FIntVector> Path = Pathfinder.FindPath(RoomEdge1.GridCenter, RoomEdge2.GridCenter,
           [&](const FPathNode* Current, const FPathNode* Neighbor) -> FPathCost
           {
               // The lambda captures 'Grid' by reference and passes it to our helper.
               return CalculateHallwayPathCost(Current, Neighbor, RoomEdge2, Grid);
           });
        
        // Process the found path and add it to the grid immediately.
        ProcessPath(Grid, Path);
    }
    UE_LOG(LogDungeonMaker, Log, TEXT("Finished pathfinding all hallways."));
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

	// Create a random stream initialized with the component's seed.
	const int32 Seed = Context->GetSeed();
	FRandomStream RandomStream(Seed);

	// Create local variables for the generation process
	FGrid3D<ECellType> Grid(Settings->GridSize, [](const FIntVector& Pos) { return ECellType::None; }, FIntVector::ZeroValue);
	TArray<FDungeonRoom> DungeonRooms;
	TArray<TTuple<int32, int32>> DelaunayEdges;
	TArray<TTuple<int32, int32>> MstEdges;
	TArray<TTuple<int32, int32>> HallwayEdges;

	// Call generation steps, passing the random stream to functions that need it
	PlaceRooms(Grid, DungeonRooms, Settings, RandomStream);
	CalculateDelaunayTetrahedralization(DelaunayEdges, DungeonRooms);
	CalculateMst(MstEdges, DungeonRooms, DelaunayEdges);
	CreateHallwayEdges(HallwayEdges, DungeonRooms, MstEdges, DelaunayEdges, Settings, RandomStream);
	PathfindHallways(Grid, DungeonRooms, HallwayEdges, Settings);
    
	GeneratePCGAttributes(Grid, Context, Settings);
	return true;
}