#include "SWPolicyClient.h"
#include "SymbioticWorld.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "AddressInfoTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformTime.h"
#include "Containers/StringConv.h"

FSWPolicyClient::~FSWPolicyClient()
{
	Shutdown();
}

int32 FSWPolicyClient::Configure(const FString& Spec, int32 InTimeoutMs)
{
	Shutdown();
	Servers.Reset();
	TimeoutMs = FMath::Clamp(InTimeoutMs, 1, 60000);

	TArray<FString> Entries;
	Spec.ParseIntoArray(Entries, TEXT("|"), true);
	for (FString Entry : Entries)
	{
		Entry.TrimStartAndEndInline();
		if (Entry.IsEmpty()) continue;
		FString HostPort, SpeciesStr;
		if (!Entry.Split(TEXT("="), &HostPort, &SpeciesStr))
		{
			UE_LOG(LogSymbioticWorld, Warning, TEXT("SWPolicy: entry '%s' needs host:port=Lumen|Tecton|Both"), *Entry);
			continue;
		}
		FString Host, PortStr;
		if (!HostPort.Split(TEXT(":"), &Host, &PortStr, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			UE_LOG(LogSymbioticWorld, Warning, TEXT("SWPolicy: entry '%s' needs host:port"), *Entry);
			continue;
		}
		FSWPolicyServer S;
		S.Host = Host.TrimStartAndEnd();
		S.Port = FCString::Atoi(*PortStr);
		S.Name = FString::Printf(TEXT("%s:%d"), *S.Host, S.Port);
		SpeciesStr = SpeciesStr.TrimStartAndEnd().ToLower();
		S.bLumen = SpeciesStr == TEXT("lumen") || SpeciesStr == TEXT("both");
		S.bTecton = SpeciesStr == TEXT("tecton") || SpeciesStr == TEXT("both");
		if (S.Port <= 0 || (!S.bLumen && !S.bTecton))
		{
			UE_LOG(LogSymbioticWorld, Warning, TEXT("SWPolicy: bad entry '%s' (port %d, species '%s')"), *Entry, S.Port, *SpeciesStr);
			continue;
		}
		S.NextConnectAttempt = 0.0;
		S.LastReportTime = FPlatformTime::Seconds();
		Servers.Add(S);
		UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: configured for %s (timeout %d ms)"), *S.Name,
			S.bLumen && S.bTecton ? TEXT("Lumen+Tecton") : (S.bLumen ? TEXT("Lumen") : TEXT("Tecton")), TimeoutMs);
	}
	return Servers.Num();
}

void FSWPolicyClient::Shutdown()
{
	for (FSWPolicyServer& S : Servers)
	{
		if (S.Socket)
		{
			UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: closing. %d requests, %d replies, %d timeouts, %d stale, mean round trip %.2f ms"),
				*S.Name, S.Requests, S.Replies, S.Timeouts, S.Stale, S.MeanRoundTripMs());
			Disconnect(S, TEXT("shutdown"));
		}
	}
}

void FSWPolicyClient::ServersFor(ESWSpecies Sp, TArray<int32>& Out) const
{
	Out.Reset();
	for (int32 i = 0; i < Servers.Num(); ++i)
	{
		if (Servers[i].Controls(Sp)) Out.Add(i);
	}
}

int32 FSWPolicyClient::NumConnected() const
{
	int32 N = 0;
	for (const FSWPolicyServer& S : Servers) if (S.bConnected) N++;
	return N;
}

void FSWPolicyClient::SetHello(const FString& InHelloLine)
{
	HelloLine = InHelloLine;
	for (FSWPolicyServer& S : Servers)
	{
		if (S.bConnected && SendLine(S, HelloLine))
		{
			UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: hello sent (%d bytes)"), *S.Name, HelloLine.Len());
		}
	}
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

bool FSWPolicyClient::TryConnect(FSWPolicyServer& S)
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) return false;

	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	bool bValidIp = false;
	Addr->SetIp(*S.Host, bValidIp);
	if (!bValidIp)
	{
		FAddressInfoResult R = SS->GetAddressInfo(*S.Host, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Streaming);
		if (R.ReturnCode != SE_NO_ERROR || R.Results.Num() == 0)
		{
			if (!S.bEverConnected && S.Requests == 0 && S.NextConnectAttempt == 0.0)
			{
				UE_LOG(LogSymbioticWorld, Warning, TEXT("PolicyServer %s: cannot resolve host"), *S.Name);
			}
			return false;
		}
		Addr = R.Results[0].Address;
	}
	Addr->SetPort(S.Port);

	FSocket* Sock = SS->CreateSocket(NAME_Stream, TEXT("SWPolicy"), Addr->GetProtocolType());
	if (!Sock) return false;
	Sock->SetNonBlocking(true);
	Sock->Connect(*Addr);   // non-blocking: completes (or fails) during the Wait below, bounded by the timeout
	const bool bWritable = Sock->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(TimeoutMs));
	if (!bWritable || Sock->GetConnectionState() == SCS_ConnectionError)
	{
		Sock->Close();
		SS->DestroySocket(Sock);
		return false;
	}
	Sock->SetNonBlocking(false);
	Sock->SetNoDelay(true);
	S.Socket = Sock;
	S.bConnected = true;
	S.bEverConnected = true;
	S.bTimingOut = false;
	S.RecvBuf.Reset();
	UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: connected (%s)"), *S.Name, *Addr->ToString(true));
	if (!HelloLine.IsEmpty() && SendLine(S, HelloLine))
	{
		UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: hello sent (%d bytes)"), *S.Name, HelloLine.Len());
	}
	return true;
}

void FSWPolicyClient::Disconnect(FSWPolicyServer& S, const TCHAR* Reason)
{
	if (S.Socket)
	{
		S.Socket->Close();
		if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) SS->DestroySocket(S.Socket);
		S.Socket = nullptr;
	}
	if (S.bConnected)
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("PolicyServer %s: disconnected (%s); its organisms use the built-in bandit until it is back (retry every %.0f s)"),
			*S.Name, Reason, ReconnectSeconds);
	}
	S.bConnected = false;
	S.bAwaitingReply = false;
	S.RecvBuf.Reset();
	S.NextConnectAttempt = FPlatformTime::Seconds() + ReconnectSeconds;
}

void FSWPolicyClient::Tick()
{
	const double Now = FPlatformTime::Seconds();
	for (FSWPolicyServer& S : Servers)
	{
		if (!S.bConnected)
		{
			if (Now >= S.NextConnectAttempt)
			{
				if (!TryConnect(S))
				{
					if (S.NextConnectAttempt == 0.0)
					{
						UE_LOG(LogSymbioticWorld, Warning, TEXT("PolicyServer %s: not reachable; its organisms use the built-in bandit (retry every %.0f s)"), *S.Name, ReconnectSeconds);
					}
					S.NextConnectAttempt = Now + ReconnectSeconds;
				}
			}
			continue;
		}
		DrainSideMessages(S);
		if (Now - S.LastReportTime >= ReportSeconds && S.Requests != S.RequestsAtLastReport)
		{
			UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: %d requests, %d replies, %d timeouts, %d stale, mean round trip %.2f ms"),
				*S.Name, S.Requests, S.Replies, S.Timeouts, S.Stale, S.MeanRoundTripMs());
			S.LastReportTime = Now;
			S.RequestsAtLastReport = S.Requests;
		}
	}
}

// ---------------------------------------------------------------------------
// Line transport
// ---------------------------------------------------------------------------

bool FSWPolicyClient::SendLine(FSWPolicyServer& S, const FString& Line)
{
	if (!S.Socket || !S.bConnected) return false;
	FTCHARToUTF8 Conv(*Line);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
	Bytes.Add('\n');
	int32 Offset = 0;
	while (Offset < Bytes.Num())
	{
		int32 Sent = 0;
		if (!S.Socket->Send(Bytes.GetData() + Offset, Bytes.Num() - Offset, Sent) || Sent <= 0)
		{
			Disconnect(S, TEXT("send failed"));
			return false;
		}
		Offset += Sent;
	}
	return true;
}

bool FSWPolicyClient::ReadLine(FSWPolicyServer& S, double DeadlineWall, FString& OutLine)
{
	if (!S.Socket || !S.bConnected) return false;
	uint8 Tmp[65536];
	for (;;)
	{
		const int32 NL = S.RecvBuf.IndexOfByKey(static_cast<uint8>('\n'));
		if (NL != INDEX_NONE)
		{
			FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(S.RecvBuf.GetData()), NL);
			OutLine = FString(Conv.Length(), Conv.Get());
			OutLine.TrimStartAndEndInline();
			S.RecvBuf.RemoveAt(0, NL + 1, EAllowShrinking::No);
			return true;
		}
		bool bReadable = false;
		if (DeadlineWall > 0.0)
		{
			const double Remaining = DeadlineWall - FPlatformTime::Seconds();
			if (Remaining <= 0.0) return false;
			bReadable = S.Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(Remaining));
		}
		else
		{
			uint32 Pending = 0;
			bReadable = S.Socket->HasPendingData(Pending) && Pending > 0;
		}
		if (!bReadable) return false;
		int32 Read = 0;
		if (!S.Socket->Recv(Tmp, sizeof(Tmp), Read) || Read <= 0)
		{
			Disconnect(S, TEXT("connection closed by server"));
			return false;
		}
		S.RecvBuf.Append(Tmp, Read);
	}
}

bool FSWPolicyClient::HandleSideMessage(FSWPolicyServer& S, const TSharedPtr<FJsonObject>& Msg)
{
	if (!Msg.IsValid()) return true;
	const FString Type = Msg->GetStringField(TEXT("type"));
	if (Type == TEXT("log"))
	{
		UE_LOG(LogSymbioticWorld, Log, TEXT("[%s] %s"), *S.Name, *Msg->GetStringField(TEXT("text")));
		return true;
	}
	if (Type == TEXT("actions"))
	{
		S.Stale++;   // an actions reply outside (or after) its exchange window
		return true;
	}
	return true;
}

void FSWPolicyClient::DrainSideMessages(FSWPolicyServer& S)
{
	FString Line;
	int32 Guard = 0;
	while (Guard++ < 1000 && ReadLine(S, 0.0, Line))
	{
		TSharedPtr<FJsonObject> Msg;
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Line);
		if (FJsonSerializer::Deserialize(Reader, Msg)) HandleSideMessage(S, Msg);
	}
}

// ---------------------------------------------------------------------------
// Decide / actions exchange
// ---------------------------------------------------------------------------

bool FSWPolicyClient::ParseAction(const TSharedPtr<FJsonValue>& V, int32& OutIdx)
{
	if (!V.IsValid()) return false;
	if (V->Type == EJson::Number)
	{
		OutIdx = FMath::RoundToInt(static_cast<float>(V->AsNumber()));
		return OutIdx >= 0 && OutIdx < SW_NUM_ACTIONS;
	}
	if (V->Type == EJson::String)
	{
		const FString Str = V->AsString().TrimStartAndEnd().ToLower();
		if (Str.IsNumeric())
		{
			OutIdx = FCString::Atoi(*Str);
			return OutIdx >= 0 && OutIdx < SW_NUM_ACTIONS;
		}
		for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
		{
			if (Str == SWActionName(static_cast<ESWAction>(A))) { OutIdx = A; return true; }
		}
	}
	return false;
}

void FSWPolicyClient::Exchange(int32 Step, const TArray<FString>& RequestLines, TArray<TMap<int32, int32>>& OutActions)
{
	OutActions.SetNum(Servers.Num());
	for (TMap<int32, int32>& M : OutActions) M.Reset();

	// Phase A: send every request first so the servers work in parallel.
	for (int32 i = 0; i < Servers.Num(); ++i)
	{
		FSWPolicyServer& S = Servers[i];
		S.bAwaitingReply = false;
		if (!S.bConnected || !RequestLines.IsValidIndex(i) || RequestLines[i].IsEmpty()) continue;
		DrainSideMessages(S);
		if (!S.bConnected) continue;
		S.SendTime = FPlatformTime::Seconds();
		if (!SendLine(S, RequestLines[i])) continue;
		S.Requests++;
		S.bAwaitingReply = true;
	}

	// Phase B: collect replies; one shared deadline so the substep never blocks longer than the timeout.
	const double Deadline = FPlatformTime::Seconds() + TimeoutMs / 1000.0;
	for (int32 i = 0; i < Servers.Num(); ++i)
	{
		FSWPolicyServer& S = Servers[i];
		if (!S.bAwaitingReply) continue;
		S.bAwaitingReply = false;
		bool bGot = false;
		FString Line;
		while (!bGot && ReadLine(S, Deadline, Line))
		{
			TSharedPtr<FJsonObject> Msg;
			TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Line);
			if (!FJsonSerializer::Deserialize(Reader, Msg) || !Msg.IsValid()) continue;
			if (Msg->GetStringField(TEXT("type")) != TEXT("actions")) { HandleSideMessage(S, Msg); continue; }
			// A reply that echoes "step" must match this step; older ones are stale (late replies after a timeout).
			double EchoStep = 0.0;
			if (Msg->TryGetNumberField(TEXT("step"), EchoStep) && FMath::RoundToInt(static_cast<float>(EchoStep)) != Step)
			{
				S.Stale++;
				continue;
			}
			const TSharedPtr<FJsonObject>* Actions = nullptr;
			if (Msg->TryGetObjectField(TEXT("actions"), Actions) && Actions && Actions->IsValid())
			{
				for (const auto& KV : (*Actions)->Values)
				{
					int32 Idx = -1;
					if (KV.Key.IsNumeric() && ParseAction(KV.Value, Idx))
					{
						OutActions[i].Add(FCString::Atoi(*KV.Key), Idx);
					}
				}
			}
			bGot = true;
			S.Replies++;
			S.RoundTripMsSum += (FPlatformTime::Seconds() - S.SendTime) * 1000.0;
			if (S.Replies == 1)
			{
				UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: first reply, %d actions, %.2f ms"), *S.Name, OutActions[i].Num(),
					(FPlatformTime::Seconds() - S.SendTime) * 1000.0);
			}
		}
		if (!S.bConnected) continue;   // dropped mid-exchange; Disconnect() already logged
		if (!bGot)
		{
			S.Timeouts++;
			if (!S.bTimingOut)
			{
				S.bTimingOut = true;
				UE_LOG(LogSymbioticWorld, Warning, TEXT("PolicyServer %s: no reply within %d ms; its organisms use the built-in bandit until it responds again"), *S.Name, TimeoutMs);
			}
		}
		else if (S.bTimingOut)
		{
			S.bTimingOut = false;
			UE_LOG(LogSymbioticWorld, Log, TEXT("PolicyServer %s: responding again (%d timeouts so far)"), *S.Name, S.Timeouts);
		}
	}
}
