#pragma once

#include "CoreMinimal.h"

// ---------------------------------------------------------------------------
// A decaying scalar field on an N x N grid over the (rectangular) arena. Two instances:
//   Trace X  - Lumen information markers (short half-life)
//   Trace Y  - Tecton soil/moisture modification (long half-life)
// Deterministic: advanced on the manager's fixed logical substep only.
// Values are clamped to [0, MaxValue]. Not a UObject: plain data owned by
// ASWWorldManager; the environment reads it for the ground overlay.
// ---------------------------------------------------------------------------
class SYMBIOTICWORLD_API FSWTraceField
{
public:
	void Init(int32 InCells, float InHalfSizeX, float InHalfSizeY, float InHalfLifeSeconds, float InMaxValue);

	// Exponential decay: v *= 0.5^(Dt / HalfLife).
	void Decay(float Dt);

	// Add Amount at (X, Y) with a 3x3 falloff (centre full, edges 35 %).
	void Deposit(float X, float Y, float Amount);

	// Bilinear-free nearest-cell read; 0 outside the grid.
	float Sample(float X, float Y) const;

	// World-space direction (unit, XY) of increasing value from the 4 neighbours, correct for
	// rectangular cells; false if flat.
	bool Gradient(float X, float Y, FVector& OutDir) const;

	float Mean() const;
	float MaxCell() const;
	int32 Cells() const { return N; }
	float HalfSize() const { return HalfX; }    // along the valley (X)
	float HalfSizeY() const { return HalfY; }   // across it (Y)
	float CellSize() const { return N > 0 ? 2.f * HalfX / N : 1.f; }
	float CellSizeY() const { return N > 0 ? 2.f * HalfY / N : 1.f; }
	float At(int32 I, int32 J) const { return (I >= 0 && J >= 0 && I < N && J < N) ? V[J * N + I] : 0.f; }
	void Reset() { for (float& X : V) X = 0.f; }

private:
	int32 N = 0;
	float HalfX = 1.f;
	float HalfY = 1.f;
	float HalfLife = 10.f;
	float MaxValue = 1.f;
	TArray<float> V;

	bool ToCell(float X, float Y, int32& I, int32& J) const;
};
