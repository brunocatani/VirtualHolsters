#pragma once

// Transform helpers for holster displays. FO4VR's NiMatrix43 stores a
// child's axes as rows.

#include "f4se/NiTypes.h"

namespace Holsters {
	NiPoint3 LocalVectorToWorld(const NiMatrix43& m, const NiPoint3& v);
	NiPoint3 WorldVectorToLocal(const NiMatrix43& m, const NiPoint3& v);
	NiPoint3 LocalPointToWorld(const NiTransform& parent, const NiPoint3& local);
	NiPoint3 WorldPointToLocal(const NiTransform& parent, const NiPoint3& world);
	// The local transform that places a child of `parent` at `world`.
	// False when the parent's scale is unusable.
	bool LocalFromWorld(const NiTransform& parent, const NiTransform& world, NiTransform& local);
	// The world transform of a child of `parent` placed at `local`.
	NiTransform WorldFromLocal(const NiTransform& parent, const NiTransform& local);
	// A transform between `from` (t = 0) and `to` (t = 1).
	NiTransform BlendTransforms(const NiTransform& from, const NiTransform& to, float t);
}
