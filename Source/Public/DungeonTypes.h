#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "DungeonTypes.generated.h"


UENUM()
enum class ECellType
{
	None,
	Room,
	Hallway,
	Stairs
};

UENUM()
enum class EStairPart : uint8
{
	None,        // Not part of a staircase
	LowerMesh,   // The cell where the lower stair mesh should be spawned
	UpperMesh,   // The cell where the upper stair mesh should be spawned
	EmptySpace   // Part of the stair volume, but no mesh is spawned here
};

USTRUCT(BlueprintType)
struct FCellData
{
	GENERATED_BODY()

	UPROPERTY()
	ECellType Type = ECellType::None;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	EStairPart StairPart = EStairPart::None;
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
