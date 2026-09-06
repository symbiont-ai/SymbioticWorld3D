#include "SWProcMesh.h"

namespace
{
	float Noise2(float X, float Y) { return FMath::PerlinNoise2D(FVector2D(X, Y)); }

	// Fractal 2D noise in [-1, 1].
	float FBm2(float X, float Y, int32 Octaves, float Lacunarity = 2.f, float Gain = 0.5f)
	{
		float Sum = 0.f, Amp = 1.f, Norm = 0.f, F = 1.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Noise2(X * F, Y * F);
			Norm += Amp;
			Amp *= Gain;
			F *= Lacunarity;
		}
		return Sum / FMath::Max(Norm, 1e-4f);
	}

	// Cheap 3D noise from three 2D samples (enough for rock displacement).
	float Noise3(const FVector& P)
	{
		return (Noise2(P.X, P.Y) + Noise2(P.Y + 31.7f, P.Z) + Noise2(P.Z + 77.3f, P.X)) / 3.f;
	}

	float FBm3(const FVector& P, int32 Octaves)
	{
		float Sum = 0.f, Amp = 1.f, Norm = 0.f, F = 1.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Noise3(P * F);
			Norm += Amp; Amp *= 0.5f; F *= 2.f;
		}
		return Sum / Norm;
	}
}

namespace SWProc
{

void FMeshData::Append(const FMeshData& Other, const FTransform& Xf)
{
	const int32 Base = Verts.Num();
	Verts.Reserve(Base + Other.Verts.Num());
	for (int32 i = 0; i < Other.Verts.Num(); ++i)
	{
		Verts.Add(Xf.TransformPosition(Other.Verts[i]));
		Normals.Add(Xf.TransformVectorNoScale(Other.Normals.IsValidIndex(i) ? Other.Normals[i] : FVector::UpVector).GetSafeNormal());
		UV0.Add(Other.UV0.IsValidIndex(i) ? Other.UV0[i] : FVector2D::ZeroVector);
		Colors.Add(Other.Colors.IsValidIndex(i) ? Other.Colors[i] : FLinearColor::Black);
	}
	Tris.Reserve(Tris.Num() + Other.Tris.Num());
	for (int32 T : Other.Tris) Tris.Add(T + Base);
}

void ComputeNormals(FMeshData& M)
{
	M.Normals.SetNumZeroed(M.Verts.Num());
	for (int32 i = 0; i + 2 < M.Tris.Num(); i += 3)
	{
		const int32 A = M.Tris[i], B = M.Tris[i + 1], C = M.Tris[i + 2];
		// Unreal's ProceduralMesh uses clockwise winding when viewed from outside;
		// the builders below wind (A,B,C) so that (B-A)x(C-A) points inward, hence the sign.
		const FVector N = FVector::CrossProduct(M.Verts[C] - M.Verts[A], M.Verts[B] - M.Verts[A]);
		M.Normals[A] += N; M.Normals[B] += N; M.Normals[C] += N;
	}
	for (FVector& N : M.Normals) N = N.GetSafeNormal(1e-6f, FVector::UpVector);
}

// ---------------------------------------------------------------------------
// Terrain field
// ---------------------------------------------------------------------------

float RiverCenterY(const FSWLookSettings& L, float X)
{
	const float K = 2.f * PI / FMath::Max(L.RiverWavelength, 1000.f);
	return L.RiverAmp * FMath::Sin(X * K) + 0.35f * L.RiverAmp * FMath::Sin(X * K * 2.3f + 1.7f);
}

float TerrainHeight(const FSWLookSettings& L, float X, float Y)
{
	const float Half = FMath::Max(L.TerrainHalfSize, 1000.f);
	// Valley cross-section: flat floor, rims rising steeply (sextic profile).
	const float Ny = FMath::Abs(Y) / FMath::Max(L.ValleyHalfWidth, 100.f);
	const float Rim = 1.f - FMath::Exp(-FMath::Pow(Ny, 6.f));
	// Valley ends rise so the horizon closes behind the camera (-X); the +X end (the sun end, ahead of
	// the start camera) stays low so the disc sits in the open gap behind the hero arch.
	const float Ex = FMath::Max(0.f, (FMath::Abs(X) - 0.62f * Half) / (0.22f * Half));
	const float EndFactor = X > 0.f ? 0.10f : 0.7f;   // 0.25 could still hide a sun disc below ~4 deg elevation
	const float EndRise = EndFactor * (1.f - FMath::Exp(-Ex * Ex));
	const float Rise = FMath::Clamp(Rim + EndRise, 0.f, 1.15f);

	const float Ridge = FBm2(X * 0.00022f + 3.1f, Y * 0.00022f - 7.7f, 4);
	const float Hills = L.ValleyDepth * Rise + L.RidgeNoiseAmp * Rise * (0.6f + Ridge) + 0.25f * L.RidgeNoiseAmp * FMath::Max(0.f, Ridge) * Rise;

	const float Floor = L.FloorNoiseAmp * FBm2(X * 0.0011f - 5.f, Y * 0.0011f + 2.f, 3);

	const float Dy = Y - RiverCenterY(L, X);
	const float W = FMath::Max(L.RiverWidth, 50.f);
	const float Carve = -L.RiverDepth * FMath::Exp(-(Dy * Dy) / (W * W));
	// Soft banks: a wider, shallower depression around the channel.
	const float Bank = -0.35f * L.RiverDepth * FMath::Exp(-(Dy * Dy) / (9.f * W * W));

	return Hills + Floor + Carve + Bank;
}

float GroundZ(const FSWLookSettings& L, float X, float Y, float WaterDrop)
{
	const float H = TerrainHeight(L, X, Y);
	return FMath::Max(H, L.WaterLevel - WaterDrop - 8.f);
}

void BuildTerrain(const FSWLookSettings& L, FMeshData& Out)
{
	const int32 N = FMath::Clamp(L.TerrainGrid, 16, 400);
	const float Half = L.TerrainHalfSize;
	const float Step = 2.f * Half / N;
	Out.Reserve((N + 1) * (N + 1), N * N * 6);
	for (int32 j = 0; j <= N; ++j)
	{
		for (int32 i = 0; i <= N; ++i)
		{
			const float X = -Half + i * Step;
			const float Y = -Half + j * Step;
			const float H = TerrainHeight(L, X, Y);
			// Wetness: near/below water level, fading out over WetlandBand.
			const float Wet = FMath::Clamp(1.f - (H - L.WaterLevel) / FMath::Max(L.WetlandBand, 1.f), 0.f, 1.f);
			// World-tiling UVs so any imported tiling ground material repeats every TerrainUVTile uu.
			const float Tile = FMath::Max(L.TerrainUVTile, 10.f);
			Out.AddVert(FVector(X, Y, H), FLinearColor(0.f, 0.f, Wet, 1.f), FVector2D(X / Tile, Y / Tile));
		}
	}
	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const int32 A = j * (N + 1) + i, B = A + 1, C = A + (N + 1), D = C + 1;
			Out.AddTri(A, C, B);
			Out.AddTri(B, C, D);
		}
	}
	ComputeNormals(Out);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void AppendEllipsoid(FMeshData& Out, const FTransform& Xf, const FVector& Radii, int32 Segs, int32 Rings,
                     TFunctionRef<FLinearColor(const FVector&, const FVector&)> ColorFn,
                     TFunctionRef<float(const FVector&)> DisplaceFn)
{
	FMeshData M;
	Segs = FMath::Max(Segs, 3); Rings = FMath::Max(Rings, 2);
	for (int32 r = 0; r <= Rings; ++r)
	{
		const float V = r / (float)Rings;
		const float Phi = V * PI;                 // 0 top .. PI bottom
		for (int32 s = 0; s <= Segs; ++s)
		{
			const float U = s / (float)Segs;
			const float Theta = U * 2.f * PI;
			const FVector Unit(FMath::Sin(Phi) * FMath::Cos(Theta), FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi));
			const float D = 1.f + DisplaceFn(Unit);
			const FVector P = Unit * Radii * D;
			M.AddVert(P, ColorFn(Unit, P), FVector2D(U, V));
		}
	}
	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A = r * (Segs + 1) + s, B = A + 1, C = A + Segs + 1, D = C + 1;
			M.AddTri(A, B, C);
			M.AddTri(B, D, C);
		}
	}
	ComputeNormals(M);
	Out.Append(M, Xf);
}

void AppendTaperedCylinder(FMeshData& Out, const FTransform& Xf, float R0, float R1, float Length, int32 Segs,
                           const FLinearColor& C0, const FLinearColor& C1)
{
	// Axis along local +Z from 0 to Length. Capped at both ends.
	FMeshData M;
	Segs = FMath::Max(Segs, 3);
	const int32 Rings = 3;
	for (int32 r = 0; r <= Rings; ++r)
	{
		const float V = r / (float)Rings;
		const float R = FMath::Lerp(R0, R1, V);
		for (int32 s = 0; s <= Segs; ++s)
		{
			const float Theta = s / (float)Segs * 2.f * PI;
			M.AddVert(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), V * Length), FMath::Lerp(C0, C1, V), FVector2D(s / (float)Segs, V));
		}
	}
	for (int32 r = 0; r < Rings; ++r)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A = r * (Segs + 1) + s, B = A + 1, C = A + Segs + 1, D = C + 1;
			M.AddTri(A, C, B);
			M.AddTri(B, C, D);
		}
	}
	// Caps.
	const int32 Bot = M.AddVert(FVector(0, 0, 0), C0);
	const int32 Top = M.AddVert(FVector(0, 0, Length), C1);
	for (int32 s = 0; s < Segs; ++s)
	{
		M.AddTri(Bot, s, s + 1);
		const int32 T0 = Rings * (Segs + 1) + s;
		M.AddTri(Top, T0 + 1, T0);
	}
	ComputeNormals(M);
	Out.Append(M, Xf);
}

// ---------------------------------------------------------------------------
// Landscape features
// ---------------------------------------------------------------------------

void BuildArch(FRandomStream& Rng, float MajorR, float MinorR, float NoiseAmp, FMeshData& Out)
{
	// Torus in the XZ plane: opening faces +/-Y. Thicker at the feet than at the crown.
	const int32 U = 36, V = 14;
	const float Seed = Rng.FRandRange(0.f, 100.f);
	for (int32 i = 0; i <= U; ++i)
	{
		const float A = i / (float)U * 2.f * PI;
		const float Thick = 1.f + 0.55f * FMath::Max(0.f, -FMath::Sin(A));   // bottom half thicker
		for (int32 j = 0; j <= V; ++j)
		{
			const float B = j / (float)V * 2.f * PI;
			const FVector Ring(FMath::Cos(A), 0.f, FMath::Sin(A));
			const FVector Tube = Ring * FMath::Cos(B) + FVector(0.f, FMath::Sin(B), 0.f);
			FVector P = Ring * MajorR + Tube * MinorR * Thick;
			const float N = FBm3(P * 0.004f + FVector(Seed), 3);
			const float N2 = FBm3(P * 0.02f + FVector(Seed * 2.f), 2);
			P += Tube * (NoiseAmp * N + 0.25f * NoiseAmp * N2);
			Out.AddVert(P, FLinearColor(0, 0, 0, 1), FVector2D(i / (float)U, j / (float)V));
		}
	}
	for (int32 i = 0; i < U; ++i)
	{
		for (int32 j = 0; j < V; ++j)
		{
			const int32 A = i * (V + 1) + j, B = A + 1, C = A + V + 1, D = C + 1;
			Out.AddTri(A, C, B);
			Out.AddTri(B, C, D);
		}
	}
	ComputeNormals(Out);
}

void BuildMassifArch(FRandomStream& Rng, float OpenW, float OpenH, float TotalH, float DepthCrown, float DepthFoot,
                     float LegW, float NoiseAmp, FMeshData& Out, TArray<FVector>* OutCrown)
{
	// Two curves in the local XZ plane share one parameter u in [0, 1] from the left foot to the right foot:
	//   u in [0, 0.22]   left jamb rising    (inner: x = -R; outer: flank tapering from LegW*1.35 to LegW at 0.55*TotalH)
	//   u in [0.22, 0.78] the vault           (inner: semicircle topping at OpenH; outer: rounded shoulders + flat top at TotalH)
	//   u in [0.78, 1]   right jamb, mirrored
	// The solid is a closed rectangular tube swept along u: intrados (opening ceiling, 6 segments across),
	// front face (4), extrados (top + outer flanks, 8), back face (4). Shared corner vertices mean the noise
	// displacement never opens cracks between the surfaces.
	const int32 NJ = 16, NV = 40, NU = NJ + NV + NJ;   // 72 segments, 73 rings
	const int32 KI = 6, KF = 4, KE = 8;                  // segments across intrados / each face / extrados
	const int32 Loop = KI + KF + KE + KF;                // 22 vertices around the section
	const float R = 0.5f * OpenW;                        // vault radius
	const float Zc = OpenH - R;                          // springing height of the semicircle
	const float ZBase = -0.25f * TotalH;
	const float ZShoulder = 0.55f * TotalH;
	const float XOut = R + LegW;                         // outer flank at the shoulder
	const float ShoulderT = 0.28f;                       // fraction of the vault section each shoulder rounds over
	const float Sx = ShoulderT * (OpenW + 2.f * LegW);   // shoulder radius, horizontal
	const float Sz = TotalH - ZShoulder;                 // shoulder radius, vertical
	const float StrataH = 150.f;
	const FVector Seed(Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f));
	const float SagSeed = Rng.FRandRange(0.f, 100.f);

	auto UAt = [&](int32 i) -> float
	{
		if (i <= NJ) return 0.22f * i / (float)NJ;
		if (i <= NJ + NV) return 0.22f + 0.56f * (i - NJ) / (float)NV;
		return 0.78f + 0.22f * (i - NJ - NV) / (float)NJ;
	};
	auto Inner = [&](float u) -> FVector2D   // (x, z)
	{
		if (u < 0.22f) { const float t = u / 0.22f; return FVector2D(-R, FMath::Lerp(ZBase, Zc, t)); }
		if (u <= 0.78f) { const float t = (u - 0.22f) / 0.56f; return FVector2D(-R * FMath::Cos(PI * t), Zc + R * FMath::Sin(PI * t)); }
		const float t = (u - 0.78f) / 0.22f; return FVector2D(R, FMath::Lerp(Zc, ZBase, t));
	};
	auto Outer = [&](float u) -> FVector2D
	{
		if (u < 0.22f)
		{
			const float t = u / 0.22f;
			return FVector2D(-(R + FMath::Lerp(LegW * 1.9f, LegW, t)), FMath::Lerp(ZBase, ZShoulder, t));
		}
		if (u <= 0.78f)
		{
			const float t = (u - 0.22f) / 0.56f;
			if (t < ShoulderT)
			{
				const float A = (t / ShoulderT) * 0.5f * PI;
				return FVector2D(-XOut + Sx * (1.f - FMath::Cos(A)), ZShoulder + Sz * FMath::Sin(A));
			}
			if (t > 1.f - ShoulderT)
			{
				const float A = ((1.f - t) / ShoulderT) * 0.5f * PI;
				return FVector2D(XOut - Sx * (1.f - FMath::Cos(A)), ZShoulder + Sz * FMath::Sin(A));
			}
			// Flat top: 3% sag toward the middle plus up to 2% noise, both windowed to zero at the shoulders.
			const float tt = (t - ShoulderT) / (1.f - 2.f * ShoulderT);
			const float X = FMath::Lerp(-(XOut - Sx), XOut - Sx, tt);
			const float Win = FMath::Sin(PI * tt);
			const float Z = TotalH * (1.f - 0.03f * Win + 0.02f * Win * FBm2(X * 0.0025f + SagSeed, SagSeed * 0.7f, 2));
			return FVector2D(X, Z);
		}
		const float t = (u - 0.78f) / 0.22f;
		return FVector2D(R + FMath::Lerp(LegW, LegW * 1.9f, t), FMath::Lerp(ZShoulder, ZBase, t));
	};
	auto Depth = [&](float u) -> float
	{
		const float JambT = u < 0.22f ? u / 0.22f : (u > 0.78f ? (1.f - u) / 0.22f : 1.f);
		return FMath::Lerp(DepthFoot, DepthCrown, FMath::SmoothStep(0.f, 1.f, JambT));
	};

	FMeshData M;
	M.Reserve((NU + 1) * Loop, NU * Loop * 6);
	TArray<FVector> Outward;
	Outward.Reserve((NU + 1) * Loop);
	TArray<FVector> Crown;
	for (int32 i = 0; i <= NU; ++i)
	{
		const float u = UAt(i);
		const FVector2D Pi = Inner(u), Po = Outer(u);
		const float d = Depth(u);
		const FVector2D Across2 = (Po - Pi).GetSafeNormal();
		const FVector Across(Across2.X, 0.f, Across2.Y);   // inner -> outer, in the section plane
		for (int32 j = 0; j < Loop; ++j)
		{
			// Section coordinates: s in [0 inner, 1 outer], y in [-d/2 front, +d/2 back].
			float s, y; FVector Dir; float Amp;
			if (j < KI)                                   // intrados: back -> front
			{
				const int32 k = j;
				s = 0.f; y = 0.5f * d - d * k / (float)KI;
				Dir = -Across; Amp = 0.5f;
				if (k == 0) { Dir = (-Across + FVector(0.f, 1.f, 0.f)).GetSafeNormal(); Amp = 0.4f; }
			}
			else if (j < KI + KF)                         // front face: inner -> outer
			{
				const int32 k = j - KI;
				s = k / (float)KF; y = -0.5f * d;
				Dir = FVector(0.f, -1.f, 0.f); Amp = 0.3f;
				if (k == 0) { Dir = (-Across + FVector(0.f, -1.f, 0.f)).GetSafeNormal(); Amp = 0.4f; }
			}
			else if (j < KI + KF + KE)                    // extrados: front -> back
			{
				const int32 k = j - KI - KF;
				s = 1.f; y = -0.5f * d + d * k / (float)KE;
				Dir = Across; Amp = 1.f;
				if (k == 0) { Dir = (Across + FVector(0.f, -1.f, 0.f)).GetSafeNormal(); Amp = 0.65f; }
			}
			else                                          // back face: outer -> inner
			{
				const int32 k = j - KI - KF - KE;
				s = 1.f - k / (float)KF; y = 0.5f * d;
				Dir = FVector(0.f, 1.f, 0.f); Amp = 0.3f;
				if (k == 0) { Dir = (Across + FVector(0.f, 1.f, 0.f)).GetSafeNormal(); Amp = 0.65f; }
			}
			FVector P(FMath::Lerp(Pi.X, Po.X, s), y, FMath::Lerp(Pi.Y, Po.Y, s));
			// FBm3 averages three 2D samples (amplitude ~1/3 of one Perlin); rescale to ~[-1, 1].
			const float N = 2.5f * FBm3(P * 0.0025f + Seed, 3);
			float Disp = Amp * NoiseAmp * N;
			// Horizontal strata: the upper 45% of each band stands proud so ledges show in silhouette. Irregular courses
			// (not brick rows): the band period drifts per vertex (FBm3 raw is ~[-0.4, 0.4], so 0.90..1.10 x StrataH) and
			// courses are broken off where a finer noise (rescaled to ~[-1, 1]) dips below -0.2.
			const float Period = StrataH * (0.75f + 0.5f * FMath::Clamp(FBm3(P * 0.0009f + Seed, 2) * 0.5f + 0.5f, 0.f, 1.f));
			const bool bBroken = 2.5f * FBm3(P * 0.004f + Seed * 2.f, 2) < -0.2f;
			if (!bBroken && FMath::Frac(P.Z / Period) > 0.55f) Disp += 0.22f * NoiseAmp;
			P += Dir * Disp;
			M.AddVert(P, FLinearColor(0.f, 0.f, 0.f, 1.f), FVector2D(u, j / (float)Loop));
			Outward.Add(Dir);
			if (j == KI + KF + KE / 2 && Po.Y >= 0.9f * TotalH) Crown.Add(P);   // extrados centreline (y = 0)
		}
	}
	for (int32 i = 0; i < NU; ++i)
	{
		for (int32 j = 0; j < Loop; ++j)
		{
			const int32 j1 = (j + 1) % Loop;
			const int32 A = i * Loop + j, B = i * Loop + j1, C = A + Loop, D = (i + 1) * Loop + j1;
			M.AddTri(A, C, B);
			M.AddTri(B, C, D);
		}
	}
	ComputeNormals(M);
	// Winding check: the computed normals must agree with the known outward directions; flip if not.
	double Agree = 0.0;
	for (int32 v = 0; v < M.Verts.Num(); ++v) Agree += FVector::DotProduct(M.Normals[v], Outward[v]);
	if (Agree < 0.0)
	{
		for (int32 t = 0; t + 2 < M.Tris.Num(); t += 3) Swap(M.Tris[t + 1], M.Tris[t + 2]);
		ComputeNormals(M);
	}
	// R = top-facing (moss mask in M_SW_Sandstone), soft ramp around normal.z = 0.6.
	for (int32 v = 0; v < M.Verts.Num(); ++v)
	{
		M.Colors[v].R = FMath::Clamp((M.Normals[v].Z - 0.45f) / 0.3f, 0.f, 1.f);
	}
	Out.Append(M, FTransform::Identity);
	if (OutCrown) *OutCrown = MoveTemp(Crown);
}

void BuildRock(FRandomStream& Rng, float Radius, float NoiseAmp, FMeshData& Out)
{
	const FVector Seed(Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f), Rng.FRandRange(0.f, 100.f));
	const FVector Radii(Radius * Rng.FRandRange(0.8f, 1.3f), Radius * Rng.FRandRange(0.8f, 1.3f), Radius * Rng.FRandRange(0.55f, 0.9f));
	AppendEllipsoid(Out, FTransform::Identity, Radii, 18, 12,
		[](const FVector&, const FVector&) { return FLinearColor(0, 0, 0, 1); },
		[&](const FVector& Unit) { return NoiseAmp * FBm3(Unit * 2.2f + Seed, 3) + 0.4f * NoiseAmp * FBm3(Unit * 6.f + Seed, 2); });
}

void BuildGlowCluster(FRandomStream& Rng, float Radius, FMeshData& Out)
{
	const int32 Count = 5 + Rng.RandRange(0, 3);
	for (int32 k = 0; k < Count; ++k)
	{
		const float Ang = Rng.FRandRange(0.f, 2.f * PI);
		const float Dist = k == 0 ? 0.f : Rng.FRandRange(0.35f, 1.f) * Radius;
		const float R = (k == 0 ? 0.55f : Rng.FRandRange(0.22f, 0.42f)) * Radius;
		const FVector Seed(Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f), 0.f);
		const FTransform Xf(FRotator(0, Rng.FRandRange(0.f, 360.f), 0), FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, R * 0.55f));
		AppendEllipsoid(Out, Xf, FVector(R, R * 0.9f, R * 0.8f), 12, 8,
			// Core stone glows fully; satellites glow on top only.
			[k](const FVector& Unit, const FVector&) { return FLinearColor(k == 0 ? 1.f : FMath::Clamp(Unit.Z, 0.f, 1.f), 0.f, 0.f, 1.f); },
			[&](const FVector& Unit) { return 0.18f * FBm3(Unit * 3.f + Seed, 2); });
	}
}

// ---------------------------------------------------------------------------
// Organisms
// ---------------------------------------------------------------------------

void BuildLumen(FRandomStream& Rng, FMeshData& Out)
{
	// Sleek quadruped ~150 uu long, legs about half the body height. Vertex
	// colour R = bioluminescent markings: a DOTTED dorsal line (not a solid
	// band), sparse flank rings, eye spots, tail-tip; G = phase along the body.
	const FVector Body(72.f, 17.f, 21.f);
	const float Seed = Rng.FRandRange(0.f, 10.f);
	const float DotPitch = Rng.FRandRange(0.30f, 0.38f);       // dots every ~17-21 uu
	const float BodyZ = 58.f;
	AppendEllipsoid(Out, FTransform(FVector(0.f, 0.f, BodyZ)), Body, 40, 20,
		[&](const FVector& U, const FVector& P)
		{
			const float Phase = (P.X + Body.X) / (2.f * Body.X);
			float Mark = 0.f;
			// Dorsal dots: high on the back, periodic along the spine.
			if (U.Z > 0.62f && FMath::Abs(U.Y) < 0.22f)
			{
				const float Dot = FMath::Sin(P.X * DotPitch + Seed);
				if (Dot > 0.55f) Mark = 1.f;
			}
			// Flank rings: sparse, mid-height.
			const float Ring = FMath::Sin(P.X * 0.17f + Seed * 1.3f);
			if (U.Z > -0.1f && U.Z < 0.55f && Ring > 0.93f) Mark = FMath::Max(Mark, 0.7f);
			return FLinearColor(Mark, Phase, 0.f, 1.f);
		},
		// Slight taper toward the hips and a subtle rib ripple.
		[&](const FVector& U) { return -0.05f * FMath::Max(0.f, -U.X) + 0.03f * FMath::Sin(U.X * 11.f + Seed); });
	// Neck: angled up and forward.
	AppendEllipsoid(Out, FTransform(FRotator(-28.f, 0.f, 0.f), FVector(66.f, 0.f, BodyZ + 16.f)), FVector(22.f, 10.f, 10.f), 16, 8,
		[](const FVector&, const FVector&) { return FLinearColor(0.f, 1.f, 0.f, 1.f); },
		[](const FVector&) { return 0.f; });
	// Head: wedge with eye spots.
	AppendEllipsoid(Out, FTransform(FRotator(-6.f, 0.f, 0.f), FVector(94.f, 0.f, BodyZ + 26.f)), FVector(20.f, 11.f, 10.f), 18, 9,
		[](const FVector& U, const FVector&)
		{
			const bool Eye = U.X > 0.30f && FMath::Abs(U.Y) > 0.55f && U.Z > 0.05f && U.Z < 0.6f;
			return FLinearColor(Eye ? 1.f : 0.f, 1.f, 0.f, 1.f);
		},
		[](const FVector& U) { return U.X > 0.6f ? -0.12f * (U.X - 0.6f) : 0.f; });   // narrows to a snout
	// Ears.
	AppendTaperedCylinder(Out, FTransform(FRotator(-55.f, 0.f, 0.f), FVector(88.f, 6.5f, BodyZ + 34.f)), 3.5f, 0.8f, 17.f, 6, FLinearColor(0, 1, 0, 1), FLinearColor(0.8f, 1, 0, 1));
	AppendTaperedCylinder(Out, FTransform(FRotator(-55.f, 0.f, 0.f), FVector(88.f, -6.5f, BodyZ + 34.f)), 3.5f, 0.8f, 17.f, 6, FLinearColor(0, 1, 0, 1), FLinearColor(0.8f, 1, 0, 1));
	// Legs: long and thin, pointing down (rotate 180), slightly splayed, a knee bend via two segments.
	const float LegX[2] = { 44.f, -42.f };
	for (int32 f = 0; f < 2; ++f)
	{
		for (int32 side = -1; side <= 1; side += 2)
		{
			const float Y = side * 12.f;
			const float Top = BodyZ - 10.f;
			// upper segment
			AppendTaperedCylinder(Out, FTransform(FRotator(180.f - (f == 0 ? 6.f : -6.f), 0.f, side * 6.f), FVector(LegX[f], Y, Top)), 5.5f, 3.8f, 26.f, 7,
				FLinearColor(0, f == 0 ? 0.78f : 0.18f, 0, 1), FLinearColor(0, f == 0 ? 0.78f : 0.18f, 0, 1));
			// lower segment (from the knee to the ground)
			const float KneeX = LegX[f] + (f == 0 ? -2.7f : 2.7f);
			AppendTaperedCylinder(Out, FTransform(FRotator(180.f + (f == 0 ? 5.f : -5.f), 0.f, side * 6.f), FVector(KneeX, Y + side * 2.7f, Top - 25.f)), 3.8f, 2.4f, 24.f, 7,
				FLinearColor(0, f == 0 ? 0.78f : 0.18f, 0, 1), FLinearColor(0.25f, f == 0 ? 0.78f : 0.18f, 0, 1));
		}
	}
	// Tail: long, curving up, glowing tip.
	AppendTaperedCylinder(Out, FTransform(FRotator(-118.f, 0.f, 0.f), FVector(-66.f, 0.f, BodyZ + 4.f)), 5.5f, 1.2f, 66.f, 7, FLinearColor(0.f, 0, 0, 1), FLinearColor(1.f, 0, 0, 1));
	ComputeNormals(Out);
}

void BuildTecton(FRandomStream& Rng, FMeshData& Out)
{
	// Rock-armoured quadruped ~360 uu long. R = amber seams where the armour
	// noise crosses zero (cracks between plates); no fungi, no vegetation.
	const FVector Body(150.f, 92.f, 82.f);
	const FVector Seed(Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f));
	// FBm3 averages three 2D samples, so its amplitude is ~1/3 of a single Perlin; rescale to ~[-1,1].
	auto Armour = [&](const FVector& U) { return 3.2f * FBm3(U * 2.6f + Seed, 3); };
	// Fine tessellation so the seam band is a few vertices wide instead of smeared
	// across big triangles; the seam itself is a sharp band around the noise
	// zero-crossing (|n| < ~0.05), so the glow reads as cracks between plates.
	AppendEllipsoid(Out, FTransform(FVector(0.f, 0.f, 105.f)), Body, 64, 40,
		[&](const FVector& U, const FVector& P)
		{
			// R = armour region (back and flanks); the crack lines themselves are
			// computed per pixel in M_SW_Creature (CrackMode = 1), so they stay
			// thin regardless of tessellation.
			const float Top = FMath::Clamp(U.Z + 0.55f, 0.f, 1.f);
			const float Phase = (P.X + Body.X) / (2.f * Body.X);
			return FLinearColor(Top, Phase, 0.f, 1.f);
		},
		[&](const FVector& U)
		{
			const float N = FMath::Abs(Armour(U));
			// Plates: raised where |n| is large, sunken cracks along the zero-crossing; flatter belly.
			const float Belly = U.Z < -0.3f ? 0.4f : 1.f;
			const float Plate = FMath::SmoothStep(0.05f, 0.30f, N);
			return Belly * (0.09f * Plate - 0.045f) + 0.025f * FBm3(U * 9.f + Seed, 2);
		});
	// Head: wedge-like, low.
	AppendEllipsoid(Out, FTransform(FRotator(-12.f, 0.f, 0.f), FVector(150.f, 0.f, 90.f)), FVector(62.f, 44.f, 36.f), 36, 20,
		[&](const FVector& U, const FVector&)
		{
			const bool Eye = U.X > 0.3f && FMath::Abs(U.Y) > 0.6f && U.Z > 0.1f && U.Z < 0.5f;
			const float Top = FMath::Clamp(U.Z + 0.3f, 0.f, 1.f) * 0.7f;
			return FLinearColor(Eye ? 1.f : Top, 1.f, 0.f, 1.f);
		},
		[&](const FVector& U) { return 0.06f * FMath::SmoothStep(0.05f, 0.3f, FMath::Abs(3.2f * FBm3(U * 3.f + Seed * 1.7f, 2))); });
	// Legs: thick columns.
	const float LegX[2] = { 92.f, -88.f };
	for (int32 f = 0; f < 2; ++f)
	{
		for (int32 side = -1; side <= 1; side += 2)
		{
			const FTransform Xf(FRotator(180.f, 0.f, side * 5.f), FVector(LegX[f], side * 62.f, 80.f));
			AppendTaperedCylinder(Out, Xf, 30.f, 24.f, 78.f, 10, FLinearColor(0.f, f == 0 ? 0.8f : 0.2f, 0, 1), FLinearColor(0, f == 0 ? 0.8f : 0.2f, 0, 1));
		}
	}
	// Stubby tail.
	AppendTaperedCylinder(Out, FTransform(FRotator(-150.f, 0.f, 0.f), FVector(-140.f, 0.f, 95.f)), 26.f, 8.f, 70.f, 9, FLinearColor(0.f, 0, 0, 1), FLinearColor(0.3f, 0, 0, 1));
	ComputeNormals(Out);
}

void BuildLeviathan(FRandomStream& Rng, FMeshData& Out)
{
	// ~1650 uu nose to fluke: roughly 4.5x the Tecton, so it reads as a different
	// order of animal rather than a third organism. Built only from ellipsoids so
	// every part's orientation is unambiguous (no cylinder axis convention).
	//
	// Vertex colour R = bioluminescent mask (throat grooves, raked flank scars, eye,
	// fluke edge), G = phase along the body from fluke (0) to snout (1) so the
	// M_SW_Creature pulse travels the length of the animal. B unused.
	const float Seed = Rng.FRandRange(0.f, 40.f);
	const FVector NSeed(Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f), Rng.FRandRange(0.f, 50.f));
	auto Phase = [](float X) { return FMath::Clamp((X + 820.f) / 1660.f, 0.f, 1.f); };

	// ---- Trunk: long barrel, tapering hard toward the tail stock ----------
	const FVector Trunk(560.f, 152.f, 180.f);
	AppendEllipsoid(Out, FTransform(FVector::ZeroVector), Trunk, 56, 28,
		[&](const FVector& U, const FVector& P)
		{
			float Mark = 0.f;
			// Ventral throat grooves: parallel lines running down the underside of the front half.
			if (U.Z < -0.30f && P.X > -140.f)
			{
				const float Groove = FMath::Sin(P.Y * 0.085f + Seed);
				if (Groove > 0.55f) Mark = 0.9f;
			}
			// Old raking scars across the flanks: sparse, thin, dimmer than the grooves.
			const float Rake = FMath::Sin(P.X * 0.020f + U.Z * 5.5f + Seed * 1.7f);
			if (FMath::Abs(U.Z) < 0.5f && Rake > 0.987f) Mark = FMath::Max(Mark, 0.45f);
			return FLinearColor(Mark, Phase(P.X), 0.f, 1.f);
		},
		[&](const FVector& U)
		{
			const float Back = FMath::Max(0.f, -U.X);
			const float Taper = -0.30f * Back * Back;          // narrows toward the peduncle
			const float Shoulder = 0.07f * FMath::Max(0.f, U.X); // fills out behind the head
			const float Belly = U.Z < -0.6f ? -0.05f : 0.f;      // flattens the underside
			return Taper + Shoulder + Belly + 0.012f * FBm3(U * 6.f + NSeed, 2);
		});

	// ---- Head: blunt, narrowing to a snout, with a blowhole and an eye spot ----
	AppendEllipsoid(Out, FTransform(FRotator(-3.f, 0.f, 0.f), FVector(600.f, 0.f, 18.f)), FVector(250.f, 140.f, 126.f), 40, 22,
		[&](const FVector& U, const FVector& P)
		{
			const bool Eye = U.X > 0.28f && FMath::Abs(U.Y) > 0.62f && U.Z > 0.02f && U.Z < 0.42f;
			const bool Blow = U.Z > 0.82f && U.X > 0.05f && U.X < 0.45f && FMath::Abs(U.Y) < 0.24f;
			return FLinearColor(Eye ? 1.f : (Blow ? 0.6f : 0.f), Phase(P.X + 600.f), 0.f, 1.f);
		},
		[&](const FVector& U)
		{
			const float Snout = U.X > 0.45f ? -0.30f * (U.X - 0.45f) : 0.f;
			return Snout + 0.02f * FBm3(U * 4.f + NSeed * 1.3f, 2);
		});

	// ---- Lower jaw: the glowing throat. This is the part that shows in a lunge. ----
	AppendEllipsoid(Out, FTransform(FRotator(4.f, 0.f, 0.f), FVector(578.f, 0.f, -82.f)), FVector(236.f, 124.f, 62.f), 34, 16,
		[&](const FVector& U, const FVector& P)
		{
			float Mark = 0.f;
			const float Groove = FMath::Sin(P.Y * 0.10f + Seed * 0.7f);
			if (U.Z < 0.35f && Groove > 0.35f) Mark = 1.f;
			return FLinearColor(Mark, Phase(P.X + 578.f), 0.f, 1.f);
		},
		[&](const FVector& U) { return U.X > 0.5f ? -0.24f * (U.X - 0.5f) : 0.f; });

	// ---- Peduncle: the narrow tail stock the flukes hang off ----
	AppendEllipsoid(Out, FTransform(FVector(-600.f, 0.f, -6.f)), FVector(215.f, 56.f, 84.f), 26, 14,
		[&](const FVector&, const FVector& P) { return FLinearColor(0.f, Phase(P.X - 600.f), 0.f, 1.f); },
		[&](const FVector& U) { return -0.22f * FMath::Max(0.f, -U.X); });

	// ---- Flukes: two flat lobes, swept back, glowing along the trailing edge ----
	for (int32 side = -1; side <= 1; side += 2)
	{
		AppendEllipsoid(Out, FTransform(FRotator(0.f, side * -22.f, 0.f), FVector(-772.f, side * 168.f, -10.f)),
			FVector(118.f, 200.f, 19.f), 22, 12,
			[&](const FVector& U, const FVector&)
			{
				// Trailing (rear) edge lights up; the leading edge stays dark.
				const float Edge = FMath::Clamp(-U.X, 0.f, 1.f);
				return FLinearColor(Edge > 0.55f ? Edge : 0.f, 0.f, 0.f, 1.f);
			},
			[&](const FVector& U) { return -0.18f * FMath::Abs(U.Y); });
	}

	// ---- Dorsal ridge + a small hooked fin: what the camera sees when it surfaces ----
	AppendEllipsoid(Out, FTransform(FVector(-150.f, 0.f, 150.f)), FVector(300.f, 34.f, 62.f), 22, 10,
		[&](const FVector&, const FVector& P) { return FLinearColor(0.f, Phase(P.X - 150.f), 0.f, 1.f); },
		[&](const FVector&) { return 0.f; });
	AppendEllipsoid(Out, FTransform(FRotator(-24.f, 0.f, 0.f), FVector(-210.f, 0.f, 226.f)), FVector(64.f, 15.f, 84.f), 18, 10,
		[&](const FVector& U, const FVector&) { return FLinearColor(U.Z > 0.55f ? 0.5f : 0.f, 0.55f, 0.f, 1.f); },
		[&](const FVector&) { return 0.f; });

	// ---- Pectoral flippers: long, swept back and slightly down ----
	for (int32 side = -1; side <= 1; side += 2)
	{
		AppendEllipsoid(Out, FTransform(FRotator(-10.f, side * 125.f, 0.f), FVector(248.f, side * 282.f, -104.f)),
			FVector(196.f, 62.f, 25.f), 20, 10,
			[&](const FVector& U, const FVector&) { return FLinearColor(U.X < -0.7f ? 0.4f : 0.f, 0.75f, 0.f, 1.f); },
			[&](const FVector& U) { return -0.16f * FMath::Max(0.f, U.X); });
	}

	ComputeNormals(Out);
}

} // namespace SWProc
