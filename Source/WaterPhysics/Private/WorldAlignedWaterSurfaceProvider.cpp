// Copyright Mans Isaksson. All Rights Reserved.

#include "WorldAlignedWaterSurfaceProvider.h"
#include "DrawDebugHelpers.h"

namespace WorldAlignedWaterSurfaceProvider
{
	template<typename T>
	FORCEINLINE T FourWayLerp(const T& A, const T& B, const T& C, const T& D, float X, float Y)
	{
		return FMath::Lerp(FMath::Lerp(A, C, Y), FMath::Lerp(B, D, Y), X);
	}
};

void FWorldAlignedWaterSurfaceProvider::DrawDebugProvider(UWorld* World)
{
	for (const FWaterInfoSection& SectionInfo : WaterInfoSections)
	{
		// Draw fetched vertices
		int32 VertexCount = 0;
		float AccumZ = 0.f;
		for (const FWaterInfoSection::FWaterInfoVertex& WaterInfoVertex : SectionInfo.WaterInfoVertices)
		{
			if (WaterInfoVertex.bIsSet)
			{
				DrawDebugPoint(World, WaterInfoVertex.Result.WaterSurfaceLocation, 10.f, FColor::Green, false, 0.f, -1);
				VertexCount++;
				AccumZ += WaterInfoVertex.Result.WaterSurfaceLocation.Z;
			}
		}

		if (VertexCount != 0)
		{
			const FVector Location = FVector(SectionInfo.SectionLocation.X, SectionInfo.SectionLocation.Y, AccumZ / VertexCount);

			const FVector A = Location;
			const FVector B = Location + FVector(WaterInfoSection::SectionSize(), 0.f, 0.f);
			const FVector C = Location + FVector(0.f, WaterInfoSection::SectionSize(), 0.f);
			const FVector D = Location + FVector(WaterInfoSection::SectionSize(), WaterInfoSection::SectionSize(), 0.f);

			DrawDebugLine(World, A, B, FColor::Yellow, false, 0.f, -1, 5);
			DrawDebugLine(World, A, C, FColor::Yellow, false, 0.f, -1, 5);
			DrawDebugLine(World, B, D, FColor::Yellow, false, 0.f, -1, 5);
			DrawDebugLine(World, C, D, FColor::Yellow, false, 0.f, -1, 5);
		}
	}
}

void FWorldAlignedWaterSurfaceProvider::EndStepScene()
{
	// Flag sections as "not used", allowing us to deallocate them if they are not used the next frame
	auto* WaterSectionNode = WaterInfoSections.GetHead();
	while (WaterSectionNode != nullptr)
	{
		if (!WaterSectionNode->GetValue().bIsUsed)
		{
			auto* NodeToRemove = WaterSectionNode;
			WaterSectionNode = WaterSectionNode->GetNextNode();
			WaterInfoSections.RemoveNode(NodeToRemove, true);
		}
		else
		{
			WaterSectionNode->GetValue().bIsUsed = false;
			WaterSectionNode = WaterSectionNode->GetNextNode();
		}
	}
}

FWaterSurfaceProvider::FVertexWaterInfoArray FWorldAlignedWaterSurfaceProvider::CalculateVerticesWaterInfo(const WaterPhysics::FVertexList& Vertices, 
	const UActorComponent* Component, const FGetWaterInfoAtLocation& SurfaceGetter)
{
	FWaterSurfaceProvider::FVertexWaterInfoArray OutArray;
	OutArray.Reserve(Vertices.Num());
	for (const FVector& Vertex : Vertices)
	{
		OutArray.Emplace(CalculateWaterInfoAtLocation(Vertex, Component, SurfaceGetter));
	}
	return OutArray;
}

// NOTE: Inlining this function can more than double performance depending on the system we're running on
FORCEINLINE FGetWaterInfoResult FWorldAlignedWaterSurfaceProvider::CalculateWaterInfoAtLocation(const FVector &Location, 
	const UActorComponent* Component, const FGetWaterInfoAtLocation& GetWaterInfoCallable)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(CalculateWaterInfoAtLocation);

RedoFindSection:

	FWaterInfoSection* CurrentWaterSection = nullptr;
	FWaterInfoSection* UnusedWaterSection  = nullptr;

	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FindSection);

		auto* WaterSectionNode = WaterInfoSections.GetHead();
		while (WaterSectionNode != nullptr)
		{
			if (!WaterSectionNode->GetValue().bIsUsed)
			{
				UnusedWaterSection = &WaterSectionNode->GetValue();
				break; // Unused sections appear at the end of the list, no point in checking the rest
			}

			if (WaterSectionNode->GetValue().IsInSection(Location))
			{
				CurrentWaterSection = &WaterSectionNode->GetValue();
				break;
			}

			WaterSectionNode = WaterSectionNode->GetNextNode();
		}
			
		if (CurrentWaterSection == nullptr)
		{
			FScopeLock Lock(&WaterInfoCS);

			const FVector SectionLocation = FVector(
					FMath::FloorToFloat(Location.X * WaterInfoSection::InverseSectionSize()) * WaterInfoSection::SectionSize(),
					FMath::FloorToFloat(Location.Y * WaterInfoSection::InverseSectionSize()) * WaterInfoSection::SectionSize(),
					Location.Z
				);

			if (UnusedWaterSection != nullptr)
			{
				if (UnusedWaterSection->bIsUsed == true) // Someone else beat us to it, try again
					goto RedoFindSection;

				CurrentWaterSection = UnusedWaterSection;
				CurrentWaterSection->InitAtLocation(SectionLocation);
			}
			else
			{
				auto* CurrentTail = WaterInfoSections.GetTail();
				if (CurrentTail && CurrentTail->GetValue().IsInSection(Location)) // Someone else beat us to it, try again
					goto RedoFindSection;

				// IMPORTANT: FWaterInfoSection is too big to fit on the stack. Create the FWaterInfoSectionNode on the heap and
				// placement new the FWaterInfoSection into the node.
				FWaterInfoSectionNode* NewNodeMemory = (FWaterInfoSectionNode*)FMemory::MallocZeroed(sizeof(FWaterInfoSectionNode));
				new (&NewNodeMemory->GetValue()) FWaterInfoSection(SectionLocation);
				WaterInfoSections.AddTail(NewNodeMemory);

				CurrentWaterSection = &WaterInfoSections.GetTail()->GetValue();
			}
		}
	}

	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(CalculateCellInfoAtLocation);

		const FWaterInfoSection::FWaterInfoCell WaterInfoCell = CurrentWaterSection->CalculateCellInfoAtLocation(Location, Component, GetWaterInfoCallable);

		FGetWaterInfoResult OutResult;
		{
			OutResult.WaterSurfaceLocation = WorldAlignedWaterSurfaceProvider::FourWayLerp(
				WaterInfoCell.A->Result.WaterSurfaceLocation, 
				WaterInfoCell.B->Result.WaterSurfaceLocation,
				WaterInfoCell.C->Result.WaterSurfaceLocation,
				WaterInfoCell.D->Result.WaterSurfaceLocation,
				WaterInfoCell.AlphaX,
				WaterInfoCell.AlphaY
			);

			OutResult.WaterSurfaceNormal = WorldAlignedWaterSurfaceProvider::FourWayLerp(
				WaterInfoCell.A->Result.WaterSurfaceNormal, 
				WaterInfoCell.B->Result.WaterSurfaceNormal,
				WaterInfoCell.C->Result.WaterSurfaceNormal,
				WaterInfoCell.D->Result.WaterSurfaceNormal,
				WaterInfoCell.AlphaX,
				WaterInfoCell.AlphaY
			).GetSafeNormal();

			OutResult.WaterVelocity = WorldAlignedWaterSurfaceProvider::FourWayLerp(
				WaterInfoCell.A->Result.WaterVelocity, 
				WaterInfoCell.B->Result.WaterVelocity,
				WaterInfoCell.C->Result.WaterVelocity,
				WaterInfoCell.D->Result.WaterVelocity,
				WaterInfoCell.AlphaX,
				WaterInfoCell.AlphaY
			);
		}

		return OutResult;
	}
};