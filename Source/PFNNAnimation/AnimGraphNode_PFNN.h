// Copyright 2018 Sticks & Stones. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "../SurvivalGame/Animation/AnimNode_PFNN.h"

#include "AnimGraphNode_PFNN.generated.h"

/**
 * 
 */
UCLASS()
class PFNNANIMATION_API UAnimGraphNode_PFNN : public UAnimGraphNode_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_PFNN Node;

public:

	// UEdGraphNode interface
	/**
	 * Default AnimGraphNode function to get title
	 * @param[in] TitleType, blueprint title of this object 
	 * @return Tilte of this animation node
	 */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	/**
	 * Default AnimGraphNode function to get GetTooltipText
	 * @return GetTooltipText of this animation node
	 */
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface
};
