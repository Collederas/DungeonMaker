#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

struct FPathNode;

template<typename T>
class TMinHeap
{
public:
    // Constructor remains the same
    TMinHeap(TFunction<bool(const T&, const T&)> InCompare) : Compare(InCompare) {}

    /** Adds a new item to the heap. Assumes the item is not already present. */
    void Push(const T& Item)
    {
        Data.Add(Item);
        const int32 Index = Data.Num() - 1;
        IndexMap.Add(Item, Index);
        HeapifyUp(Index);
    }

    /** Removes and returns the top item from the heap. */
    T Pop()
    {
        check(Data.Num() > 0);
        T TopItem = Data[0];
        IndexMap.Remove(TopItem);

        if (Data.Num() > 1)
        {
            Data[0] = Data.Last();
            IndexMap[Data[0]] = 0; // Update index of the moved item
            Data.Pop();
            HeapifyDown(0);
        }
        else
        {
            Data.Pop();
        }
        return TopItem;
    }

    /**
     * Updates an item's position in the heap after its priority has changed,
     * or pushes it if it's new. This is now a fast O(log N) operation.
     */
    void Update(const T& Item)
    {
        const int32* IndexPtr = IndexMap.Find(Item);
        if (IndexPtr != nullptr)
        {
            // Item already exists, just re-sort it from its current position
            HeapifyUp(*IndexPtr);
        }
        else
        {
            // Item is new, add it normally
            Push(Item);
        }
    }

    /** Clears the heap. */
    void Clear()
    {
        Data.Empty();
        IndexMap.Empty();
    }

    int32 Num() const { return Data.Num(); }

private:
    TArray<T> Data;
    TMap<T, int32> IndexMap;
    TFunction<bool(const T&, const T&)> Compare;

    /** Swaps two items in the Data array and updates their indices in the map. */
    void SwapItems(int32 IndexA, int32 IndexB)
    {
        // Update the map to reflect the new positions
        IndexMap[Data[IndexA]] = IndexB;
        IndexMap[Data[IndexB]] = IndexA;

        // Swap the items in the data array
        Data.Swap(IndexA, IndexB);
    }

    void HeapifyUp(int32 Index)
    {
        while (Index > 0)
        {
            int32 ParentIndex = (Index - 1) / 2;
            if (!Compare(Data[Index], Data[ParentIndex]))
            {
                break;
            }
            SwapItems(Index, ParentIndex);
            Index = ParentIndex;
        }
    }

    void HeapifyDown(int32 Index)
    {
        const int32 Size = Data.Num();
        while (true)
        {
            const int32 LeftChildIndex = 2 * Index + 1;
            const int32 RightChildIndex = 2 * Index + 2;
            int32 SmallestIndex = Index;

            if (LeftChildIndex < Size && Compare(Data[LeftChildIndex], Data[SmallestIndex]))
            {
                SmallestIndex = LeftChildIndex;
            }
            if (RightChildIndex < Size && Compare(Data[RightChildIndex], Data[SmallestIndex]))
            {
                SmallestIndex = RightChildIndex;
            }

            if (SmallestIndex == Index)
            {
                break;
            }
            SwapItems(Index, SmallestIndex);
            Index = SmallestIndex;
        }
    }
};