// Copyright Hello Project. All Rights Reserved.

#include "DataAsset/ComboConfigDataAsset.h"

const FComboSegmentData& UComboConfigDataAsset::GetSegment(int32 InSegmentIndex) const
{
	static FComboSegmentData EmptySegment;
	if (InSegmentIndex < 1 || InSegmentIndex > MaxComboSegments)
		return EmptySegment;

	int32 ArrayIndex = InSegmentIndex - 1;
	if (!Segments.IsValidIndex(ArrayIndex))
		return EmptySegment;

	return Segments[ArrayIndex];
}
