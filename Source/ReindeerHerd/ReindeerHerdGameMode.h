// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ReindeerHerdGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AReindeerHerdGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AReindeerHerdGameMode();
};



