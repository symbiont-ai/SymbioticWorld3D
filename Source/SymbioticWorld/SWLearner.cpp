#include "SWLearner.h"

FSWContextualBandit::FSWContextualBandit()
{
	for (int32 C = 0; C < SW_NUM_ENERGY_BINS; ++C)
	{
		for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
		{
			Q[C][A] = 0.f;
			Visits[C][A] = 0;
		}
	}
}

void FSWContextualBandit::InitRandom(FRandomStream& Rng, float QInitMax)
{
	for (int32 C = 0; C < SW_NUM_ENERGY_BINS; ++C)
	{
		for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
		{
			Q[C][A] = Rng.FRandRange(0.f, QInitMax);
			Visits[C][A] = 0;
		}
	}
}

ESWAction FSWContextualBandit::Greedy(int32 Context, uint32 FeasibleMask) const
{
	int32 Best = -1;
	float BestV = -TNumericLimits<float>::Max();
	for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
	{
		if (!IsFeasible(FeasibleMask, static_cast<ESWAction>(A))) continue;
		if (Q[Context][A] > BestV)
		{
			BestV = Q[Context][A];
			Best = A;
		}
	}
	return Best < 0 ? ESWAction::Rest : static_cast<ESWAction>(Best);
}

ESWAction FSWContextualBandit::Select(int32 Context, uint32 FeasibleMask, float Epsilon, FRandomStream& Rng, bool& bOutExplored) const
{
	Context = FMath::Clamp(Context, 0, SW_NUM_ENERGY_BINS - 1);
	bOutExplored = false;

	// Collect feasible actions.
	int32 Feasible[SW_NUM_ACTIONS];
	int32 N = 0;
	for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
	{
		if (IsFeasible(FeasibleMask, static_cast<ESWAction>(A))) Feasible[N++] = A;
	}
	if (N == 0) return ESWAction::Rest;
	if (N == 1) return static_cast<ESWAction>(Feasible[0]);

	if (Rng.FRand() < Epsilon)
	{
		bOutExplored = true;
		return static_cast<ESWAction>(Feasible[Rng.RandRange(0, N - 1)]);
	}
	return Greedy(Context, FeasibleMask);
}

void FSWContextualBandit::Update(int32 Context, ESWAction Action, float Reward, float Alpha)
{
	Context = FMath::Clamp(Context, 0, SW_NUM_ENERGY_BINS - 1);
	const int32 A = static_cast<int32>(Action);
	if (A < 0 || A >= SW_NUM_ACTIONS) return;
	Q[Context][A] += Alpha * (Reward - Q[Context][A]);
	Visits[Context][A]++;
}
