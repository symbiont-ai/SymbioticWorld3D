#pragma once

#include "CoreMinimal.h"
#include "SWTypes.h"

class FSocket;
class FJsonObject;

// ---------------------------------------------------------------------------
// FSWPolicyClient: the sim side of the external policy protocol (docs/POLICY_API.md).
//
// One TCP connection per server, newline-delimited UTF-8 JSON. The sim is the
// client; a collaborator's Python process (Tools/policy_server.py) is the server.
// Per logical substep the world manager sends at most ONE "decide" request per
// server carrying every organism assigned to it whose decision is due, then
// blocks for at most TimeoutMs for the "actions" reply. Anything that goes wrong
// (not connected, timeout, malformed or infeasible action) makes the organism
// fall back to its own built-in contextual bandit for that decision; the manager
// counts those fallbacks. State changes (connect / disconnect / timeout / recovery)
// are logged once, never per decision.
//
// Nothing here touches the simulation's seeded FRandomStream.
// ---------------------------------------------------------------------------
struct FSWPolicyServer
{
	FString Host;
	int32 Port = 0;
	FString Name;                    // "host:port", used in logs, CSV and the HUD
	bool bLumen = false;
	bool bTecton = false;

	FSocket* Socket = nullptr;
	bool bConnected = false;
	double NextConnectAttempt = 0.0; // wall-clock seconds (FPlatformTime)
	TArray<uint8> RecvBuf;           // bytes received but not yet consumed (partial lines)

	// State flags for once-per-change logging.
	bool bTimingOut = false;
	bool bEverConnected = false;

	// Stats (wall clock).
	int32 Requests = 0;              // decide requests sent
	int32 Replies = 0;               // actions replies received in time
	int32 Timeouts = 0;
	int32 Stale = 0;                 // replies for an older step, discarded
	double RoundTripMsSum = 0.0;
	double LastReportTime = 0.0;
	int32 RequestsAtLastReport = 0;

	// Per-exchange scratch.
	double SendTime = 0.0;
	bool bAwaitingReply = false;

	bool Controls(ESWSpecies S) const { return S == ESWSpecies::Lumen ? bLumen : bTecton; }
	float MeanRoundTripMs() const { return Replies > 0 ? static_cast<float>(RoundTripMsSum / Replies) : 0.f; }
};

class FSWPolicyClient
{
public:
	~FSWPolicyClient();

	// "host:port=Lumen|host:port=Tecton|host:port=Both". Returns the number of servers parsed.
	int32 Configure(const FString& Spec, int32 InTimeoutMs);
	void Shutdown();

	bool HasServers() const { return Servers.Num() > 0; }
	int32 NumServers() const { return Servers.Num(); }
	const FSWPolicyServer& GetServer(int32 Idx) const { return Servers[Idx]; }
	// Servers configured for a species (indices into the server list).
	void ServersFor(ESWSpecies S, TArray<int32>& Out) const;
	int32 NumConnected() const;

	// Reconnect attempts (every ReconnectSeconds while disconnected), incoming "log"
	// lines, and a periodic stats line. Call once per rendered frame.
	void Tick();

	// Sent to every connected server now, and re-sent automatically after a reconnect.
	void SetHello(const FString& HelloLine);

	// One request per server (empty string = nothing to send to that server). Blocks up to
	// TimeoutMs for the replies. OutActions[server][agent id] = action index 0..6.
	void Exchange(int32 Step, const TArray<FString>& RequestLines, TArray<TMap<int32, int32>>& OutActions);

	int32 GetTimeoutMs() const { return TimeoutMs; }

private:
	TArray<FSWPolicyServer> Servers;
	int32 TimeoutMs = 200;
	FString HelloLine;
	static constexpr double ReconnectSeconds = 5.0;
	static constexpr double ReportSeconds = 10.0;

	bool TryConnect(FSWPolicyServer& S);
	void Disconnect(FSWPolicyServer& S, const TCHAR* Reason);
	bool SendLine(FSWPolicyServer& S, const FString& Line);
	// Reads one complete line if available before DeadlineWall (0 = do not wait). False on nothing / disconnect.
	bool ReadLine(FSWPolicyServer& S, double DeadlineWall, FString& OutLine);
	// Handles non-"actions" messages (log lines). Returns true if the line was consumed.
	bool HandleSideMessage(FSWPolicyServer& S, const TSharedPtr<FJsonObject>& Msg);
	void DrainSideMessages(FSWPolicyServer& S);
	static bool ParseAction(const TSharedPtr<class FJsonValue>& V, int32& OutIdx);
};
