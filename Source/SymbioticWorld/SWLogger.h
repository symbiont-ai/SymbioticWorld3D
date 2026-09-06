#pragma once

#include "CoreMinimal.h"
#include "SWTypes.h"

class ASWAgent;

// Plain CSV logger. Three files per run under Saved/SymbioticWorld/<run_id>/:
//   agents.csv      per-agent rows every AgentLogInterval logical seconds
//   births.csv      one row per birth: parent/child ids and both genomes
//   population.csv  per-species summary every 5 logical seconds
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
	                   float TraceXMean, float TraceYMean, int32 ExtDecisions, int32 ExtFallbacks);

	void Flush();

	const FString& GetDirectory() const { return Directory; }

private:
	bool bOpen = false;
	FString RunId;
	int32 Seed = 0;
	ESWLearningMode Mode = ESWLearningMode::LearningEvolution;
	FString Directory;
	FString AgentsPath, BirthsPath, DeathsPath, PopulationPath;
	TArray<FString> AgentsBuf, BirthsBuf, DeathsBuf, PopulationBuf;

	void Append(const FString& Path, TArray<FString>& Buf);
};
