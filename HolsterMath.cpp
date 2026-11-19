#include "HolsterMath.h"

#include <cmath>

namespace Holsters {
	// FO4VR stores child axes as matrix rows: a local vector maps to world
	// as Transpose()*v (raw compose helper 0x1401A8D60, as ROCK verifies).
	NiPoint3 LocalVectorToWorld(const NiMatrix43& m, const NiPoint3& v)
	{
		return NiPoint3(
			m.data[0][0] * v.x + m.data[1][0] * v.y + m.data[2][0] * v.z,
			m.data[0][1] * v.x + m.data[1][1] * v.y + m.data[2][1] * v.z,
			m.data[0][2] * v.x + m.data[1][2] * v.y + m.data[2][2] * v.z);
	}

	NiPoint3 WorldVectorToLocal(const NiMatrix43& m, const NiPoint3& v)
	{
		return NiPoint3(
			m.data[0][0] * v.x + m.data[0][1] * v.y + m.data[0][2] * v.z,
			m.data[1][0] * v.x + m.data[1][1] * v.y + m.data[1][2] * v.z,
			m.data[2][0] * v.x + m.data[2][1] * v.y + m.data[2][2] * v.z);
	}

	// Stored-row product: the engine's world rotation is child * parent.
	NiMatrix43 MultiplyStored(const NiMatrix43& lhs, const NiMatrix43& rhs)
	{
		NiMatrix43 result{};
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				result.data[row][column] =
					lhs.data[row][0] * rhs.data[0][column] +
					lhs.data[row][1] * rhs.data[1][column] +
					lhs.data[row][2] * rhs.data[2][column];
			}
			result.data[row][3] = 0.0F;
		}
		return result;
	}

	NiPoint3 LocalPointToWorld(const NiTransform& parent, const NiPoint3& local)
	{
		const NiPoint3 offset = LocalVectorToWorld(parent.rot, local);
		return NiPoint3(
			parent.pos.x + offset.x * parent.scale,
			parent.pos.y + offset.y * parent.scale,
			parent.pos.z + offset.z * parent.scale);
	}

	// The local transform that places a child of `parent` at `world`.
	bool LocalFromWorld(const NiTransform& parent, const NiTransform& world, NiTransform& local)
	{
		if (!std::isfinite(parent.scale) || std::fabs(parent.scale) < 0.0001F) {
			return false;
		}
		const float inverseScale = 1.0F / parent.scale;
		local.rot = MultiplyStored(world.rot, parent.rot.Transpose());
		const NiPoint3 delta(
			world.pos.x - parent.pos.x,
			world.pos.y - parent.pos.y,
			world.pos.z - parent.pos.z);
		local.pos = WorldVectorToLocal(parent.rot, delta);
		local.pos.x *= inverseScale;
		local.pos.y *= inverseScale;
		local.pos.z *= inverseScale;
		local.scale = world.scale * inverseScale;
		return std::isfinite(local.pos.x) && std::isfinite(local.pos.y) &&
			std::isfinite(local.pos.z) && std::isfinite(local.scale);
	}

	// The world transform of a child of `parent` placed at `local`.
	NiTransform WorldFromLocal(const NiTransform& parent, const NiTransform& local)
	{
		NiTransform world{};
		world.rot = MultiplyStored(local.rot, parent.rot);
		world.pos = LocalPointToWorld(parent, local.pos);
		world.scale = parent.scale * local.scale;
		return world;
	}

	NiPoint3 WorldPointToLocal(const NiTransform& parent, const NiPoint3& world)
	{
		const float inverseScale =
			std::fabs(parent.scale) > 0.0001F ? 1.0F / parent.scale : 1.0F;
		NiPoint3 local = WorldVectorToLocal(parent.rot, world - parent.pos);
		local.x *= inverseScale;
		local.y *= inverseScale;
		local.z *= inverseScale;
		return local;
	}

	struct Quaternion
	{
		float w{ 1.0F }, x{}, y{}, z{};
	};

	Quaternion ToQuaternion(const NiMatrix43& m)
	{
		const float (*r)[4] = m.data;
		Quaternion q{};
		const float trace = r[0][0] + r[1][1] + r[2][2];
		if (trace > 0.0F) {
			const float s = std::sqrt(trace + 1.0F) * 2.0F;
			q.w = 0.25F * s;
			q.x = (r[2][1] - r[1][2]) / s;
			q.y = (r[0][2] - r[2][0]) / s;
			q.z = (r[1][0] - r[0][1]) / s;
		} else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
			const float s = std::sqrt(1.0F + r[0][0] - r[1][1] - r[2][2]) * 2.0F;
			q.w = (r[2][1] - r[1][2]) / s;
			q.x = 0.25F * s;
			q.y = (r[0][1] + r[1][0]) / s;
			q.z = (r[0][2] + r[2][0]) / s;
		} else if (r[1][1] > r[2][2]) {
			const float s = std::sqrt(1.0F + r[1][1] - r[0][0] - r[2][2]) * 2.0F;
			q.w = (r[0][2] - r[2][0]) / s;
			q.x = (r[0][1] + r[1][0]) / s;
			q.y = 0.25F * s;
			q.z = (r[1][2] + r[2][1]) / s;
		} else {
			const float s = std::sqrt(1.0F + r[2][2] - r[0][0] - r[1][1]) * 2.0F;
			q.w = (r[1][0] - r[0][1]) / s;
			q.x = (r[0][2] + r[2][0]) / s;
			q.y = (r[1][2] + r[2][1]) / s;
			q.z = 0.25F * s;
		}
		return q;
	}

	NiMatrix43 FromQuaternion(const Quaternion& q)
	{
		NiMatrix43 m{};
		m.data[0][0] = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
		m.data[0][1] = 2.0F * (q.x * q.y - q.z * q.w);
		m.data[0][2] = 2.0F * (q.x * q.z + q.y * q.w);
		m.data[1][0] = 2.0F * (q.x * q.y + q.z * q.w);
		m.data[1][1] = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
		m.data[1][2] = 2.0F * (q.y * q.z - q.x * q.w);
		m.data[2][0] = 2.0F * (q.x * q.z - q.y * q.w);
		m.data[2][1] = 2.0F * (q.y * q.z + q.x * q.w);
		m.data[2][2] = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
		return m;
	}

	NiMatrix43 SlerpRotation(const NiMatrix43& from, const NiMatrix43& to, const float t)
	{
		Quaternion a = ToQuaternion(from);
		Quaternion b = ToQuaternion(to);
		float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
		if (dot < 0.0F) {
			b = { -b.w, -b.x, -b.y, -b.z };
			dot = -dot;
		}
		Quaternion q{};
		if (dot > 0.9995F) {
			q = { a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		} else {
			const float theta = std::acos(dot);
			const float sinTheta = std::sin(theta);
			const float wa = std::sin((1.0F - t) * theta) / sinTheta;
			const float wb = std::sin(t * theta) / sinTheta;
			q = { a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb };
		}
		const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (length > 0.0001F) {
			q = { q.w / length, q.x / length, q.y / length, q.z / length };
		}
		return FromQuaternion(q);
	}

	NiTransform BlendTransforms(const NiTransform& from, const NiTransform& to, const float t)
	{
		NiTransform result{};
		result.rot = SlerpRotation(from.rot, to.rot, t);
		result.pos = NiPoint3(
			from.pos.x + (to.pos.x - from.pos.x) * t,
			from.pos.y + (to.pos.y - from.pos.y) * t,
			from.pos.z + (to.pos.z - from.pos.z) * t);
		result.scale = from.scale + (to.scale - from.scale) * t;
		return result;
	}
}
