// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "WaterPhysicsScene.h"

namespace WaterInfoSection
{
	static constexpr float CellSize()           { return 200.f; }
	static constexpr int32 CellCount()          { return 100; }
	static constexpr float InverseCellSize()    { return 1 / CellSize(); }
	static constexpr int32 VertexRowCount()     { return CellCount() + 1; }
	static constexpr int32 VertexCount()        { return VertexRowCount() * VertexRowCount(); }
	static constexpr float SectionSize()        { return CellSize() * CellCount(); }
	static constexpr float InverseSectionSize() { return 1.f / SectionSize(); }
};

/*
	Structure for storing and fetching cached results from GetWaterInfoCallable
	In order to maintain thread safty and the ability to support any sized world we split the information into multiple blocks,
	the size of which is define by CellSize and CellCount. We then store each block in a linked list, allocating the blocks as we go.

	This algorithm samples points with a set distance defined by CellSize, and then interpolates between the results as follows:
	A________B
	|        |
	|  .x,y  |
	|________|
	C        D

	Lerp(Lerp(A, C, y), Lerp(B, D, y), x)
*/
struct FWorldAlignedWaterSurfaceProvider : public FWaterSurfaceProvider
{
private:
	struct FWaterInfoSection
	{
		struct FWaterInfoVertex
		{
			bool bIsSet;
			FGetWaterInfoResult Result;
		};
		struct FWaterInfoCell
		{
			FWaterInfoVertex* A;
			FWaterInfoVertex* B;
			FWaterInfoVertex* C;
			FWaterInfoVertex* D;
			float AlphaX;
			float AlphaY;
		};

		bool bIsUsed;
		FVector SectionLocation;
		FWaterInfoVertex WaterInfoVertices[WaterInfoSection::VertexCount()];
		FCriticalSection CriticalSections[WaterInfoSection::VertexCount()];

		FWaterInfoSection() = default;

		FWaterInfoSection(const FWaterInfoSection& InWaterInfoSection)
			: bIsUsed(InWaterInfoSection.bIsUsed)
			, SectionLocation(InWaterInfoSection.SectionLocation)
		{
			FMemory::Memcpy((void*)&WaterInfoVertices[0], (void*)&InWaterInfoSection.WaterInfoVertices[0], WaterInfoSection::VertexCount() * sizeof(WaterInfoVertices[0]));
		}

		FWaterInfoSection(const FVector& InSectionLocation)
		{
			InitAtLocation(InSectionLocation);
		}

		void InitAtLocation(const FVector& InSectionLocation)
		{
			bIsUsed = true;
			SectionLocation = InSectionLocation;
			FMemory::Memzero((void*)&WaterInfoVertices[0], WaterInfoSection::VertexCount() * sizeof(WaterInfoVertices[0]));
		}

		FORCEINLINE int32 FlattenVertexIndex(int32 X, int32 Y) const { return (X + Y * WaterInfoSection::VertexRowCount()); }

		FORCEINLINE FWaterInfoVertex* CalculateVertexInfoForIndex(int32 X, int32 Y, 
			const UActorComponent* Component, const FGetWaterInfoAtLocation& WaterInfoGetter)
		{
			const int32 Index = FlattenVertexIndex(X, Y);
			FWaterInfoVertex* OutVertexInfo = &WaterInfoVertices[Index];
			if (!OutVertexInfo->bIsSet)
			{
				FScopeLock Lock(&CriticalSections[Index]);
				if (!OutVertexInfo->bIsSet) // Make sure no one else beat us to it
				{
					const FVector VertexLocation = SectionLocation + FVector(X * WaterInfoSection::CellSize(), Y * WaterInfoSection::CellSize(), 0.f);
					OutVertexInfo->Result = WaterInfoGetter.Execute(Component, VertexLocation);
					OutVertexInfo->bIsSet = true;
				}
			}
			return OutVertexInfo;
		}

		FORCEINLINE FWaterInfoCell CalculateCellInfoAtLocation(const FVector& InLocation, 
			const UActorComponent* Component, const FGetWaterInfoAtLocation& WaterInfoGetter)
		{
			const FVector RelativeLocation = InLocation - SectionLocation;
			// The Clamp is here since due to float inaccuracy we can get (RelativeLocation / CellSize = CellCount) 
			// which should not be possible since IsInSection excludes all positions which are equal to SectionSize.
			const int32 X = FMath::Clamp(FMath::TruncToInt(RelativeLocation.X * WaterInfoSection::InverseCellSize()), 0, WaterInfoSection::CellCount() - 1); 
			const int32 Y = FMath::Clamp(FMath::TruncToInt(RelativeLocation.Y * WaterInfoSection::InverseCellSize()), 0, WaterInfoSection::CellCount() - 1);
			check(X >= 0 && X < WaterInfoSection::CellCount());
			check(Y >= 0 && Y < WaterInfoSection::CellCount());
			
			FWaterInfoCell OutCell;

			OutCell.A = CalculateVertexInfoForIndex(X,   Y,   Component, WaterInfoGetter);
			OutCell.B = CalculateVertexInfoForIndex(X+1, Y,   Component, WaterInfoGetter);
			OutCell.C = CalculateVertexInfoForIndex(X,   Y+1, Component, WaterInfoGetter);
			OutCell.D = CalculateVertexInfoForIndex(X+1, Y+1, Component, WaterInfoGetter);
			OutCell.AlphaX = RelativeLocation.X * WaterInfoSection::InverseSectionSize();
			OutCell.AlphaY = RelativeLocation.Y * WaterInfoSection::InverseSectionSize();

			return OutCell; 
		}

		FORCEINLINE void FlaggIsNotUsed() { bIsUsed = false; }

		FORCEINLINE bool IsInSection(const FVector& InLocation) const
		{
			const FVector RelativeLocation = InLocation - SectionLocation;
			return bIsUsed
				&& RelativeLocation.X >= 0.f && RelativeLocation.X < WaterInfoSection::SectionSize()
				&& RelativeLocation.Y >= 0.f && RelativeLocation.Y < WaterInfoSection::SectionSize();
		}
	};
	TDoubleLinkedList<FWaterInfoSection> WaterInfoSections;

	typedef TDoubleLinkedList<FWaterInfoSection>::TDoubleLinkedListNode FWaterInfoSectionNode;

	FCriticalSection WaterInfoCS;

public:
	virtual void DrawDebugProvider(UWorld* World) override;
	virtual void EndStepScene() override;
	virtual FVertexWaterInfoArray CalculateVerticesWaterInfo(const WaterPhysics::FVertexList& Vertices, 
		const UActorComponent* Component, const FGetWaterInfoAtLocation& SurfaceGetter) override;
	virtual bool SupportsParallelExecution() const override { return true; }

	FGetWaterInfoResult CalculateWaterInfoAtLocation(const FVector& Location, const UActorComponent* Component, const FGetWaterInfoAtLocation& GetWaterInfoCallable);
};