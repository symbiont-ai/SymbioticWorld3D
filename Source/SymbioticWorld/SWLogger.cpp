#include "SWLogger.h"
#include "SWAgent.h"
#include "SymbioticWorld.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void FSWRunLogger::Open(const FString& InRunId, int32 InSeed, ESWLearningMode InMode)
{
	Close();
	RunId = InRunId;
	Seed = InSeed;
	Mode = InMode;
	Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SymbioticWorld"), RunId);
	IFileManager::Get().MakeDirectory(*Directory, true);

	AgentsPath = FPaths::Combine(Directory, TEXT("agents.csv"));
	BirthsPath = FPaths::Combine(Directory, TEXT("births.csv"));
	DeathsPath = FPaths::Combine(Directory, TEXT("deaths.csv"));
	PopulationPath = FPaths::Combine(Directory, TEXT("population.csv"));

	FString AgentHeader = TEXT("run_id,seed,mode,sim_time,generation,drought_state,agent_id,parent_id,species,age,energy,alpha,epsilon,social,env_effect,energy_bin,current_action,explored,reward,decisions,trace_x,trace_y");
	static const TCHAR* BinNames[SW_NUM_ENERGY_BINS] = { TEXT("low"), TEXT("mid"), TEXT("high") };
	for (int32 B = 0; B < SW_NUM_ENERGY_BINS; ++B)
	{
		for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
		{
			AgentHeader += FString::Printf(TEXT(",Q_%s_%s"), BinNames[B], SWActionName(static_cast<ESWAction>(A)));
		}
	}
	AgentHeader += TEXT(",births,deaths,pop_lumen,pop_tecton,resource_A,resource_B,policy\n");
	FFileHelper::SaveStringToFile(AgentHeader, *AgentsPath, FFileHelper::EEncodingOptions::ForceAnsi);

	FFileHelper::SaveStringToFile(TEXT("run_id,sim_time,parent_id,child_id,species,child_generation,child_alpha,child_epsilon,child_social,child_env_effect,parent_alpha,parent_epsilon,parent_social,parent_env_effect,parent_age,parent_energy\n"), *BirthsPath, FFileHelper::EEncodingOptions::ForceAnsi);
	FFileHelper::SaveStringToFile(TEXT("run_id,sim_time,agent_id,species,generation,age,energy,cause,alpha,epsilon,social,env_effect,decisions\n"), *DeathsPath, FFileHelper::EEncodingOptions::ForceAnsi);
	FFileHelper::SaveStringToFile(TEXT("run_id,seed,mode,sim_time,species,n,mean_alpha,sd_alpha,mean_epsilon,sd_epsilon,mean_social,sd_social,mean_env_effect,sd_env_effect,mean_generation,max_generation,births,deaths,resource_A,resource_B,drought_state,trace_X_mean,trace_Y_mean,ext_decisions,ext_fallbacks\n"), *PopulationPath, FFileHelper::EEncodingOptions::ForceAnsi);

	bOpen = true;
	UE_LOG(LogSymbioticWorld, Log, TEXT("Run log opened: %s"), *Directory);
}

void FSWRunLogger::Close()
{
	if (!bOpen) return;
	Flush();
	bOpen = false;
}

void FSWRunLogger::LogAgent(const ASWAgent& A, float SimTime, bool bDrought,
                            int32 Births, int32 Deaths, int32 PopLumen, int32 PopTecton,
                            float ResourceA, float ResourceB, const FString& Policy)
{
	if (!bOpen) return;
	const FSWGenome& G = A.GetGenome();
	const int32 Bin = A.GetCurrentContext();
	FString Row = FString::Printf(TEXT("%s,%d,%s,%.2f,%d,%d,%d,%d,%s,%.2f,%.3f,%.4f,%.4f,%.4f,%.4f,%d,%s,%d,%.4f,%d,%.3f,%.3f"),
		*RunId, Seed, SWModeName(Mode), SimTime, A.GetGeneration(), bDrought ? 1 : 0,
		A.GetAgentId(), A.GetParentId(), SWSpeciesName(A.GetSpecies()), A.GetAge(), A.GetEnergy(),
		G.Alpha, G.Epsilon, G.Social, G.EnvEffect, Bin, SWActionName(A.GetCurrentAction()), A.WasLastExplored() ? 1 : 0,
		A.GetLastReward(), A.GetDecisionCount(), A.GetLocalTraceX(), A.GetLocalTraceY());
	for (int32 B = 0; B < SW_NUM_ENERGY_BINS; ++B)
	{
		for (int32 Act = 0; Act < SW_NUM_ACTIONS; ++Act)
		{
			Row += FString::Printf(TEXT(",%.4f"), A.GetBandit().Value(B, static_cast<ESWAction>(Act)));
		}
	}
	Row += FString::Printf(TEXT(",%d,%d,%d,%d,%.1f,%.1f,%s\n"), Births, Deaths, PopLumen, PopTecton, ResourceA, ResourceB, *Policy);
	AgentsBuf.Add(MoveTemp(Row));
	if (AgentsBuf.Num() >= 2000) Append(AgentsPath, AgentsBuf);
}

void FSWRunLogger::LogBirth(float SimTime, const ASWAgent& Parent, const ASWAgent& Child)
{
	if (!bOpen) return;
	const FSWGenome& PG = Parent.GetGenome();
	const FSWGenome& CG = Child.GetGenome();
	BirthsBuf.Add(FString::Printf(TEXT("%s,%.2f,%d,%d,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f\n"),
		*RunId, SimTime, Parent.GetAgentId(), Child.GetAgentId(), SWSpeciesName(Child.GetSpecies()), Child.GetGeneration(),
		CG.Alpha, CG.Epsilon, CG.Social, CG.EnvEffect, PG.Alpha, PG.Epsilon, PG.Social, PG.EnvEffect, Parent.GetAge(), Parent.GetEnergy()));
	if (BirthsBuf.Num() >= 200) Append(BirthsPath, BirthsBuf);
}

void FSWRunLogger::LogDeath(float SimTime, const ASWAgent& A, const TCHAR* Cause)
{
	if (!bOpen) return;
	const FSWGenome& G = A.GetGenome();
	DeathsBuf.Add(FString::Printf(TEXT("%s,%.2f,%d,%s,%d,%.2f,%.3f,%s,%.4f,%.4f,%.4f,%.4f,%d\n"),
		*RunId, SimTime, A.GetAgentId(), SWSpeciesName(A.GetSpecies()), A.GetGeneration(), A.GetAge(), A.GetEnergy(), Cause,
		G.Alpha, G.Epsilon, G.Social, G.EnvEffect, A.GetDecisionCount()));
	if (DeathsBuf.Num() >= 200) Append(DeathsPath, DeathsBuf);
}

void FSWRunLogger::LogPopulation(float SimTime, ESWSpecies Species, int32 N,
                                 float MeanAlpha, float SdAlpha, float MeanEps, float SdEps,
                                 float MeanSocial, float SdSocial, float MeanEnv, float SdEnv, float MeanGen, int32 MaxGen,
                                 int32 Births, int32 Deaths, float ResourceA, float ResourceB, bool bDrought,
                                 float TraceXMean, float TraceYMean, int32 ExtDecisions, int32 ExtFallbacks)
{
	if (!bOpen) return;
	PopulationBuf.Add(FString::Printf(TEXT("%s,%d,%s,%.2f,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%d,%d,%d,%.1f,%.1f,%d,%.4f,%.4f,%d,%d\n"),
		*RunId, Seed, SWModeName(Mode), SimTime, SWSpeciesName(Species), N,
		MeanAlpha, SdAlpha, MeanEps, SdEps, MeanSocial, SdSocial, MeanEnv, SdEnv, MeanGen, MaxGen,
		Births, Deaths, ResourceA, ResourceB, bDrought ? 1 : 0, TraceXMean, TraceYMean, ExtDecisions, ExtFallbacks));
	if (PopulationBuf.Num() >= 100) Append(PopulationPath, PopulationBuf);
}

void FSWRunLogger::Append(const FString& Path, TArray<FString>& Buf)
{
	if (Buf.Num() == 0) return;
	FString Blob;
	Blob.Reserve(Buf.Num() * 160);
	for (const FString& L : Buf) Blob += L;
	FFileHelper::SaveStringToFile(Blob, *Path, FFileHelper::EEncodingOptions::ForceAnsi, &IFileManager::Get(), FILEWRITE_Append);
	Buf.Reset();
}

void FSWRunLogger::Flush()
{
	Append(AgentsPath, AgentsBuf);
	Append(BirthsPath, BirthsBuf);
	Append(DeathsPath, DeathsBuf);
	Append(PopulationPath, PopulationBuf);
}
