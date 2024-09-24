#pragma once

#include <iostream>

#include "Xenon/Core/RootBus/HostBridge/PCIBridge/PCIDevice.h"

namespace Xe
{
	namespace PCIDev
	{
		namespace EHCI1
		{
#define EHCI1_DEV_SIZE	0x1000

			class EHCI1 : public PCIDevice
			{
			public:
				EHCI1();
				void Read(u64 readAddress, u64* data, u8 byteCount) override;
				void ConfigRead(u64 readAddress, u64* data, u8 byteCount) override;
				void Write(u64 writeAddress, u64 data, u8 byteCount) override;
				void ConfigWrite(u64 writeAddress, u64 data, u8 byteCount) override;

			private:
			};

		}
	}
}