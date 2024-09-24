#pragma once

#include <iostream>

#include "Xenon/Core/RootBus/HostBridge/PCIBridge/PCIDevice.h"

namespace Xe
{
	namespace PCIDev
	{
		namespace EHCI0
		{
#define EHCI0_DEV_SIZE	0x1000

			class EHCI0 : public PCIDevice
			{
			public:
				EHCI0();
				void Read(u64 readAddress, u64* data, u8 byteCount) override;
				void ConfigRead(u64 readAddress, u64* data, u8 byteCount) override;
				void Write(u64 writeAddress, u64 data, u8 byteCount) override;
				void ConfigWrite(u64 writeAddress, u64 data, u8 byteCount) override;

			private:
			};

		}
	}
}