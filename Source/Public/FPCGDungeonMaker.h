#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "Pathfinder3D.h"
#include "PCGSettings.h"

#include "FPCGDungeonMaker.generated.h"


UCLASS(MinimalAPI, BlueprintType, ClassGroup=(Procedural))
class UDungeonPCGSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DungeonLayoutGenerator")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("DungeonPCG", "NodeTitle", "Dungeon Layout Generator"); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
	//~End UPCGSettings interface

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "10"))
	FIntVector GridSize = FIntVector(20, 20, 3);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Grid", meta = (ClampMin = "1"))
	float GridUnitSize = 400.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	int32 NumberOfRooms = 5;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms")
	FIntPoint RoomSizeMinMax = FIntPoint(4, 10);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "1"))
	FIntPoint RoomHeightInFloorsMinMax = FIntPoint(1, 2);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Dungeon Settings | Rooms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExtraConnectionChance = 0.2f;
};

class FDungeonPCGElement : public IPCGElement
{
protected:
	static bool IsCellConnected(const FGrid3D<ECellType>& Grid, FIntVector CellCoord);
	static ECellType GetCell(const FGrid3D<ECellType>& Grid, int32 x, int32 y, int32 z);
	static ECellType GetCellTypeAt(const FGrid3D<FCellData>& Grid, int32 x, int32 y, int32 z);

	static void GeneratePCGAttributes(const FGrid3D<FCellData>& Grid, FPCGContext* Context, const UDungeonPCGSettings* Settings);

	virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* Settings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
