// Copyright Mans Isaksson. All Rights Reserved.

#include "WaterPhysicsDataProfiler.h"

#if WITH_DEBUG_FORCE_CAPTURE
#include "Misc/OutputDeviceFile.h"
#include "GameDelegates.h"
#include "WaterPhysicsModule.h"
#include "JsonObjectConverter.h"

// Chrome tracer format for visual debugging https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
/*
{
  "name": "myName",			// Display Name
  "cat": "category,list",	// Category used to filter events
  "ph": "X",				// Event type, e.g. X is complete event, B Begining, and E end (Consult Complete Events in documentation for more information)
  "ts": 123,				// Time Stamp, in microseconds
  "dur": 234,				// Duration of event, only applicable in "ph": "X"
  "pid": 2343,				// Process Id
  "tid": 2347,				// Thread Id
  "args": {					// Any additional args, displayed in the profiler when viewing the event
    "someArg": 1,
    "anotherArg": {
      "value": "my value"
    }
  }
}
*/
namespace WaterPhysicsDebug
{
	struct FFileWriter
	{
		FAsyncWriter*      AsyncWriter;
		FArchive*          WriterArchive;
		FThreadSafeCounter WrittenBytes;

		FFileWriter()
			: WrittenBytes(0)
		{
			const FString FilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Profiling"), TEXT("WaterPhysicsProfile.json"));
			WriterArchive = IFileManager::Get().CreateFileWriter(*FilePath, FILEWRITE_Silent | FILEWRITE_AllowRead);
			AsyncWriter   = new FAsyncWriter(*WriterArchive);

			static uint8 UTF8BOM[3] = { 0xEF, 0xBB, 0xBF };
			AsyncWriter->Serialize(UTF8BOM, sizeof(UTF8BOM)); // Write Byte Order Mark To Archive
		}

		~FFileWriter()
		{
			delete AsyncWriter;
			AsyncWriter = nullptr;
			delete WriterArchive;
			WriterArchive = nullptr;
		}

		void Serialize(const FString& OutString)
		{
			// NOTE: AsyncWriter->Serialize is thread safe.
			FTCHARToUTF8 StringConverter(*OutString);
			const int32 NrBytes = FCString::Strlen(*OutString) * sizeof(ANSICHAR);
			WrittenBytes.Add(NrBytes);
			AsyncWriter->Serialize((uint8*)StringConverter.Get(), NrBytes);
		}
	};

	FFileWriter*  CurrentSessionWriter = nullptr;
	TArray<FName> WhitelistedCategories;
	int64         Time = 0;

	struct FThreadEventData
	{
		struct FEventTraceData
		{
			int64 Ts;
			TSharedPtr<FJsonObject> JsonData;
		};
		TArray<FEventTraceData> JsonObjectStack;

		void PushEvent(const FString& ObjectName, const FName& Category)
		{
			Time += 1; // Offset for better visualization in the profiler
			TSharedPtr<FJsonObject> NewObject = MakeShared<FJsonObject>();
			NewObject->SetStringField(TEXT("name"), ObjectName);
			NewObject->SetStringField(TEXT("cat"), Category.ToString());
			NewObject->SetStringField(TEXT("ph"), "X");
			NewObject->SetNumberField(TEXT("pid"), FGenericPlatformProcess::GetCurrentProcessId());
			NewObject->SetNumberField(TEXT("tid"), FPlatformTLS::GetCurrentThreadId());
			NewObject->SetNumberField(TEXT("ts"), Time);
			NewObject->SetObjectField(TEXT("args"), MakeShared<FJsonObject>());
			JsonObjectStack.Add({ Time, NewObject });
		}

		void PopEvent(float BlockSizeMultiplier)
		{
			const FEventTraceData TraceData = JsonObjectStack.Pop();

			if (WhitelistedCategories.Num() != 0)
			{
				const FName Category = *TraceData.JsonData->GetStringField(TEXT("cat"));
				if (!WhitelistedCategories.Contains(Category))
					return;
			}

			int64 BlockSize = FMath::Max<int64>(10, FMath::Max<int64>(100, Time - TraceData.Ts) * BlockSizeMultiplier);
			TraceData.JsonData->SetNumberField(TEXT("dur"), BlockSize);

			Time = FMath::Max(Time, TraceData.Ts + BlockSize);

			if (JsonObjectStack.Num() == 0) // Add a nice gap in profiler view
				Time += BlockSize / 3;

			FString OutputString;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
			FJsonSerializer::Serialize(TraceData.JsonData.ToSharedRef(), Writer);
			OutputString.InsertAt(0, TEXT(","));

			if (CurrentSessionWriter->WrittenBytes.GetValue() == 1) // Don't add "," first time around
			{
				static FCriticalSection AddSeperatorMutex;
				AddSeperatorMutex.Lock();
				if (CurrentSessionWriter->WrittenBytes.GetValue() == 1) // Make sure no-one else wrote while we were acquiring mutex
				{
					OutputString.RemoveAt(0, 1);
					CurrentSessionWriter->Serialize(OutputString);
					AddSeperatorMutex.Unlock();
				}
				else
				{
					AddSeperatorMutex.Unlock();
					CurrentSessionWriter->Serialize(OutputString);
				}
			}
			else
			{
				CurrentSessionWriter->Serialize(OutputString);
			}
		}

		TSharedPtr<FJsonObject>& GetCurrentEventJson() { return JsonObjectStack.Last().JsonData; }
	};

	struct FThreadEventDataContainer
	{
		int32 ThreadId;
		FThreadEventData EventData;
	};
	TDoubleLinkedList<FThreadEventDataContainer> ThreadEventData;

	FThreadEventData& GetThreadEventData()
	{
		static FCriticalSection Mutex;
		Mutex.Lock();

		FThreadEventData* OutEventData = nullptr;
		const int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

		auto* EventDataNode = ThreadEventData.GetHead();
		while (EventDataNode != nullptr)
		{
			if (EventDataNode->GetValue().ThreadId == CurrentThreadId)
			{
				OutEventData = &EventDataNode->GetValue().EventData;
				break;
			}

			EventDataNode = EventDataNode->GetNextNode();
		}

		if (OutEventData == nullptr)
		{
			ThreadEventData.AddTail(FThreadEventDataContainer{ CurrentThreadId, FThreadEventData() });
			OutEventData = &ThreadEventData.GetTail()->GetValue().EventData;
		}
		Mutex.Unlock();

		return *OutEventData;
	}

	// Right now we assume the begin and end session events fire at the end of the game tick, at which point no thread should be working.
	// This might change in the future, in which case we need to make this thread safe.
	FDelegateHandle ExitCommandDelegateHandle;
	FDelegateHandle EndPlayMapDelegateHandle;
}

void WaterPhysicsDebug::BeginSession(const TArray<FString>& Args)
{
	check(CurrentSessionWriter == nullptr);
	CurrentSessionWriter = new FFileWriter();
	CurrentSessionWriter->Serialize(TEXT("["));

	Time = 0;

	WhitelistedCategories.Empty(Args.Num());
	for (const FString Arg : Args)
		WhitelistedCategories.Add(*Arg);

	ExitCommandDelegateHandle = FGameDelegates::Get().GetExitCommandDelegate().AddLambda([]()
	{
		EndSession();
	});

	EndPlayMapDelegateHandle = FGameDelegates::Get().GetEndPlayMapDelegate().AddLambda([]()
	{
		EndSession();
	});
}

void WaterPhysicsDebug::EndSession()
{
	if (CurrentSessionWriter)
	{
		UE_LOG(LogWaterPhysics, Log, TEXT("Ended Physics Data Capture Session"));

		FGameDelegates::Get().GetExitCommandDelegate().Remove(ExitCommandDelegateHandle);
		FGameDelegates::Get().GetEndPlayMapDelegate().Remove(EndPlayMapDelegateHandle);

		ThreadEventData.Empty(); // Clear out thread data information

		CurrentSessionWriter->Serialize(TEXT("]")); // Write JSON session terminator
		CurrentSessionWriter->AsyncWriter->Flush();

		delete CurrentSessionWriter;
		CurrentSessionWriter = nullptr;

		WhitelistedCategories.Empty();
	}
}


WaterPhysicsDebug::FScopedEventData::FScopedEventData(const FString& ObjectName, const FName& Category, float InScale)
	: Scale(InScale)
{
	if (CurrentSessionWriter != nullptr)
	{
		GetThreadEventData().PushEvent(ObjectName, Category);
	}
}

WaterPhysicsDebug::FScopedEventData::~FScopedEventData() 
{
	if (CurrentSessionWriter != nullptr)
	{
		GetThreadEventData().PopEvent(Scale);
	}
}

void WaterPhysicsDebug::CaptureString(const FString& Name, const FString& Value)
{
	check(CurrentSessionWriter != nullptr);
	GetThreadEventData().GetCurrentEventJson()->GetObjectField(TEXT("args"))->SetStringField(Name, Value);
}

template<typename T>
void WaterPhysicsDebug::CaptureNumber(const FString& Name, T Value)
{
	check(CurrentSessionWriter != nullptr);
	GetThreadEventData().GetCurrentEventJson()->GetObjectField(TEXT("args"))->SetNumberField(Name, Value);
}

template void WaterPhysicsDebug::CaptureNumber<int32>(const FString& Name, int32 Value);
template void WaterPhysicsDebug::CaptureNumber<uint8>(const FString& Name, uint8 Value);
template void WaterPhysicsDebug::CaptureNumber<uint32>(const FString& Name, uint32 Value);
template void WaterPhysicsDebug::CaptureNumber<int64>(const FString& Name, int64 Value);
template void WaterPhysicsDebug::CaptureNumber<uint64>(const FString& Name, uint64 Value);
template void WaterPhysicsDebug::CaptureNumber<float>(const FString& Name, float Value);
template void WaterPhysicsDebug::CaptureNumber<double>(const FString& Name, double Value);

void WaterPhysicsDebug::CaptureStruct(const FString& Name, UStruct* StructClass, const void* Value)
{
	check(CurrentSessionWriter != nullptr);

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (FJsonObjectConverter::UStructToJsonObject(StructClass, Value, JsonObject, 0, 0))
		GetThreadEventData().GetCurrentEventJson()->GetObjectField(TEXT("args"))->SetObjectField(Name, JsonObject);
}

static FAutoConsoleCommand WaterPhysicsBeginDataCaptureCmd(
	TEXT("BeginWaterPhysicsDataCapture"),
	TEXT("Starts a \"Unreal Water Physics Plugin\" data capture profiling session."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&WaterPhysicsDebug::BeginSession)
);

static FAutoConsoleCommand WaterPhysicsEndDataCaptureCmd(
	TEXT("EndWaterPhysicsDataCapture"),
	TEXT("Ends the \"Unreal Water Physics Plugin\" data capture profiling session."),
	FConsoleCommandDelegate::CreateStatic(&WaterPhysicsDebug::EndSession)
);

#endif
