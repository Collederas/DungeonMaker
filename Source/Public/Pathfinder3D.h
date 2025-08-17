#pragma once

#include "CoreMinimal.h"
#include "Grid3D.h"
#include "SimpleQueue.h"


struct FPathfinderDebugDrawer
{
	bool bVisualize = false;
	TFunction<void(FIntVector Pos)> DrawCurrentNode;
	TFunction<void(FIntVector Pos)> DrawNeighborNode;
	TFunction<void(FIntVector Pos)> DrawClosedNode;
	TFunction<void(FIntVector Pos)> DrawPathNode;
};

/** Represents a single node in the A* search space. Stores all nodes that the path has
 * visited and the cost of the whole path up to the node.
**/

struct FPathNode
{
	FIntVector Position;
	FPathNode* Previous;

	// All the positions that the path took to reach this node.
	TSet<FIntVector> TraversalHistory;

	// *Accumulated* cost from the start to the current node.
	float Cost;
	
	FPathNode() 
		: Position(FIntVector::ZeroValue)
		, Previous(nullptr)
		, Cost(FLT_MAX)
	{
	}

	FPathNode(const FIntVector& InPos) 
		: Position(InPos)
		, Previous(nullptr)
		, Cost(FLT_MAX)
	{
	}

	bool operator==(const FPathNode& Other) const
	{
		return Position == Other.Position;
	}
};

struct FPathCost
{
	bool bTraversable = false;
	float Cost = 0;
	bool bIsStairs = false;
};

enum class EPathStatus { InProgress, Succeeded, Failed };


/**
 * A* inspired pathfinder for creating architecturally sound hallways and staircases.
 * Ported to Unreal C++ from https://github.com/vazgriz/DungeonGenerator/blob/master/Assets/Scripts3D/DungeonPathfinder3D.cs
 *
 * I added some functionality to help debugging the algorithm using the VisLog as it is generally quite complex to debug with
 * breakpoints.
 */
class DUNGEONMAKER_API Pathfinder3D
{
public:

	Pathfinder3D();

	~Pathfinder3D() = default;

	void Initialize(const FIntVector& InGridSize, UObject* InOwner, TFunction<FVector(const FIntVector&)> InGridToWorldFunc);
	
	void ResetNodes()
	{
		const FIntVector Size = Grid.Size;
		for (int32 X = 0; X < Size.X; ++X)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 Z = 0; Z < Size.Z; ++Z)
				{
					FPathNode& Node = Grid(X, Y, Z);
					Node.Previous = nullptr;
					Node.Cost = FLT_MAX;
					Node.TraversalHistory.Reset();
				}
			}
		}
	}
	void StartPath(FIntVector Start, FIntVector End, TFunction<FPathCost(FPathNode*, FPathNode*)> CostFunc);
	EPathStatus Step();
	TArray<FIntVector> GetFinalPath();
	
	TArray<FIntVector> FindPath(FIntVector Start, FIntVector End, TFunction<FPathCost(FPathNode*, FPathNode*)> CostFunction);

	FPathNode* GetCurrentNode() const { return CurrentNode; };
	
private:
	FGrid3D<FPathNode> Grid;
	TMinHeap<FPathNode*> OpenQueue;
	TSet<FPathNode*> ClosedList;
	TArray<FIntVector> PathStack;
	
	UObject* OwnerContext = nullptr;
	
	// The pathfinder works with its own FGrid3D that is initialized to be = to the main grid of the generator just with coordinates instead of celltypes
	// This function converts a grid to world location based on the user rules (the grid of the generator)
	TFunction<FVector(const FIntVector&)> GridToWorldFunc;
	
	static const FIntVector Neighbours[12];

	TArray<FPathNode*> VisitedNodes;

	// STATE VARIABLES (for advancing in steps if needed for debug)
	FPathNode* GoalNode = nullptr;
	FPathNode* CurrentNode = nullptr; // node currently processed
	TFunction<FPathCost(FPathNode*, FPathNode*)> CurrentCostFunction;
};