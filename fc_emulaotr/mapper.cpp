#include <cassert>

#include "mapper.h"
#include "cartridge.h"
namespace fc_emulator {


void Mapper::MappingNametableBanks(uint8_t* nametableData) {
	switch (morrorring_) {
	case Mirroring::Horizontal:
		// 水平: $2000=$2400, $2800=$2C00
		mappingChrRom1k(nametableData, 0x08, 0);
		mappingChrRom1k(nametableData, 0x09, 0);
		mappingChrRom1k(nametableData, 0x0A, 1);
		mappingChrRom1k(nametableData, 0x0B, 1);
		break;
	case Mirroring::Vertical:
		// 垂直: $2000=$2800, $2400=$2C00
		mappingChrRom1k(nametableData, 0x08, 0);
		mappingChrRom1k(nametableData, 0x09, 1);
		mappingChrRom1k(nametableData, 0x0A, 0);
		mappingChrRom1k(nametableData, 0x0B, 1);
		break;
	case Mirroring::FourScreen:
		// 四屏幕模式
		mappingChrRom1k(nametableData, 0x08, 0);
		mappingChrRom1k(nametableData, 0x09, 1);
		mappingChrRom1k(nametableData, 0x0A, 0);
		mappingChrRom1k(nametableData, 0x0B, 1);
		break;
	case Mirroring::SingleScreenA:
		mappingChrRom1k(nametableData, 0x08, 0);
		mappingChrRom1k(nametableData, 0x09, 0);
		mappingChrRom1k(nametableData, 0x0A, 0);
		mappingChrRom1k(nametableData, 0x0B, 0);
		break;
	case Mirroring::SingleScreenB:
		mappingChrRom1k(nametableData, 0x08, 1);
		mappingChrRom1k(nametableData, 0x09, 1);
		mappingChrRom1k(nametableData, 0x0A, 1);
		mappingChrRom1k(nametableData, 0x0B, 1);
		break;

	default:
		assert(!"BAD ACTION");
	}
	// 镜像
	mappingChrRom1k(nametableData, 0x0C, 0x08);
	mappingChrRom1k(nametableData, 0x0D, 0x09);
	mappingChrRom1k(nametableData, 0x0E, 0x0A);
	mappingChrRom1k(nametableData, 0x0F, 0x0B);
}

// 
void Mapper::reset() {
	memset(prgBanks_, 0, sizeof(prgBanks_));
	memset(chrBanks_, 0, sizeof(chrBanks_));
}

} // namespace fc_emulator