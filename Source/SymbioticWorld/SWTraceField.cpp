#include "SWTraceField.h"

void FSWTraceField::Init(int32 InCells, float InHalfSize, float InHalfLifeSeconds, float InMaxValue)
{
	N = FMath::Clamp(InCells, 4, 256);
	Half = FMath::Max(InHalfSize, 100.f);
	HalfLife = FMath::Max(InHalfLifeSeconds, 0.1f);
	MaxValue = FMath::Max(InMaxValue, 0.01f);
	V.SetNumZeroed(N * N);
}

bool FSWTraceField::ToCell(float X, float Y, int32& I, int32& J) const
{
	if (N <= 0) return false;
	const float Cell = CellSize();
	I = static_cast<int32>(FMath::Floor((X + Half) / Cell));
	J = static_cast<int32>(FMath::Floor((Y + Half) / Cell));
	return I >= 0 && J >= 0 && I < N && J < N;
}

void FSWTraceField::Decay(float Dt)
{
	if (Dt <= 0.f) return;
	const float K = FMath::Pow(0.5f, Dt / HalfLife);
	for (float& X : V)
	{
		X *= K;
		if (X < 1e-4f) X = 0.f;
	}
}

void FSWTraceField::Deposit(float X, float Y, float Amount)
{
	int32 I, J;
	if (!ToCell(X, Y, I, J) || Amount <= 0.f) return;
	for (int32 dj = -1; dj <= 1; ++dj)
	{
		for (int32 di = -1; di <= 1; ++di)
		{
			const int32 II = I + di, JJ = J + dj;
			if (II < 0 || JJ < 0 || II >= N || JJ >= N) continue;
			const float W = (di == 0 && dj == 0) ? 1.f : ((di == 0 || dj == 0) ? 0.35f : 0.18f);
			float& Cell = V[JJ * N + II];
			Cell = FMath::Min(Cell + Amount * W, MaxValue);
		}
	}
}

float FSWTraceField::Sample(float X, float Y) const
{
	int32 I, J;
	return ToCell(X, Y, I, J) ? V[J * N + I] : 0.f;
}

bool FSWTraceField::Gradient(float X, float Y, FVector& OutDir) const
{
	int32 I, J;
	if (!ToCell(X, Y, I, J)) return false;
	const float Gx = At(I + 1, J) - At(I - 1, J);
	const float Gy = At(I, J + 1) - At(I, J - 1);
	if (FMath::Abs(Gx) < 1e-3f && FMath::Abs(Gy) < 1e-3f) return false;
	OutDir = FVector(Gx, Gy, 0.f).GetSafeNormal();
	return true;
}

float FSWTraceField::Mean() const
{
	if (V.Num() == 0) return 0.f;
	double S = 0.0;
	for (float X : V) S += X;
	return static_cast<float>(S / V.Num());
}

float FSWTraceField::MaxCell() const
{
	float M = 0.f;
	for (float X : V) M = FMath::Max(M, X);
	return M;
}
