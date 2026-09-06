#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SWTypes.h"
#include "SWHUD.generated.h"

class ASWWorldManager;
class ASWAgent;
class UFont;

// Canvas-drawn HUD in the layout of concept plate 1 (no UMG assets, so
// everything is authored as text):
//
//   top-left      title block: mode, seed, sim time, speed
//   top row       stat cards: generation, population A, population B, resources, stability
//   left          SPECIES A - LUMEN: genome means + behaviour distribution bars
//   right         individual inspector (inherited vs learned Q table), then
//                 SPECIES B - TECTON panel below it
//   bottom-left   ECOSYSTEM FLOW minimap: water, Trace X (cyan), Trace Y (amber), organisms, nodes
//   bottom-right  keys (H toggles)
//   bottom-centre drought banner when active
//
// "Stability" is population steadiness over the last 30 logical seconds:
// 1 - (max - min) / max of the total population in that window. It is a
// descriptive score, not a claim about resilience.
UCLASS()
class SYMBIOTICWORLD_API ASWHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASWHUD();
	virtual void DrawHUD() override;

protected:
	UPROPERTY() UFont* Font = nullptr;
	float LineHeight = 15.f;

	// Glass panel: translucent fill, a slightly darker header strip of HeaderH px behind the title
	// lines, a 1 px light border and an optional accent rule along the top.
	void DrawPanel(float X, float Y, float W, float H, const FLinearColor& Fill, const FLinearColor* Accent = nullptr, float HeaderH = 0.f) const;
	void DrawRect(float X, float Y, float W, float H, const FLinearColor& Fill) const;
	float DrawLine(float X, float Y, const FString& Text, const FLinearColor& Color, float Scale = 1.f) const;
	float TextWidth(const FString& Text, float Scale = 1.f) const;

	void DrawTitle(const ASWWorldManager& M);
	void DrawStatCards(const ASWWorldManager& M);
	void DrawSpeciesPanel(const ASWWorldManager& M, ESWSpecies S, float X, float Y, float W);
	void DrawInspector(const ASWWorldManager& M, const ASWAgent& A, float X, float Y, float W);
	void DrawMinimap(const ASWWorldManager& M, float X, float Y, float Size);
	void DrawHelp(float X, float Y);
	// Bottom-centre credits card (Look.GreetingLine1 / GreetingLine2). Cosmetic only.
	void DrawGreeting(const ASWWorldManager& M, float CentreX, float BottomY);
	void DrawSelectionMarker(const ASWAgent& A);
	void DrawEvolutionStrip(const ASWWorldManager& M, float X, float Y, float W);
};
