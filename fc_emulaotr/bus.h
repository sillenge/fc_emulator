#pragma once

#include <array>


namespace fc_emulator{

class CPU;
class PPU;
class Mapper;
class Cartridge;
class Controller;

class Bus
{

public:
    Bus(CPU* cpu, PPU* ppu, Controller* controller1, Controller* controller2);
    ~Bus() = default;

    // 这个函数主要用于换卡的时候（老板，换碟）
	inline void connectCartridge(Cartridge* cartridge) { cartridge_ = cartridge; }
	inline void setMapper(Mapper* mapper) { mapper_ = mapper; }
    // 用于下发reset信号给各个部件
    //bool reset();

    uint8_t CPURead (uint16_t addr);
    void    CPUWrite(uint16_t addr, uint8_t data);
    uint8_t PPURead (uint16_t addr);
    void    PPUWrite(uint16_t addr, uint8_t data);

    void mappingPPUNametables(uint8_t* pNametable);

    void triggerNmi();
private:
    std::array<uint8_t, 2 * 1024>       mainMemory_;      // 主内存

    CPU* cpu_;
	PPU* ppu_;
	Cartridge* cartridge_;
    // mapper本该由Cartridge管理，但现在PPU也会向其中映射VRAM
    Mapper* mapper_;
    Controller* controller1_;
    Controller* controller2_;
};

} // namespace fc_emulator