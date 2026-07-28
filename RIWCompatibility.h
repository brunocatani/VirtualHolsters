#pragma once

#include <cstdint>

namespace Holsters::RIWCompatibility
{
	void QueueEquippedWeaponHand(
		bool isLeft,
		std::uint32_t weaponFormId) noexcept;
	void ServicePendingRequest() noexcept;
	void ResetPendingRequest() noexcept;
}
