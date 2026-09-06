#include "SWTypes.h"

namespace
{
	// Box–Muller from the seeded stream, so runs stay reproducible.
	float GaussianFromStream(FRandomStream& Rng)
	{
		float U1 = Rng.FRand();
		float U2 = Rng.FRand();
		U1 = FMath::Max(U1, 1e-7f);
		return FMath::Sqrt(-2.f * FMath::Loge(U1)) * FMath::Cos(2.f * PI * U2);
	}
}

FSWGenome FSWGenome::Mutated(FRandomStream& Rng, float Sigma) const
{
	FSWGenome Child = *this;
	Child.Alpha   += Sigma * GaussianFromStream(Rng);
	Child.Epsilon += Sigma * GaussianFromStream(Rng);
	Child.Social  += Sigma * GaussianFromStream(Rng);
	Child.EnvEffect += Sigma * GaussianFromStream(Rng);
	Child.Clamp();
	return Child;
}
