#pragma once
#include <cassert>
#include <stdint.h>
#include <vector>


namespace fc_emulator{

// 定义卡带镜像类型
enum class Mirroring {
	Horizontal,
	Vertical,
	FourScreen,
	SingleScreenA,
	SingleScreenB
};


// 指向8 * 8k的物理页，用于程序地址的映射
constexpr int kPrgBanksSize = 0x10000 >> 13;
// 指向16 * 1k的物理页，用于显卡地址的映射
constexpr int kChrBanksSize = 0x4000 / 0x400;

union BanksAddr {

	struct {
		uint16_t bankAddr : 13;
		uint16_t bankIndex : 3;
	} cpu;

	struct {
		uint16_t bankAddr : 10;
		uint16_t bankIndex : 4;
		uint16_t unused : 2;
	} ppu;

	uint16_t addr;

	BanksAddr() = default;
	BanksAddr(uint16_t addr) { this->addr = addr; }
};

/*
映射器基类
说明：这里的映射是Cartridge让Mapper进行映射，Cartridge真包含Mapper
所以Mapper并不知道Cartridge的存在，而仅是做好自己该做的事情
*/
class Mapper {
public:
    Mapper() {}
    virtual ~Mapper() = default;

    // 负责初始化Mapper
    void reset();
    void setMorroring(Mirroring morrorring) { morrorring_ = morrorring; }

    virtual uint8_t readPrg(BanksAddr addr) = 0;
    virtual void writePrg(BanksAddr addr, uint8_t data) = 0;
    virtual uint8_t readChr(BanksAddr addr) = 0;
	virtual void writeChr(BanksAddr addr, uint8_t data) = 0;

    // 名称表镜像
    // IRQ支持
    virtual void irq_pending() const    { assert(!"unsupported"); }
    virtual void clear_irq()            { assert(!"unsupported"); }
    virtual void tick(uint64_t cycles)  { assert(!"unsupported"); }

    // 保存状态
    virtual void save_state(std::vector<uint8_t>& out) const = 0;
    virtual void load_state(const std::vector<uint8_t>& in) = 0;
    // 地址映射

    // ROM data 由 Cartridge 提供
	virtual void mappingPrgRomBanks(uint8_t *prgRomData, uint8_t szBanks) = 0;
	virtual void mappingChrRomBanks(uint8_t* chrRomData, uint8_t szBanks) = 0;
    // nametabaleData 由 PPU 提供，这里主要映射了VRAM
	void MappingNametableBanks(uint8_t* nametableData);

protected:
    // 8k 每bank
	inline void mappingPrgRom8k(uint8_t* data, int desBank, int srcBank) {
		prgBanks_[desBank] = data + (srcBank * 8 * 1024);
	}

	// 1k 每bank
	inline void mappingChrRom1k(uint8_t* data, int desBank, int srcBank) {
		chrBanks_[desBank] = data + (srcBank * 1024);
	}

protected:
    //Cartridge& cartridge_;
    // 单位为8k方便后续支持8k mapper 的映射器
    uint8_t* prgBanks_[kPrgBanksSize] = { nullptr };
    // 单位为1k
    uint8_t* chrBanks_[kChrBanksSize] = { nullptr };

private:
    Mirroring morrorring_;
};

} // namespace FCEmulatro;