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
	const TCHAR* SpeciesLabel() const { return bLumen && bTecton ? TEXT("Both") : (bLumen ? TEXT("Lumen") : TEXT("Tecton")); }
};

// One parsed "host:port=Species" entry (from -SWPolicy or the server list file), before it has a socket.
struct FSWPolicyServerSpec
{
	FString Host;
	int32 Port = 0;
	FString Name;                    // "host:port"
	bool bLumen = false;
	bool bTecton = false;

	bool SameServer(const FSWPolicyServerSpec& O) const { return Name.Equals(O.Name, ESearchCase::IgnoreCase); }
	bool SameSpecies(const FSWPolicyServerSpec& O) const { return bLumen == O.bLumen && bTecton == O.bTecton; }
	const TCHAR* SpeciesLabel() const { return bLumen && bTecton ? TEXT("Both") : (bLumen ? TEXT("Lumen") : TEXT("Tecton")); }
};

class FSWPolicyClient
{
public:
	~FSWPolicyClient();

	// Parses one "host:port=Lumen|Tecton|Both" entry (species case-insensitive, whitespace trimmed).
	// False with a reason in OutError when the entry is malformed. Pure: no sockets, no logging.
	static bool ParseServerEntry(const FString& Entry, FSWPolicyServerSpec& Out, FString& OutError);
	// "host:port=Lumen|host:port=Tecton|host:port=Both" (-SWPolicy). Bad entries are logged and skipped.
	static int32 ParseServerList(const FString& Spec, TArray<FSWPolicyServerSpec>& Out);

	void SetTimeoutMs(int32 InTimeoutMs) { TimeoutMs = FMath::Clamp(InTimeoutMs, 1, 60000); }
	// Makes the server list exactly Desired (in that order). A server already present (same host:port,
	// case-insensitive) keeps its socket, stats and reconnect timer and only takes the new species flags;
	// a new one starts disconnected with an immediate connect attempt on the next Tick() (same path as
	// launch-time servers); one no longer listed is closed. OutOldToNew[old index] = new index, or -1
	// when removed, so the owner can remap organisms bound by index. Returns the number of servers now.
	int32 ApplyServerSet(const TArray<FSWPolicyServerSpec>& Desired, TArray<int32>& OutOldToNew);
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

	// Stored for every (re)connect. bSendNow also sends it to every server connected right now
	// (run start / reset); false only updates the stored line (server set changed while running,
	// so that connected servers do not treat it as a new run and forget their organisms).
	void SetHello(const FString& HelloLine, bool bSendNow = true);

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
	// Closes the socket without the "disconnected, retrying" warning (shutdown / removed from the set).
	void CloseSocket(FSWPolicyServer& S);
	bool SendLine(FSWPolicyServer& S, const FString& Line);
	// Reads one complete line if available before DeadlineWall (0 = do not wait). False on nothing / disconnect.
	bool ReadLine(FSWPolicyServer& S, double DeadlineWall, FString& OutLine);
	// Handles non-"actions" messages (log lines). Returns true if the line was consumed.
	bool HandleSideMessage(FSWPolicyServer& S, const TSharedPtr<FJsonObject>& Msg);
	void DrainSideMessages(FSWPolicyServer& S);
	static bool ParseAction(const TSharedPtr<class FJsonValue>& V, int32& OutIdx);
};
