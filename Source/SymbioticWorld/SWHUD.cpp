#include "SWHUD.h"
#include "SWWorldManager.h"
#include "SWAgent.h"
#include "SWScientistAvatar.h"
#include "SWResourcePatch.h"
#include "SWPlayerController.h"
#include "SWLearner.h"
#include "SWProcMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

namespace
{
	const FLinearColor ColText(0.92f, 0.94f, 0.96f);
	const FLinearColor ColDim(0.60f, 0.66f, 0.72f);
	const FLinearColor ColLumen(0.25f, 0.90f, 1.00f);   // cyan  = Lumen / information
	const FLinearColor ColTecton(1.00f, 0.62f, 0.18f);  // amber = Tecton / terrain
	const FLinearColor ColGreen(0.35f, 0.90f, 0.40f);   // green = resource recovery
	const FLinearColor ColRed(1.00f, 0.30f, 0.25f);     // red   = perturbation
	// Glass: the scene stays visible through the panel body; the header strip is a little
	// darker so title lines stay legible over a bright sky band.
	const FLinearColor ColPanel(0.02f, 0.03f, 0.05f, 0.38f);
	const FLinearColor ColHeader(0.02f, 0.03f, 0.05f, 0.55f);
	const FLinearColor ColBorder(1.00f, 1.00f, 1.00f, 0.25f);
	const FLinearColor ColTrack(0.80f, 0.86f, 0.92f, 0.25f);
	const FLinearColor ColShadow(0.00f, 0.00f, 0.00f, 0.60f);

	FString Fmt(float V, int32 Decimals = 2) { return FString::Printf(TEXT("%.*f"), Decimals, V); }
	FString Thousands(int32 V)
	{
		FString S = FString::FromInt(FMath::Abs(V));
		for (int32 i = S.Len() - 3; i > 0; i -= 3) S.InsertAt(i, TEXT(","));
		return (V < 0 ? TEXT("-") : TEXT("")) + S;
	}
}

ASWHUD::ASWHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ASWHUD::DrawRect(float X, float Y, float W, float H, const FLinearColor& Fill) const
{
	if (!Canvas) return;
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Fill);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ASWHUD::DrawPanel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor* Accent, float HeaderH) const
{
	// Header strip and body do not overlap, so each has exactly its own alpha.
	HeaderH = FMath::Clamp(HeaderH, 0.f, H);
	if (HeaderH > 0.f) DrawRect(X, Y, W, HeaderH, ColHeader);
	if (H - HeaderH > 0.f) DrawRect(X, Y + HeaderH, W, H - HeaderH, Fill);
	// 1 px light border.
	DrawRect(X, Y, W, 1.f, ColBorder);
	DrawRect(X, Y + H - 1.f, W, 1.f, ColBorder);
	DrawRect(X, Y, 1.f, H, ColBorder);
	DrawRect(X + W - 1.f, Y, 1.f, H, ColBorder);
	if (Accent) DrawRect(X, Y, W, 2.f, *Accent);          // thin accent rule along the top
}

float ASWHUD::DrawLine(float X, float Y, const FString& Text, const FLinearColor& Color, float Scale) const
{
	if (!Canvas || !Font) return Y;
	// Drawn twice: a 1 px black drop shadow at 60% under the coloured string keeps
	// text readable where the glass panel sits over a bright sky band.
	const FText T = FText::FromString(Text);
	FCanvasTextItem Shadow(FVector2D(X + 1.f, Y + 1.f), T, Font, ColShadow);
	Shadow.Scale = FVector2D(Scale, Scale);
	Canvas->DrawItem(Shadow);
	FCanvasTextItem Item(FVector2D(X, Y), T, Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Canvas->DrawItem(Item);
	return Y + LineHeight * Scale;
}

float ASWHUD::TextWidth(const FString& Text, float Scale) const
{
	if (!Canvas || !Font) return 0.f;
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Font, Text, W, H);
	return W * Scale;
}

void ASWHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	if (!Font && GEngine) Font = GEngine->GetSmallFont();
	if (!Font) return;
	LineHeight = 15.f;

	ASWWorldManager* M = ASWWorldManager::Get(GetWorld());
	if (!M)
	{
		DrawLine(20.f, 20.f, TEXT("Symbiotic World: no world manager in level"), ColRed, 1.3f);
		return;
	}
	// The scenario caption is drawn before the Look.bShowHUD gate below: hiding the HUD is for clean
	// framing, the caption is the take's narration and has to survive it.
	DrawCaption(*M);
	// Look.bShowHUD=0: draw nothing else at all (clean framing for a recorded take, docs/CONTROL_FILE.md).
	// Visual only and reversible with "set Look.bShowHUD=1"; nothing below this line touches the sim.
	if (!M->GetLook().bShowHUD) return;
	const float SX = Canvas->SizeX, SY = Canvas->SizeY;

	DrawScientistTags(*M);   // first, so the panels draw over a tag that lands behind them
	DrawTitle(*M);
	DrawStatCards(*M);

	// Left column.
	const float LeftW = 330.f;
	DrawSpeciesPanel(*M, ESWSpecies::Lumen, 20.f, 96.f, LeftW);
	DrawEvolutionStrip(*M, 20.f, 96.f + 232.f + 10.f, LeftW);   // 124 px tall
	// Free band in the left column between the evolution strip (ends at 462) and the minimap panel
	// (top at 625 on a 900 px tall window, lower on a taller one): the lab's own reports.
	if (M->GetLook().bShowLabPanel && M->GetPolicyConnectedCount() > 0)
	{
		DrawLabPanel(*M, 20.f, 472.f, LeftW);
	}
	const float MapSize = 280.f;
	const float MapH = MapSize * SWArenaHalfY(M->GetSettings()) / FMath::Max(M->GetSettings().WorldHalfSize, 1.f);   // the map keeps the arena's aspect
	DrawMinimap(*M, 20.f, SY - (MapH + 62.f) - 20.f, MapSize);   // the panel is the map plus a 62 px header and legend

	// Right column.
	const float RightW = 360.f;
	const float RX = SX - RightW - 20.f;
	float RY = 96.f;
	if (ASWAgent* Sel = M->GetSelectedAgent())
	{
		if (IsValid(Sel) && Sel->IsAlive())
		{
			DrawSelectionMarker(*Sel);
			DrawInspector(*M, *Sel, RX, RY, RightW);
			RY += 403.f + 10.f;
		}
	}
	else
	{
		DrawPanel(RX, RY, RightW, 40.f, ColPanel, &ColDim, 20.f);
		DrawLine(RX + 10.f, RY + 8.f, TEXT("INSPECTOR"), ColDim);
		DrawLine(RX + 10.f, RY + 22.f, TEXT("click an organism, or press Tab"), ColText);
		RY += 50.f;
	}
	DrawSpeciesPanel(*M, ESWSpecies::Tecton, RX, RY, RightW);

	const ASWPlayerController* PC = Cast<ASWPlayerController>(GetOwningPlayerController());
	if (!PC || PC->IsHelpVisible()) DrawHelp(SX - 470.f, SY - 150.f);   // 130 px tall: with an organism selected the Tecton panel ends 9 px above

	if (M->IsPaused())
	{
		DrawLine(SX * 0.5f - 40.f, 70.f, TEXT("PAUSED"), ColRed, 1.6f);
	}
	if (M->IsDrought())
	{
		// While the drought is on, predation is paused (Settings.bLeviathanPauseInDrought),
		// so say so rather than leaving the viewer to wonder why the kills stopped.
		const bool bPredationPaused = M->GetSettings().bLeviathan && M->GetSettings().bLeviathanPauseInDrought;
		const float W = bPredationPaused ? 430.f : 320.f;
		DrawPanel(SX * 0.5f - W * 0.5f, SY - 66.f, W, 34.f, FLinearColor(0.45f, 0.08f, 0.05f, 0.85f), &ColRed);
		DrawLine(SX * 0.5f - W * 0.5f + 14.f, SY - 58.f,
			bPredationPaused ? TEXT("PERTURBATION ACTIVE:  DROUGHT   (predation paused)")
			                 : TEXT("PERTURBATION ACTIVE:  DROUGHT"), ColText, 1.2f);
	}
}

void ASWHUD::DrawCaption(const ASWWorldManager& M)
{
	// control "caption=<text>" (docs/CONTROL_FILE.md). Purely a drawing layer: no sim state is read except
	// the text and the wall-clock age, and a paused sim keeps its title because the age is wall time.
	const FSWLookSettings& L = M.GetLook();
	if (!L.bShowCaptions) return;
	const FString Text = M.GetCaptionText();
	if (Text.IsEmpty()) return;
	const float Duration = FMath::Max(L.CaptionSeconds, 0.f);
	const float Age = M.GetCaptionAgeSeconds();
	if (Age < 0.f || Age >= Duration) return;
	const float FadeSeconds = 0.5f;
	const float Remaining = Duration - Age;
	const float Alpha = FMath::Clamp(Remaining / FadeSeconds, 0.f, 1.f);   // full, then out over the last half second

	const float SX = Canvas->SizeX, SY = Canvas->SizeY;
	// Big enough to read in a recorded frame at any window size, shrunk to fit a long line.
	float Scale = 3.0f * FMath::Clamp(SY / 900.f, 0.7f, 2.0f);
	const float MaxW = FMath::Max(SX - 120.f, 100.f);
	const float RawW = TextWidth(Text, Scale);
	if (RawW > MaxW && RawW > 1.f) Scale *= MaxW / RawW;
	const float W = TextWidth(Text, Scale);
	const float H = LineHeight * Scale;
	const float X = (SX - W) * 0.5f;
	// Lower third, above the KEYS panel (SY - 150) and the drought banner (SY - 66), below the stat cards.
	const float Y = SY * 0.66f - H * 0.5f;
	const float PadX = 30.f, PadY = 16.f;

	DrawRect(X - PadX, Y - PadY, W + 2.f * PadX, H + 2.f * PadY, FLinearColor(0.02f, 0.03f, 0.05f, 0.62f * Alpha));
	DrawRect(X - PadX, Y - PadY, W + 2.f * PadX, 2.f, FLinearColor(ColLumen.R, ColLumen.G, ColLumen.B, 0.85f * Alpha));

	const FText T = FText::FromString(Text);
	FCanvasTextItem Shadow(FVector2D(X + 2.f, Y + 2.f), T, Font, FLinearColor(0.f, 0.f, 0.f, 0.70f * Alpha));
	Shadow.Scale = FVector2D(Scale, Scale);
	Shadow.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Shadow);
	FCanvasTextItem Item(FVector2D(X, Y), T, Font, FLinearColor(ColText.R, ColText.G, ColText.B, Alpha));
	Item.Scale = FVector2D(Scale, Scale);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void ASWHUD::DrawTitle(const ASWWorldManager& M)
{
	const FSWRunSettings& S = M.GetSettings();
	// The run line is already close to the panel's 330 px, so the external-policy and control-file
	// suffixes get their own line and the panel grows to hold it: appended, they ran out of the
	// panel and underneath the GENERATION stat card.
	const bool bHasSuffix = M.HasPolicyServers() || M.GetControlCommandsExecuted() > 0;
	DrawPanel(20.f, 14.f, 330.f, bHasSuffix ? 88.f : 70.f, ColPanel, &ColLumen, 28.f);
	float Y = DrawLine(30.f, 20.f, TEXT("SYMBIOTIC WORLD"), ColText, 1.45f);
	Y = DrawLine(30.f, Y - 2.f, TEXT("Evolution doesn't stop at deployment."), ColDim);
	FString Line = FString::Printf(TEXT("mode %s   seed %d   t %.0f s   %.0fx   %.1f ms"),
		SWModeName(S.Mode), S.Seed, M.GetSimTime(), M.GetTimeScale(), M.GetLastStepMs());
	// Predation is NOT appended here either: it gets its own stat card.
	Y = DrawLine(30.f, Y + 2.f, Line, ColDim);
	if (bHasSuffix)
	{
		FString Suffix;
		if (M.HasPolicyServers())
		{
			// external organisms / total (docs/POLICY_API.md); connection state is in the log
			Suffix = FString::Printf(TEXT("ext %d/%d"), M.GetExternalCount(), M.GetLivingCount());
		}
		if (M.GetControlCommandsExecuted() > 0)
		{
			// at least one control-file command ran this run (docs/CONTROL_FILE.md)
			Suffix += Suffix.IsEmpty() ? TEXT("ctrl") : TEXT("   ctrl");
		}
		DrawLine(30.f, Y, Suffix, ColDim);
	}
}

void ASWHUD::DrawStatCards(const ASWWorldManager& M)
{
	const FSWSpeciesStats& L = M.GetStats(ESWSpecies::Lumen);
	const FSWSpeciesStats& T = M.GetStats(ESWSpecies::Tecton);
	const float FracA = M.GetResourceCapacity(0) > 0.f ? M.GetResourceTotal(0) / M.GetResourceCapacity(0) : 0.f;
	const float FracB = M.GetResourceCapacity(1) > 0.f ? M.GetResourceTotal(1) / M.GetResourceCapacity(1) : 0.f;
	const float Res = 0.5f * (FracA + FracB);
	const float Stab = M.GetStability();

	struct FCard { FString Label; FString Value; FString Sub; FLinearColor C; };
	TArray<FCard> Cards;
	Cards.Add({ TEXT("GENERATION"),   Thousands(FMath::Max(L.MaxGeneration, T.MaxGeneration)), TEXT("ongoing evolution"), ColText });
	Cards.Add({ TEXT("POPULATION A"), Thousands(L.N), TEXT("lumen"), ColLumen });
	Cards.Add({ TEXT("POPULATION B"), Thousands(T.N), TEXT("tecton"), ColTecton });
	Cards.Add({ TEXT("RESOURCES"),    FString::Printf(TEXT("%.0f%%"), Res * 100.f), Res > 0.5f ? TEXT("stable") : (Res > 0.25f ? TEXT("strained") : TEXT("depleted")), ColGreen });
	Cards.Add({ TEXT("STABILITY"),    FString::Printf(TEXT("%.0f%%"), Stab * 100.f), Stab > 0.85f ? TEXT("thriving") : (Stab > 0.6f ? TEXT("shifting") : TEXT("turbulent")), M.IsDrought() ? ColRed : ColText });
	// Sixth card only when the predator is enabled, so the default five-card layout is
	// byte-for-byte what it was. Red, matching the drought banner: both are perturbations.
	if (M.GetSettings().bLeviathan)
	{
		// The card names the animal and splits its kills by prey species; deaths.csv carries the same
		// events with cause "predation".
		const int32 NPred = M.GetDeathsPredation();
		const bool bPaused = M.IsDrought() && M.GetSettings().bLeviathanPauseInDrought;
		const FString Split = FString::Printf(TEXT("%d lumen  %d tecton"),
			M.GetDeathsPredation(ESWSpecies::Lumen), M.GetDeathsPredation(ESWSpecies::Tecton));
		Cards.Add({ TEXT("LEVIATHAN"), FString::Printf(TEXT("%s taken"), *Thousands(NPred)),
		            bPaused ? TEXT("paused: drought") : Split, ColRed });
	}

	// Bank cycle card only while the regime is on: green like RESOURCES, because it says where regrowth is.
	if (M.GetSettings().bBankCycle)
	{
		const bool bHeld = M.GetSettings().bBankCyclePauseInDrought && M.IsDrought();
		const int32 Bank = M.GetBankCyclePhaseBank();   // 0 only during the warm-up (a drought pause shows "held")
		const float ToSwitch = M.GetBankCycleSecondsToSwitch();
		const FString Value = bHeld ? TEXT("held") : (Bank == 0 ? TEXT("warm-up") : (Bank > 0 ? TEXT("+Y bank") : TEXT("-Y bank")));
		const FString Sub = bHeld ? TEXT("drought: both regrow") : (Bank == 0 ? FString::Printf(TEXT("both regrow, %.0f s"), FMath::Max(ToSwitch, 0.f))
		                                                          : FString::Printf(TEXT("regrows, switch %.0f s"), FMath::Max(ToSwitch, 0.f)));
		Cards.Add({ TEXT("BANK CYCLE"), Value, Sub, ColGreen });
	}

	const float CardW = 150.f, CardH = 60.f, Gap = 8.f;
	const float Total = Cards.Num() * CardW + (Cards.Num() - 1) * Gap;
	// Keep clear of the 330 px title panel on the left; centre when there is room.
	float X = FMath::Max(370.f, Canvas->SizeX * 0.5f - Total * 0.5f);
	float LeviathanX = -1.f;
	for (const FCard& C : Cards)
	{
		DrawPanel(X, 14.f, CardW, CardH, ColPanel, &C.C, 18.f);
		DrawLine(X + 10.f, 20.f, C.Label, ColDim, 0.9f);
		DrawLine(X + 10.f, 34.f, C.Value, C.C, 1.5f);
		DrawLine(X + 10.f, 58.f, C.Sub, ColDim, 0.85f);
		if (C.Label == TEXT("LEVIATHAN")) LeviathanX = X;
		X += CardW + Gap;
	}
	if (LeviathanX >= 0.f) DrawKillFeed(M, LeviathanX, CardW, 14.f + CardH);
}

FString ASWHUD::Elide(const FString& Text, float W, float Scale) const
{
	const float Full = TextWidth(Text, Scale);
	if (Full <= W || Text.Len() <= 1) return Text;
	// Proportional first guess, then trim until it fits: the small font is near fixed width.
	int32 Keep = FMath::Clamp(FMath::FloorToInt(Text.Len() * W / FMath::Max(Full, 1.f)) - 3, 1, Text.Len());
	while (Keep > 1)
	{
		const FString Cut = Text.Left(Keep) + TEXT("...");
		const float CutW = TextWidth(Cut, Scale);
		if (CutW <= W) return Cut;
		Keep--;
	}
	return Text.Left(1) + TEXT("...");
}

void ASWHUD::DrawKillFeed(const ASWWorldManager& M, float CardX, float CardW, float CardBottom)
{
	// The card keeps the count; this is who was taken, newest first, fading on the wall clock so a
	// paused take keeps the feed on screen. Visual only.
	const FSWLookSettings& L = M.GetLook();
	if (!L.bPredationEffects) return;
	const TArray<FSWKillEvent>& Kills = M.GetKillEvents();
	if (Kills.Num() == 0) return;
	const float Hold = FMath::Max(L.KillFeedSeconds, 0.1f);
	const double Now = FPlatformTime::Seconds();
	const float FeedW = 210.f;
	// Under the card, but never over the inspector column on the right (1600x900 puts the six cards
	// close to it): the feed slides left until it clears that column.
	const float Right = FMath::Min(CardX + CardW, Canvas->SizeX - 396.f);
	const float X = FMath::Max(Right - FeedW, 20.f);
	float Y = CardBottom + 6.f;
	int32 Shown = 0;
	for (int32 i = Kills.Num() - 1; i >= 0 && Shown < 3; --i)
	{
		const float Age = static_cast<float>(Now - Kills[i].WallTime);
		if (Age < 0.f || Age > Hold) continue;
		const float Alpha = FMath::Clamp((Hold - Age) / 0.8f, 0.f, 1.f);   // out over the last 0.8 s
		const FString Line = FString::Printf(TEXT("%s %s taken"), SWSpeciesName(Kills[i].Species), *Kills[i].Label);
		const FLinearColor C = FLinearColor(ColRed.R, ColRed.G, ColRed.B, Alpha);
		DrawRect(X, Y, FeedW, 14.f, FLinearColor(0.02f, 0.03f, 0.05f, 0.42f * Alpha));
		DrawRect(X, Y, 2.f, 14.f, C);
		DrawLine(X + 7.f, Y + 1.f, Elide(Line, FeedW - 14.f, 0.95f), C, 0.95f);
		Y += 16.f;
		Shown++;
	}
}

void ASWHUD::DrawLabPanel(const ASWWorldManager& M, float X, float Y, float W)
{
	// The bridge's own "log" lines (docs/POLICY_API.md): what the Symbiotic Lab is doing right now.
	// Drawn only while a policy server is connected, so a run without a bridge keeps the old layout.
	const TArray<FSWLabLine>& Lines = M.GetLabLines();
	const float H = 34.f + 6.f * 13.f;
	DrawPanel(X, Y, W, H, ColPanel, &ColLumen, 26.f);
	float y = DrawLine(X + 10.f, Y + 8.f, TEXT("SYMBIOTIC LAB"), ColText, 1.05f);
	y += 4.f;
	if (Lines.Num() == 0)
	{
		DrawLine(X + 10.f, y, TEXT("bridge connected, waiting for its first report"), ColDim, 0.85f);
		return;
	}
	for (const FSWLabLine& Line : Lines)
	{
		const FString Text = FString::Printf(TEXT("%3.0fs  %s"), Line.SimTime, *Line.Text);
		DrawLine(X + 10.f, y, Elide(Text, W - 20.f, 0.85f), ColDim, 0.85f);
		y += 13.f;
	}
}

void ASWHUD::DrawSpeciesPanel(const ASWWorldManager& M, ESWSpecies S, float X, float Y, float W)
{
	const FSWSpeciesStats& St = M.GetStats(S);
	const FLinearColor C = S == ESWSpecies::Lumen ? ColLumen : ColTecton;
	const float H = 232.f;
	DrawPanel(X, Y, W, H, ColPanel, &C, 40.f);
	float y = Y + 8.f;
	const float x = X + 10.f;
	y = DrawLine(x, y, S == ESWSpecies::Lumen ? TEXT("SPECIES A  -  LUMEN") : TEXT("SPECIES B  -  TECTON"), C, 1.2f);
	y = DrawLine(x, y - 1.f, S == ESWSpecies::Lumen ? TEXT("small. connected. catalytic.") : TEXT("massive. transformative. nurturing."), ColDim, 0.9f);
	y += 4.f;
	y = DrawLine(x, y, FString::Printf(TEXT("population  %d      generation  mean %.1f  max %d"), St.N, St.MeanGeneration, St.MaxGeneration), ColText);
	y = DrawLine(x, y, FString::Printf(TEXT("energy mean %.0f    inherited: alpha %.3f  eps %.3f  s %.2f  e %.2f"), St.MeanEnergy, St.MeanAlpha, St.MeanEps, St.MeanSocial, St.MeanEnv), ColText);
	y += 6.f;
	y = DrawLine(x, y, TEXT("BEHAVIOUR DISTRIBUTION  (current actions)"), ColDim, 0.9f);

	int32 Total = 0;
	for (int32 A = 0; A < SW_NUM_ACTIONS; ++A) Total += St.ActionCounts[A];
	const float BarX = x + 70.f, BarW = W - 130.f;
	for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
	{
		const float Share = Total > 0 ? St.ActionCounts[A] / (float)Total : 0.f;
		DrawLine(x, y, SWActionName(static_cast<ESWAction>(A)), ColText, 0.95f);
		DrawRect(BarX, y + 4.f, BarW, 7.f, ColTrack);
		DrawRect(BarX, y + 4.f, FMath::Max(1.f, Share * BarW), 7.f, C);
		DrawLine(BarX + BarW + 6.f, y, FString::Printf(TEXT("%2.0f%%"), Share * 100.f), C, 0.95f);
		y += 14.f;
	}
}

void ASWHUD::DrawEvolutionStrip(const ASWWorldManager& M, float X, float Y, float W)
{
	DrawPanel(X, Y, W, 124.f, ColPanel, &ColDim, 24.f);
	float y = Y + 8.f;
	const float x = X + 10.f;
	y = DrawLine(x, y, TEXT("META-PARAMETERS  (population mean +/- sd, inherited)"), ColDim, 0.9f);
	auto Bar = [&](const TCHAR* Label, float Mean, float Sd, float Min, float Max, const FLinearColor& C)
	{
		const float BarX = x + 60.f, BarW = W - 130.f;
		DrawLine(x, y, Label, C, 0.95f);
		DrawRect(BarX, y + 4.f, BarW, 7.f, ColTrack);
		const float Lo = FMath::Clamp((Mean - Sd - Min) / (Max - Min), 0.f, 1.f);
		const float Hi = FMath::Clamp((Mean + Sd - Min) / (Max - Min), 0.f, 1.f);
		DrawRect(BarX + Lo * BarW, y + 4.f, FMath::Max(2.f, (Hi - Lo) * BarW), 7.f, C * FLinearColor(1, 1, 1, 0.45f));
		const float Mid = FMath::Clamp((Mean - Min) / (Max - Min), 0.f, 1.f);
		DrawRect(BarX + Mid * BarW - 1.f, y + 2.f, 3.f, 11.f, C);
		DrawLine(BarX + BarW + 6.f, y, Fmt(Mean, 3), C, 0.95f);
		y += 14.f;
	};
	const FSWSpeciesStats& L = M.GetStats(ESWSpecies::Lumen);
	const FSWSpeciesStats& T = M.GetStats(ESWSpecies::Tecton);
	Bar(TEXT("L alpha"), L.MeanAlpha, L.SdAlpha, FSWGenome::AlphaMin, FSWGenome::AlphaMax, ColLumen);
	Bar(TEXT("L eps"),   L.MeanEps,   L.SdEps,   FSWGenome::EpsMin,   FSWGenome::EpsMax,   ColLumen);
	Bar(TEXT("T alpha"), T.MeanAlpha, T.SdAlpha, FSWGenome::AlphaMin, FSWGenome::AlphaMax, ColTecton);
	Bar(TEXT("T eps"),   T.MeanEps,   T.SdEps,   FSWGenome::EpsMin,   FSWGenome::EpsMax,   ColTecton);
	Bar(TEXT("L env e"), L.MeanEnv,   L.SdEnv,   FSWGenome::EnvMin,   FSWGenome::EnvMax,   ColLumen);
	Bar(TEXT("T env e"), T.MeanEnv,   T.SdEnv,   FSWGenome::EnvMin,   FSWGenome::EnvMax,   ColTecton);
}

void ASWHUD::DrawMinimap(const ASWWorldManager& M, float X, float Y, float Size)
{
	const FSWRunSettings& S = M.GetSettings();
	const FSWLookSettings& L = M.GetLook();
	const float HalfX = S.WorldHalfSize, HalfY = SWArenaHalfY(S);
	const float SizeY = Size * HalfY / FMath::Max(HalfX, 1.f);   // the map keeps the arena's aspect (X along the valley)
	DrawPanel(X, Y, Size + 20.f, SizeY + 62.f, ColPanel, &ColGreen, 26.f);
	float y = DrawLine(X + 10.f, Y + 8.f, TEXT("ECOSYSTEM FLOW"), ColText, 1.05f);
	const float MX = X + 10.f, MY = y + 4.f;
	// World -> map: x right, y down.
	auto ToMap = [&](float WX, float WY) { return FVector2D(MX + (WX + HalfX) / (2.f * HalfX) * Size, MY + (WY + HalfY) / (2.f * HalfY) * SizeY); };

	const FSWTraceField& FX = M.GetTraceX();
	const FSWTraceField& FY = M.GetTraceY();
	const int32 N = FMath::Max(FX.Cells(), 1);
	const float CellW = Size / N, CellH = SizeY / N;
	// The water mask is fixed for a run (terrain and arena are built once): sample it on the first
	// frame of each run instead of N*N terrain evaluations per frame.
	if (MinimapWaterCells != N || MinimapWaterRun != M.GetRunId() || MinimapWaterLevel != L.WaterLevel || MinimapWater.Num() != N * N)
	{
		MinimapWater.SetNumUninitialized(N * N);
		for (int32 j = 0; j < N; ++j)
		{
			for (int32 i = 0; i < N; ++i)
			{
				const float WX = -HalfX + (i + 0.5f) * (2.f * HalfX / N);
				const float WY = -HalfY + (j + 0.5f) * (2.f * HalfY / N);
				MinimapWater[j * N + i] = SWProc::TerrainHeight(L, WX, WY) < L.WaterLevel ? 1 : 0;
			}
		}
		MinimapWaterCells = N;
		MinimapWaterRun = M.GetRunId();
		MinimapWaterLevel = L.WaterLevel;
	}
	// Ground / water base, then the two fields, cell by cell.
	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const bool bWater = MinimapWater[j * N + i] != 0;
			const FLinearColor Base = bWater ? FLinearColor(0.10f, 0.28f, 0.40f, 0.9f) : FLinearColor(0.07f, 0.09f, 0.07f, 0.9f);
			const float PX = MX + i * CellW, PY = MY + j * CellH;
			DrawRect(PX, PY, CellW + 0.5f, CellH + 0.5f, Base);
			const float TX = FMath::Clamp(FX.At(i, j) / FMath::Max(S.TraceMax, 0.01f), 0.f, 1.f);
			const float TY = FMath::Clamp(FY.At(i, j) / FMath::Max(S.TraceMax, 0.01f), 0.f, 1.f);
			if (TY > 0.02f) DrawRect(PX, PY, CellW + 0.5f, CellH + 0.5f, ColTecton * FLinearColor(1, 1, 1, 0.75f * TY));
			if (TX > 0.02f) DrawRect(PX, PY, CellW + 0.5f, CellH + 0.5f, ColLumen * FLinearColor(1, 1, 1, 0.75f * TX));
		}
	}
	// Resource nodes and organisms.
	for (const ASWResourcePatch* P : M.GetPatches())
	{
		if (!IsValid(P)) continue;
		const FVector2D Q = ToMap(P->GetActorLocation().X, P->GetActorLocation().Y);
		const float Frac = P->GetCapacity() > 0.f ? P->GetStock() / P->GetCapacity() : 0.f;
		DrawRect(Q.X - 2.f, Q.Y - 2.f, 4.f, 4.f, (P->GetResourceType() == 0 ? ColGreen : FLinearColor(0.9f, 0.85f, 0.3f)) * FLinearColor(1, 1, 1, 0.35f + 0.65f * Frac));
	}
	for (const ASWAgent* A : M.GetAgents())
	{
		if (!IsValid(A) || !A->IsAlive()) continue;
		const FVector2D Q = ToMap(A->GetActorLocation().X, A->GetActorLocation().Y);
		const bool bLumen = A->GetSpecies() == ESWSpecies::Lumen;
		DrawRect(Q.X - 1.5f, Q.Y - 1.5f, bLumen ? 3.f : 5.f, bLumen ? 3.f : 5.f, bLumen ? ColLumen : ColTecton);
	}
	if (const ASWAgent* Sel = M.GetSelectedAgent())
	{
		if (IsValid(Sel))
		{
			const FVector2D Q = ToMap(Sel->GetActorLocation().X, Sel->GetActorLocation().Y);
			DrawRect(Q.X - 5.f, Q.Y - 5.f, 10.f, 1.f, ColText); DrawRect(Q.X - 5.f, Q.Y + 4.f, 10.f, 1.f, ColText);
			DrawRect(Q.X - 5.f, Q.Y - 5.f, 1.f, 10.f, ColText); DrawRect(Q.X + 4.f, Q.Y - 5.f, 1.f, 10.f, ColText);
		}
	}
	// Recent predation kills: a red cross where the organism was taken, for Look.KillFeedSeconds
	// (wall clock, like the plume and the kill feed). Visual only.
	if (L.bPredationEffects)
	{
		const float Hold = FMath::Max(L.KillFeedSeconds, 0.1f);
		const double Now = FPlatformTime::Seconds();
		for (const FSWKillEvent& K : M.GetKillEvents())
		{
			const float Age = static_cast<float>(Now - K.WallTime);
			if (Age < 0.f || Age > Hold) continue;
			const float Alpha = FMath::Clamp((Hold - Age) / 0.8f, 0.f, 1.f);
			const FVector2D Q = ToMap(K.Location.X, K.Location.Y);
			const FLinearColor C(ColRed.R, ColRed.G, ColRed.B, Alpha);
			DrawRect(Q.X - 4.f, Q.Y - 1.f, 9.f, 2.f, C);
			DrawRect(Q.X - 1.f, Q.Y - 4.f, 2.f, 9.f, C);
		}
	}
	// Legend.
	float ly = MY + SizeY + 6.f;
	const float lx = MX;
	auto Key = [&](float x0, const FLinearColor& C, const TCHAR* Label)
	{
		DrawRect(x0, ly + 4.f, 8.f, 8.f, C);
		DrawLine(x0 + 12.f, ly, Label, ColDim, 0.85f);
	};
	Key(lx, ColLumen, TEXT("Lumen / trace X"));
	Key(lx + 110.f, ColTecton, TEXT("Tecton / trace Y"));
	Key(lx + 220.f, FLinearColor(0.10f, 0.28f, 0.40f), TEXT("water"));
	ly += 14.f;
	DrawLine(lx, ly, FString::Printf(TEXT("trace X mean %.3f    trace Y mean %.3f"), FX.Mean(), FY.Mean()), ColDim, 0.85f);
}

void ASWHUD::DrawSelectionMarker(const ASWAgent& A)
{
	if (!Canvas) return;
	const FVector Loc = A.GetActorLocation() + FVector(0.f, 0.f, A.GetVisualHeight() + 20.f);   // clears the authored bodies too
	const FVector Screen = Canvas->Project(Loc);
	if (Screen.Z <= 0.f) return;   // behind camera
	const FLinearColor C = A.GetSpecies() == ESWSpecies::Lumen ? ColLumen : ColTecton;
	const float S = 7.f;
	FCanvasLineItem L1(FVector2D(Screen.X - S, Screen.Y), FVector2D(Screen.X, Screen.Y - S)); L1.SetColor(C); Canvas->DrawItem(L1);
	FCanvasLineItem L2(FVector2D(Screen.X, Screen.Y - S), FVector2D(Screen.X + S, Screen.Y)); L2.SetColor(C); Canvas->DrawItem(L2);
	FCanvasLineItem L3(FVector2D(Screen.X + S, Screen.Y), FVector2D(Screen.X, Screen.Y + S)); L3.SetColor(C); Canvas->DrawItem(L3);
	FCanvasLineItem L4(FVector2D(Screen.X, Screen.Y + S), FVector2D(Screen.X - S, Screen.Y)); L4.SetColor(C); Canvas->DrawItem(L4);
	DrawLine(Screen.X + 10.f, Screen.Y - 8.f, A.GetLabel(), C);
}

void ASWHUD::DrawScientistTags(const ASWWorldManager& M)
{
	// A 1.8 m body is ~14 px tall from the start camera and a world-space label only a few px, so the
	// field team is named in screen space: the scientist's colour, a chevron pointing down at the head.
	if (!Canvas) return;
	const float Margin = 40.f;
	struct FTagLabel { FVector2D At; FString Name; FLinearColor C; };
	TArray<FTagLabel> Labels;
	for (const ASWScientistAvatar* A : M.GetScientistAvatars())
	{
		if (!IsValid(A) || A->IsHidden()) continue;   // hidden = stale bridge
		const FVector Screen = Canvas->Project(A->GetTagAnchor());
		if (Screen.Z <= 0.f) continue;   // behind camera
		if (Screen.X < -Margin || Screen.X > Canvas->SizeX + Margin || Screen.Y < -Margin || Screen.Y > Canvas->SizeY + Margin) continue;
		const FLinearColor& C = A->GetTagColor();
		const float S = 5.f;
		FCanvasLineItem L1(FVector2D(Screen.X - S, Screen.Y - S), FVector2D(Screen.X, Screen.Y));
		L1.LineThickness = 2.f; L1.SetColor(C); Canvas->DrawItem(L1);
		FCanvasLineItem L2(FVector2D(Screen.X, Screen.Y), FVector2D(Screen.X + S, Screen.Y - S));
		L2.LineThickness = 2.f; L2.SetColor(C); Canvas->DrawItem(L2);
		// The label is placed later: at camp the whole team stands in one spot, every tag projects
		// to nearly the same pixel, and the names drew on top of each other into an unreadable pile.
		Labels.Add({ FVector2D(Screen.X, Screen.Y - S - LineHeight - 2.f), A->GetScientistName(), C });
	}

	// Greedy declutter: nearest-to-camera (lowest on screen) keeps its slot, anything that would
	// overlap an already-placed label is stacked upward one line at a time.
	Labels.Sort([](const FTagLabel& L, const FTagLabel& R) { return L.At.Y > R.At.Y; });
	struct FRect { float X0, Y0, X1, Y1; };
	TArray<FRect> Taken;
	for (const FTagLabel& L : Labels)
	{
		const float W = TextWidth(L.Name);
		FVector2D P(L.At.X - 0.5f * W, L.At.Y);
		for (int32 Guard = 0; Guard < 12; ++Guard)
		{
			const FRect Box{ P.X - 2.f, P.Y - 1.f, P.X + W + 2.f, P.Y + LineHeight + 1.f };
			bool bOverlaps = false;
			for (const FRect& Q : Taken)
			{
				if (Box.X0 < Q.X1 && Q.X0 < Box.X1 && Box.Y0 < Q.Y1 && Q.Y0 < Box.Y1) { bOverlaps = true; break; }
			}
			if (!bOverlaps) { Taken.Add(Box); break; }
			P.Y -= LineHeight + 2.f;
		}
		DrawLine(P.X, P.Y, L.Name, L.C);
	}
}

void ASWHUD::DrawInspector(const ASWWorldManager& M, const ASWAgent& A, float X, float Y, float W)
{
	const FLinearColor C = A.GetSpecies() == ESWSpecies::Lumen ? ColLumen : ColTecton;
	const FSWGenome& G = A.GetGenome();
	const FSWContextualBandit& B = A.GetBandit();
	const FSWContextualBandit& B0 = A.GetInitialBandit();

	DrawPanel(X, Y, W, 403.f, ColPanel, &C, 28.f);
	float y = Y + 8.f;
	const float x = X + 10.f;
	y = DrawLine(x, y, FString::Printf(TEXT("%s  %s"), SWSpeciesName(A.GetSpecies()), *A.GetLabel()), C, 1.25f);
	y = DrawLine(x, y, FString::Printf(TEXT("gen %d   parent %s   age %.0f / %.0f s"),
		A.GetGeneration(), A.GetParentId() < 0 ? TEXT("founder") : *FString::Printf(TEXT("#%d"), A.GetParentId()),
		A.GetAge(), A.GetParams().MaxAge), ColDim);
	y = DrawLine(x, y, FString::Printf(TEXT("energy %.1f / %.0f   bin %s   decisions %d   crossings %d"),
		A.GetEnergy(), A.GetParams().MaxEnergy,
		A.GetCurrentContext() == 0 ? TEXT("LOW") : (A.GetCurrentContext() == 1 ? TEXT("MID") : TEXT("HIGH")),
		A.GetDecisionCount(), A.GetRiverCrossings()), ColText);
	y = DrawLine(x, y, FString::Printf(TEXT("action %s%s   last reward %+.3f   trace X %.2f  Y %.2f"),
		SWActionName(A.GetCurrentAction()), A.WasLastExplored() ? TEXT(" (explore)") : TEXT(""), A.GetLastReward(),
		A.GetLocalTraceX(), A.GetLocalTraceY()), ColText);
	// Who chooses among the feasible actions: this organism's own bandit, or an external policy server.
	{
		const int32 PS = A.GetPolicyServer();
		const bool bExt = PS >= 0 && PS < M.GetPolicyClient().NumServers();
		y = DrawLine(x, y, bExt ? FString::Printf(TEXT("policy: external %s"), *M.GetPolicyClient().GetServer(PS).Name) : FString(TEXT("policy: builtin")), bExt ? C : ColDim);
	}

	y += 8.f;
	y = DrawLine(x, y, TEXT("INHERITED  (fixed for this lifetime)"), ColDim);
	const float AlphaEff = A.EffectiveAlpha();
	y = DrawLine(x, y, FString::Printf(TEXT("  learning rate  alpha   %.3f%s"), G.Alpha, AlphaEff <= 0.f ? TEXT("   [mode A: forced 0]") : TEXT("")), C);
	y = DrawLine(x, y, FString::Printf(TEXT("  exploration    eps     %.3f"), G.Epsilon), C);
	y = DrawLine(x, y, FString::Printf(TEXT("  social         s       %.3f"), G.Social), C);
	y = DrawLine(x, y, FString::Printf(TEXT("  env effect     e       %.3f   (deposit x%.2f)"), G.EnvEffect, G.EnvScale()), C);

	y += 8.f;
	y = DrawLine(x, y, TEXT("LEARNED POLICY  Q[bin][action]   initial -> now"), ColDim);
	y = DrawLine(x, y, TEXT("  action     LOW            MID            HIGH"), ColDim);
	const int32 Cur = A.GetCurrentContext();
	const uint32 Mask = A.GetLastFeasibleMask();
	const ESWAction GreedyNow = B.Greedy(Cur, Mask);
	auto Tidy = [](float V) { return FMath::Abs(V) < 0.005f ? 0.f : V; };
	for (int32 Act = 0; Act < SW_NUM_ACTIONS; ++Act)
	{
		const ESWAction E = static_cast<ESWAction>(Act);
		const bool bFeasible = FSWContextualBandit::IsFeasible(Mask, E);
		FString Row = FString::Printf(TEXT("%s %-9s"), bFeasible ? TEXT(" ") : TEXT("x"), SWActionName(E));
		for (int32 Bin = 0; Bin < SW_NUM_ENERGY_BINS; ++Bin)
		{
			const float V0 = Tidy(B0.Value(Bin, E));
			const float V1 = Tidy(B.Value(Bin, E));
			Row += FString::Printf(TEXT("%s%.2f>%+.2f%s "), Bin == Cur ? TEXT("[") : TEXT(" "), V0, V1, Bin == Cur ? TEXT("]") : TEXT(" "));
		}
		const bool bGreedy = bFeasible && (GreedyNow == E);
		y = DrawLine(x, y, Row, bGreedy ? ColText : (bFeasible ? ColDim : ColDim * FLinearColor(0.6f, 0.6f, 0.6f, 1.f)));
	}
	y += 6.f;
	y = DrawLine(x, y, TEXT("[ ] = current bin   bright = greedy among feasible   x = infeasible"), ColDim);
	y = DrawLine(x, y, TEXT("Inherited values do not change while this agent lives."), ColDim);
	y = DrawLine(x, y, TEXT("Learned values move with every rewarded decision."), ColDim);
}

void ASWHUD::DrawHelp(float X, float Y)
{
	DrawPanel(X, Y, 450.f, 130.f, ColPanel, &ColDim, 24.f);
	float y = Y + 8.f;
	const float x = X + 10.f;
	y = DrawLine(x, y, TEXT("KEYS  (H hides this)"), ColDim);
	y = DrawLine(x, y, TEXT("LMB select organism   Tab youngest Lumen   F follow selected"), ColText);
	y = DrawLine(x, y, TEXT("1 / 2 / 3  speed 1x / 10x / 50x     Space pause     R reset run"), ColText);
	y = DrawLine(x, y, TEXT("P drought on/off     M cycle mode A > B > C > N (resets run)     V scientists"), ColText);
	y = DrawLine(x, y, TEXT("WASD/QE move   RMB look   wheel zoom     G follow next scientist  (F releases)"), ColText);
	y += 4.f;
	y = DrawLine(x, y, TEXT("A learning off | B learning, genome fixed | C learning + evolution | N neutral drift"), ColDim);
	y = DrawLine(x, y, TEXT("Logs: Saved/SymbioticWorld/<run_id>/{agents,births,deaths,population}.csv"), ColDim);
}
