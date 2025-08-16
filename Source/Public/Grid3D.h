#pragma once

#include "CoreMinimal.h"

// ------------------------
// Generic 3D grid
// ------------------------
template<typename T>
class FGrid3D
{
private:
	TArray<T> Data;

public:
	FIntVector Size;
	FIntVector Offset;
	
	FGrid3D() : Size(FIntVector::ZeroValue), Offset(FIntVector::ZeroValue) {}

	FGrid3D(const FIntVector& InSize, TFunction<T(const FIntVector&)> CreateObjectFunction, const FIntVector& InOffset = FIntVector::ZeroValue)
		: Size(InSize), Offset(InOffset)
	{
		const int32 TotalSize = Size.X * Size.Y * Size.Z;
		Data.Reserve(TotalSize); // Prepare the array memory

		for (int32 z = 0; z < Size.Z; ++z)
		{
			for (int32 y = 0; y < Size.Y; ++y)
			{
				for (int32 x = 0; x < Size.X; ++x)
				{
					const FIntVector CurrentPos(x, y, z);
					Data.Emplace(CreateObjectFunction(CurrentPos));
				}
			}
		}
	}
	void Clear()
	{
		Data.Empty();
		Size = FIntVector::ZeroValue;
		Offset = FIntVector::ZeroValue;
	}
	
	bool IsInitialized() { return !Data.IsEmpty(); }

	// Convert 3D coordinate to 1D index
	int GetIndex(const FIntVector& Pos) const
	{
		return Pos.X + (Size.X * Pos.Y) + (Size.X * Size.Y * Pos.Z);
	}

	// Check if a position is within the grid bounds
	bool InBounds(const FIntVector& Pos) const
	{
		FIntVector Adjusted = Pos + Offset;
		return Adjusted.X >= 0 && Adjusted.X < Size.X &&
			   Adjusted.Y >= 0 && Adjusted.Y < Size.Y &&
			   Adjusted.Z >= 0 && Adjusted.Z < Size.Z;
	}

	int32 GetGridSize()
	{
		return (Size.X * Size.Y * Size.Z);
	}

	// Access using 3 separate ints
	T& operator()(int X, int Y, int Z)
	{
		return (*this)(FIntVector(X, Y, Z));
	}

	const T& operator()(int X, int Y, int Z) const
	{
		return (*this)(FIntVector(X, Y, Z));
	}

	// Access using FIntVector
	T& operator()(const FIntVector& Pos)
	{
		FIntVector Adjusted = Pos + Offset;
		return Data[GetIndex(Adjusted)];
	}

	const T& operator()(const FIntVector& Pos) const
	{
		FIntVector Adjusted = Pos + Offset;
		return Data[GetIndex(Adjusted)];
	}
};