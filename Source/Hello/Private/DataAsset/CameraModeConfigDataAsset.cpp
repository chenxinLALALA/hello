// Copyright Hello Project. All Rights Reserved.

#include "DataAsset/CameraModeConfigDataAsset.h"

TSubclassOf<UHelloCameraMode> UCameraModeConfigDataAsset::FindCameraModeClass(const FGameplayTag& InTag) const
{
	if (!InTag.IsValid()) return nullptr;

	for (const FCameraModeEntry& Entry : CameraModeEntries)
	{
		if (Entry.CameraTag == InTag && Entry.CameraModeClass)
		{
			return Entry.CameraModeClass;
		}
	}

	return nullptr;
}

void UCameraModeConfigDataAsset::GetAllCameraTags(TArray<FGameplayTag>& OutTags) const
{
	OutTags.Reset();
	for (const FCameraModeEntry& Entry : CameraModeEntries)
	{
		if (Entry.CameraTag.IsValid())
		{
			OutTags.Add(Entry.CameraTag);
		}
	}
}
