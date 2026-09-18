#pragma once

#include "CoreMinimal.h"
#include "SWTypes.h"

class ASWAgent;

// Plain CSV logger. Three files per run under Saved/SymbioticWorld/<run_id>/:
//   agents.csv      per-agent rows every AgentLogInterval logical seconds
//   births.csv      one row per birth: parent/child ids and both genomes
//   population.csv  per-species summary every 5 logical seconds
//   commands.csv    one row per control-file command executed (docs/CONTROL_FILE.md), written at once
// agents.csv, deaths.csv and population.csv end with river_crossings: the organism's lifetime count of
// bank-to-bank crossings of the main channel, or the species' cumulative count in population.csv. deaths.csv
// also ends with mid_crossing: 1 when the organism died in the water past the centreline, not yet ashore.
// Positions: agents.csv ends with x,y,river_dist,in_water and population.csv with mean_river_dist,frac_in_water,
// where river_dist is |Y - main-channel centreline| (the valley runs along X) - how far from the river it stands.
// Columns follow Appendix A of the spec, extended with mode, context bin and
// the explore flag so a run can be audited offline (Analysis/analyze_run.py).
class FSWRunLogger
{
public:
	void Open(const FString& RunId, int32 Seed, ESWLearningMode Mode);
	void Close();
	bool IsOpen() const { return bOpen; }

	// Policy = "builtin" or "ext:host:port" (the external policy server that chooses this organism's actions).
	void LogAgent(const ASWAgent& A, float SimTime, bool bDrought,
	              int32 Births, int32 Deaths, int32 PopLumen, int32 PopTecton,
	              float ResourceA, float ResourceB, const FString& Policy);

	void LogBirth(float SimTime, const ASWAgent& Parent, const ASWAgent& Child);
	void LogDeath(float SimTime, const ASWAgent& A, const TCHAR* Cause);

	void LogPopulation(float SimTime, ESWSpecies Species, int32 N,
	                   float MeanAlpha, float SdAlpha, float MeanEps, float SdEps,
	                   float MeanSocial, float SdSocial, float MeanEnv, float SdEnv, float MeanGen, int32 MaxGen,
	                   int32 Births, int32 Deaths, float ResourceA, float ResourceB, bool bDrought,
	                   float TraceXMean, float TraceYMean, int32 ExtDecisions, int32 ExtFallbacks, int32 RiverCrossings,
	                   float MeanRiverDist, float FracInWater);

	// Control-file command (docs/CONTROL_FILE.md): run_id,sim_time,wall_utc,command,result. Appended immediately
	// (not buffered) so the audit trail survives a crash; command and result are CSV-quoted.
	void LogCommand(float SimTime, const FString& Command, const FString& Result);

	void Flush();

	const FString& GetDirectory() const { return Directory; }

private:
	bool bOpen = false;
	FString RunId;
	int32 Seed = 0;
	ESWLearningMode Mode = ESWLearningMode::LearningEvolution;
	FString Directory;
	FString AgentsPath, BirthsPath, DeathsPath, PopulationPath, CommandsPath;
	TArray<FString> AgentsBuf, BirthsBuf, DeathsBuf, PopulationBuf;

	void Append(const FString& Path, TArray<FString>& Buf);
};
