#pragma once
#include "mapper.h"
#include "cartridge.h"
namespace fc_emulator {
enum class Mirroring;

// Mapper 0 (NROM)
class Mapper_00 : public Mapper {
public:
Mapper_00() {}

uint8_t readPrg(BanksAddr addr) override {
	return prgBanks_[addr.cpu.bankIndex][addr.cpu.bankAddr];
}

inline void writePrg(BanksAddr addr, uint8_t data) override {
	assert(!"NOROM can't be written!");
}

inline uint8_t readChr(BanksAddr addr) override {
    return chrBanks_[addr.ppu.bankIndex][addr.ppu.bankAddr];
}

inline void writeChr(BanksAddr addr, uint8_t data) override {
    chrBanks_[addr.ppu.bankIndex][addr.ppu.bankAddr] = data;
}

void mappingPrgRomBanks(uint8_t *prgRomData, uint8_t szBanks) {
    assert( ((0<= szBanks) && (szBanks<= 2)) && "Wrong PRG banks size!" );
    // 前四个bank不映射，这里的szBanks == 0或2
    // 如果是1，16KB -> 载入 $8000-$BFFF, $C000-$FFFF 为镜像
    // 如果是2，32KB -> 载入 $8000-$FFFF
        
    // 映射 PRG-ROM
    uint8_t mappingUpperBanks = szBanks & 0x02;
	mappingPrgRom8k(prgRomData, 4, 0);
	mappingPrgRom8k(prgRomData, 5, 1);
	mappingPrgRom8k(prgRomData, 6, 0 + mappingUpperBanks);
	mappingPrgRom8k(prgRomData, 7, 1 + mappingUpperBanks);

}

void mappingChrRomBanks(uint8_t* chrRomData, uint8_t szBanks) {
    // 不支持 CHR-RAM
    assert((szBanks != 0) && "CHR-RAM mapping is not supported!");

    // 映射 CHR-ROM, 0x0000-0x1FFF  pattern table 0-1
    for (int i = 0; i < 8; i++) {
        mappingChrRom1k(chrRomData, i, i);
    }
}


void save_state(std::vector<uint8_t>& out) const override {
    // NROM没有状态需要保存
    assert(!"Save state is not supported!");
}

void load_state(const std::vector<uint8_t>& in) override {
    // NROM没有状态需要加载
    assert(!"Load state is not supported!");
}

}; // class Mapper_00

} // namespace fc_emulator