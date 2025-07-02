// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#ifndef WITH_DEBUG_FORCE_CAPTURE
#define WITH_DEBUG_FORCE_CAPTURE 0
#endif

#if WITH_DEBUG_FORCE_CAPTURE

namespace WaterPhysicsDebug
{
	struct FScopedEventData
	{
		float Scale;
		FScopedEventData(const FString& ObjectName, const FName& Category, float InScale = 1.f);
		~FScopedEventData();
	};

	void BeginSession(const TArray<FString>& Args);

	void EndSession();

	void CaptureString(const FString& Name, const FString& Value);

	template<typename T>
	void CaptureNumber(const FString& Name, T Value);

	void CaptureStruct(const FString& Name, UStruct* StructClass, const void* Value);

	extern struct FFileWriter* CurrentSessionWriter;
}

#define RUN_CAPTURE_FUNC(Expression) if (WaterPhysicsDebug::CurrentSessionWriter != nullptr) { Expression; }

#define SCOPED_OBJECT_DATA_CAPTURE(ObjectName, Category, ...) WaterPhysicsDebug::FScopedEventData PREPROCESSOR_JOIN(ScopedWaterPhysicsEventData, __LINE__)(ObjectName, Category, ##__VA_ARGS__)
#define DEBUG_CAPTURE_STRING(Name, Value) RUN_CAPTURE_FUNC(WaterPhysicsDebug::CaptureString(Name, Value))
#define DEBUG_CAPTURE_NUMBER(Name, Value) RUN_CAPTURE_FUNC(WaterPhysicsDebug::CaptureNumber(Name, Value))
#define DEBUG_CAPTURE_USTRUCT(Name, Value) RUN_CAPTURE_FUNC(WaterPhysicsDebug::CaptureStruct(Name, TRemoveReference<decltype(Value)>::Type::StaticStruct(), (void*)&Value))

#else

#define SCOPED_OBJECT_DATA_CAPTURE(ObjectName, Category, ...)
#define DEBUG_CAPTURE_STRING(Name, Value)
#define DEBUG_CAPTURE_NUMBER(Name, Value)
#define DEBUG_CAPTURE_USTRUCT(Name, Value)

#endif