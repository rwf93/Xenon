#pragma once

#include <vector>

#include "Xenon/Base/SystemDevice.h"
#include "Xenon/Core/RootBus/HostBridge/HostBridge.h"

// PCI Configuration region
#define PCI_CONFIG_REGION_ADDRESS	0xD0000000
#define PCI_CONFIG_REGION_SIZE		0x1000000

class RootBus
{
public:
	void Init();

    void AddHostBridge(HostBridge* newHostBridge);

	void AddDevice(SystemDevice* device);

	void Read(u64 readAddress, u64* data, u8 byteCount, bool SOC = false);
	void Write(u64 writeAddress, u64 data, u8 byteCount, bool SOC = false);
	
	// Configuration Space R/W
	void ConfigRead(u64 readAddress, u64* data, u8 byteCount);
	void ConfigWrite(u64 writeAddress, u64 data, u8 byteCount);
private:
	HostBridge* hostBridge;
	u32 deviceCount;
	std::vector<SystemDevice*> conectedDevices;

	u8* biuData = new u8[0x10000];
};