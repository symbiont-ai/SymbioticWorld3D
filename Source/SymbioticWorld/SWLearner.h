#pragma once

#include "CoreMinimal.h"
#include "SWTypes.h"

// ---------------------------------------------------------------------------
// FSWContextualBandit
//
// Exactly what it says: a tabular contextual bandit.
//   context  c  ∈ {0,1,2}          (energy bin)
//   actions  a  ∈ ESWAction        (7, of which a subset is feasible per step)
//   value    Q[c][a]
//   policy   epsilon-greedy over FEASIBLE actions only
//   update   Q[c][a] <- Q[c][a] + alpha * (r - Q[c][a])
//
// There is no bootstrapping from the next state (gamma = 0), so this is NOT
// Q-learning and should not be described as such. Alpha and epsilon are read
// from the agent's inherited genome at every call, which is what makes them
// evolvable without touching this class.
// ---------------------------------------------------------------------------
struct FSWContextualBandit
{
	float Q[SW_NUM_ENERGY_BINS][SW_NUM_ACTIONS];
	int32 Visits[SW_NUM_ENERGY_BINS][SW_NUM_ACTIONS];

	FSWContextualBandit();

	// Uniform(0, QInitMax) per cell, from the run's seeded stream.
	void InitRandom(FRandomStream& Rng, float QInitMax);

	// Returns the chosen action. FeasibleMask bit i set => action i is allowed.
	// If no action is feasible, Rest is returned (Rest is always feasible by
	// construction, so this is defensive only).
	ESWAction Select(int32 Context, uint32 FeasibleMask, float Epsilon, FRandomStream& Rng, bool& bOutExplored) const;

	void Update(int32 Context, ESWAction Action, float Reward, float Alpha);

	float Value(int32 Context, ESWAction Action) const { return Q[Context][static_cast<int32>(Action)]; }

	// Greedy action under the mask (ties broken by lowest index — deterministic).
	ESWAction Greedy(int32 Context, uint32 FeasibleMask) const;

	static bool IsFeasible(uint32 Mask, ESWAction A) { return (Mask & (1u << static_cast<uint32>(A))) != 0; }
	static uint32 Bit(ESWAction A) { return 1u << static_cast<uint32>(A); }
};
