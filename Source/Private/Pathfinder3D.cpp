#include "Pathfinder3D.h"

#include "DungeonMaker.h"


Pathfinder3D::Pathfinder3D()
	: OpenQueue(([](FPathNode* A, FPathNode* B) { return A->Cost < B->Cost; }))
{
}

void Pathfinder3D::Initialize(const FIntVector& InGridSize, UObject* InOwner, TFunction<FVector(const FIntVector&)> InGridToWorldFunc)
{
	Grid = FGrid3D<FPathNode>(InGridSize, [](const FIntVector& Pos) { return FPathNode(Pos); });
	OwnerContext = InOwner;
	GridToWorldFunc = InGridToWorldFunc;
}

void Pathfinder3D::StartPath(FIntVector Start, FIntVector End, TFunction<FPathCost(FPathNode*, FPathNode*)> CostFunc)
{
	checkf(Grid.IsInitialized(), TEXT("Pathfinder3D has not been initialized!"));

	// Clear previous run's data
	OpenQueue.Clear(); 

	ClosedList.Reset();
	VisitedNodes.Reset();
	
	GoalNode = &Grid(End);
	CurrentCostFunction = CostFunc;

	FPathNode* StartNode = &Grid(Start);
	StartNode->Cost = 0;
	OpenQueue.Push(StartNode);
	VisitedNodes.Add(StartNode);
}

EPathStatus Pathfinder3D::Step()
{
	if (OpenQueue.Num() == 0)
	{
		return EPathStatus::Failed;
	}

	FPathNode* Node = OpenQueue.Pop();
	ClosedList.Add(Node);

	const FVector ClosedWorldPos = GridToWorldFunc(Node->Position);
	UE_VLOG_BOX(OwnerContext, LogDungeonMaker, Verbose, FBox(ClosedWorldPos - 40.f, ClosedWorldPos + 40.f), FColor::Red, TEXT("Closed"));

	const FVector NodeWorldPos = this->GridToWorldFunc(Node->Position);
	UE_VLOG_BOX(OwnerContext, LogDungeonMaker, Verbose, FBox(NodeWorldPos - 50.f, NodeWorldPos + 50.f), FColor::Orange, TEXT("Current"));

	if (Node->Previous)
	{
		const FVector PrevWorldPos = this->GridToWorldFunc(Node->Previous->Position);
		UE_VLOG_ARROW(OwnerContext, LogDungeonMaker, Verbose, NodeWorldPos, PrevWorldPos, FColor::White, TEXT(""));
	}

	if (Node->Position == GoalNode->Position)
	{
		return EPathStatus::Succeeded;
	}

	for (FIntVector StairOffset : StairPaddedNeighbours)
	{
		FIntVector NeighborPos = Node->Position + StairOffset;
		if (!Grid.InBounds(NeighborPos))
		{
			continue;
		}

		FPathNode* NeighborNode = &Grid(NeighborPos);

		if (ClosedList.Contains(NeighborNode)) continue; // Node has been fully evaluated and its cost is known
		if (Node->TraversalHistory.Contains(NeighborPos)) continue;

		// Log the neighbor being considered as a Yellow box.
		const FVector NeighborWorldPos = this->GridToWorldFunc(NeighborNode->Position);
		UE_VLOG_BOX(OwnerContext, LogDungeonMaker, Verbose, FBox(NeighborWorldPos - 25.f, NeighborWorldPos + 25.f), FColor::Yellow, TEXT("Neighbor"));

		// returns the cost to step to the neighbor 
		FPathCost CostToNeighbor = CurrentCostFunction(NeighborNode, Node);
		if (!CostToNeighbor.bTraversable) continue;

		if (CostToNeighbor.bIsStairs)
		{
			// If neighbor is stairs and we already visited them, we skip. (stairs are 2x2 + 1 unit of padding for landings that is supposed to be traversable)
			FIntVector HorizontalOffset(FMath::Clamp(StairOffset.X, -1, 1), FMath::Clamp(StairOffset.Y, -1, 1), 0);
			FIntVector VerticalOffset(0, 0, StairOffset.Z);
			if (Node->TraversalHistory.Contains(Node->Position + HorizontalOffset) ||
				Node->TraversalHistory.Contains(Node->Position + HorizontalOffset * 2) ||
				Node->TraversalHistory.Contains(Node->Position + VerticalOffset + HorizontalOffset) ||
				Node->TraversalHistory.Contains(Node->Position + VerticalOffset + HorizontalOffset * 2))
			{
				continue;
			}
		}

		float NewCost = CostToNeighbor.Cost + Node->Cost; // New path cost to reach neighbor

		// If the cost of reaching this neighbor through the current path (NewCost) is less than
		// any cost we’ve previously recorded for that neighbor (NeighborNode->Cost),
		// then this new path is better, and we should update the neighbor’s Cost and Previous pointer.
		if (NewCost < NeighborNode->Cost)
		{
			if (NeighborNode->Cost == FLT_MAX) // Check if it's the first time we calculate this node
			{
				VisitedNodes.Add(NeighborNode);
				UE_VLOG_BOX(OwnerContext, LogDungeonMaker, Verbose, FBox(NeighborWorldPos - 25.f, NeighborWorldPos + 25.f), FColor::Green, TEXT("Visited"));
			}

			NeighborNode->Previous = Node;
			NeighborNode->Cost = NewCost;

			UE_LOG(LogDungeonMaker, Log, TEXT("Queue contains %d nodes"), OpenQueue.Num());

			OpenQueue.Update(NeighborNode);

			NeighborNode->TraversalHistory = Node->TraversalHistory;
			NeighborNode->TraversalHistory.Add(Node->Position);

			// ...and if see the neighbor is a staircase then we marke it all traversed (excl. landing)
			if (CostToNeighbor.bIsStairs)
			{
				FIntVector HorizontalOffset(FMath::Clamp(StairOffset.X, -1, 1), FMath::Clamp(StairOffset.Y, -1, 1), 0);
				FIntVector VerticalOffset(0, 0, StairOffset.Z);

				NeighborNode->TraversalHistory.Add(Node->Position + HorizontalOffset);
				NeighborNode->TraversalHistory.Add(Node->Position + HorizontalOffset * 2);
				NeighborNode->TraversalHistory.Add(Node->Position + VerticalOffset + HorizontalOffset);
				NeighborNode->TraversalHistory.Add(Node->Position + VerticalOffset + HorizontalOffset * 2);
			}
		}
	}
	return EPathStatus::InProgress;
}

TArray<FIntVector> Pathfinder3D::GetFinalPath()
{
	TArray<FIntVector> FinalPath;
	FPathNode* Current = GoalNode;

	// Reconstruct the path by walking backwards from the goal
	while (Current)
	{
		FinalPath.Add(Current->Position);
        
		// Log the final path segments for VLog
		if (Current->Previous)
		{
			const FVector CurrentWorldPos = GridToWorldFunc(Current->Position);
			const FVector PrevWorldPos = GridToWorldFunc(Current->Previous->Position);
			UE_VLOG_SEGMENT(OwnerContext, LogDungeonMaker, Verbose, CurrentWorldPos, PrevWorldPos, FColor::Green, TEXT("Final Path"));
		}
		Current = Current->Previous;
	}
    
	Algo::Reverse(FinalPath);

	for (FPathNode* VisitedNode : VisitedNodes)
	{
		VisitedNode->Previous = nullptr;
		VisitedNode->Cost = FLT_MAX;
		VisitedNode->TraversalHistory.Reset();
	}

	return FinalPath;
}

TArray<FIntVector> Pathfinder3D::FindPath(FIntVector Start, FIntVector End, TFunction<FPathCost(FPathNode*, FPathNode*)> CostFunction)
{
	StartPath(Start, End, CostFunction);

	while (Step() == EPathStatus::InProgress)
	{
		// This loop runs at full speed, without timers or VLog frames.
	}
	
	return GetFinalPath();
}

