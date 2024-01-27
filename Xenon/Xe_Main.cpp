#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

#include "Xenon/Core/Bus/Bus.h"
#include "Xenon/Core/PostBus/PostBus.h"
#include "Xenon/Core/NAND/NAND.h"
#include "Xenon/Core/XCPU/XCPU.h"
#include "Xenon/Core/RAM/RAM.h"
#include "Xenon/Core/PCIBus/PCIBus.h"
#include "Xenon/Core/PCIBus/XMA/XMA.h"
#include "Xenon/Core/PCIBus/CDROM/CDROM.h"
#include "Xenon/Core/PCIBus/HDD/HDD.h"
#include "Xenon/Core/PCIBus/SMC/SMC.h"

int main(int argc, char* argv[])
{
	Bus Bus;
	PCIBus pciBus;

	XMA xma;
	CDROM cdrom;
	HDD hdd;
	SMC smc;

	PostBus postBus;
	NAND nandDevice;
	RAM ram;

	Bus.Init();

	xma.Initialize("XMA", XMA_START_ADDR, XMA_END_ADDR);
	cdrom.Initialize("CDROM", CDROM_START_ADDR, CDROM_END_ADDR);
	hdd.Initialize("HDD", HDD_START_ADDR, HDD_END_ADDR);
	smc.Initialize("SMC", SMC_START_ADDR, SMC_END_ADDR);

	pciBus.addPCIDevice(&xma);
	pciBus.addPCIDevice(&cdrom);
	pciBus.addPCIDevice(&hdd);
	pciBus.addPCIDevice(&smc);

	postBus.Initialize("PostBus", POST_BUS_ADDR, POST_BUS_ADDR);
	nandDevice.Initialize("NAND", NAND_START_ADDR, NAND_END_ADDR);
	ram.Initialize("RAM", RAM_START_ADDR, RAM_START_ADDR + RAM_SIZE);

	Bus.AddPCIBus(&pciBus);
	Bus.AddDevice(&postBus);
	Bus.AddDevice(&nandDevice);
	Bus.AddDevice(&ram);
    nandDevice.Load("C://Xbox/jasper_nand_rgh2.bin");
    Xe::Core::XCPU::XCPU xcpu("C://Xbox/1bl.bin", &Bus);
    
    xcpu.Start(0x8000020000000100);
	return 0;
}