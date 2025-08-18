#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "DungeonTypes.generated.h"


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
