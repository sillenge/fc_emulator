#include <cassert>
#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include "mapper.h"
#include "cartridge.h"
#include "controller.h"
#include "logger.h"

namespace fc_emulator {

size_t kDmaClock = 513;

Bus::Bus(CPU* cpu, PPU* ppu, Controller* controller1, Controller* controller2)
  : cpu_(cpu), 
	ppu_(ppu), 
	controller1_(controller1),
	controller2_(controller2),
	cartridge_(nullptr),		// 后续使用函数connectCartridge连接
	mapper_(nullptr),			//mapper随Cartridge一同连接进来
	dmaClock_(0),
	totalSysCycles_(0)
{
	mainMemory_.fill(0);
}


void Bus::clock() {
	// DMA时间, ppu在运行
    if (dmaClock_) {
        dmaClock_--;
    }
    else {
        cpu_->clock();
    }

    ppu_->clock();
    ppu_->clock();
    ppu_->clock();
    totalSysCycles_++;
}


uint8_t fc_emulator::Bus::CPURead(uint16_t addr) {
	uint8_t data;
	/*CPU 地址空间
	+---------+-------+-------+-----------------------+
	| 地址    | 大小  | 标记  |         描述          |
	+---------+-------+-------+-----------------------+
	| $0000   | $800  |       | RAM                   |
	| $0800   | $800  | M     | RAM                   |
	| $1000   | $800  | M     | RAM                   |
	| $1800   | $800  | M     | RAM                   |
	| $2000   | 8     |       | Registers             |
	| $2008   | $1FF8 | R     | Registers             |
	| $4000   | $20   |       | Registers             |
	| $4020   | $1FDF |       | Expansion ROM         |
	| $6000   | $2000 |       | SRAM                  |
	| $8000   | $4000 |       | PRG-ROM               |
	| $C000   | $4000 |       | PRG-ROM               |
	+---------+-------+-------+-----------------------+
	标记图例: M = $0000的镜像
				R = $2000-2008 每 8 bytes 的镜像
			(e.g. $2008=$2000, $2018=$2000, etc.)     */
	switch (addr >> 13)
	{
	case 0:
		// [0x0000, 0x2000): 系统主内存, 4个镜像 [0x0000, 0x0800) == [0x0800, 0x1000) == ...
		data = mainMemory_[addr & 0x7FF];
		break;
	case 1:
		// [0x2000, 0x4000): 寄存器，8字节镜像[0x2000, 0x2008) == [0x2008, 0x2010) == ...
		data = ppu_->cpuRead(addr);
		break;
	case 2:
		// [$4000, $6000): pAPU寄存器 扩展ROM区
		if (addr < 0x4020) {
			// 前0x20字节为APU和I/O寄存器
			switch (addr) {
			case 0x4016:
				data = controller1_->read();
				break;
			case 0x4017:
				data = controller2_->read();
                break;
            default:
				return 0;
				//assert(!"Not Implementation!");
				break;
			}
		}
		else {
			assert(!"Not Implementation!");
		}
		break;
	case 3:
		// 高三位为3, [$6000, $8000): 存档 SRAM区
		assert(!"Not Implementation!");
		//data = cartridge_->readSaveRam(addr & 0x1FFF);
		break;
	case 4: case 5: case 6: case 7:
		// 高一位为1, [$8000, $10000) 程序PRG-ROM区
		data = cartridge_->readPrg(addr);
		break;
	default:
		assert(!"invalid address");
		break;
	}
	return data;
}



void Bus::CPUWrite(uint16_t addr, uint8_t data) {
	switch (addr >> 13)
	{
	case 0:
		// 高三位为0: [$0000, $2000): 系统主内存, 4次镜像
		mainMemory_[addr & (uint16_t)0x07ff] = data;
		break;
	case 1:
		// 高三位为1, [$2000, $4000): PPU寄存器, 8字节步进镜像
		ppu_->cpuWrite(addr, data);
		break;
	case 2:
		// 高三位为2, [$4000, $6000): pAPU寄存器 扩展ROM区
		// 前0x20字节为APU, I / O寄存器
		if (addr < 0x4020) {
			switch (addr) {
			case 0x4014:
				// 此时是DMA时间，cpu停止执行
				// dmaClock 进行了总时钟的奇偶对齐
				dmaClock_ = kDmaClock + (totalSysCycles_ & 1); 
				saveDmaPage(data);
				break;
			case 0x4016:
				controller1_->write(data);            // 写入控制器strobe
				break;
			case 0x4017:
				controller2_->write(data);            // 写入控制器strobe
                break;
            default:
				//assert(!"Not Implementation!");
				break;
			}
		}
		break;
	case 3:
		// 高三位为3, [$6000, $8000): 存档 SRAM区
		assert(!"Not Implementation!");
		//cartridge_->writeSaveRam(addr & 0x1FFF, data);
		break;
	case 4: case 5: case 6: case 7:
		// 高一位为1, [$8000, $10000) 程序PRG-ROM区
		assert(!"ERROR: Write PRG-ROM");
		cartridge_->writePrg(addr, data);
		break;
	default:
		assert(!"invalid address");
		break;
	}
	return;
}

/*PPU 地址空间
+-------+-------+-----------------------------------+
| 地址	| 大小	|	   描述							|
|$0000	|$1000	|	Pattern Table 0 (4KB)			|
|$1000	|$1000	|	Pattern Table 1 (4KB)			|
|$2000	|$400	|	Name Table 0 (1KB)				|
|$2400	|$400	|	Name Table 1 (1KB)				|
|$2800	|$400	|	Name Table 2 (1KB)				|
|$2C00	|$400	|	Name Table 3 (1KB)				|
|$3000	|$F00	|	Mirrors of $2000-$2EFF			|
|$3F00	|$20	|	Palette RAM indexes (32 bytes)	|
|$3F20	|$E0	|	Mirrors of $3F00-$3F1F			|
+-------+-------+-----------------------------------+*/
uint8_t Bus::PPURead(uint16_t addr) {
	if (addr < 0x3F00){
		return mapper_->readChr(addr);
	}
	else {
		return ppu_->readPalette(addr);
	}
}


void Bus::PPUWrite(uint16_t addr, uint8_t data) {
	if (addr < 0x3F00) {
		mapper_->writeChr(addr, data);
	}
	else {
		ppu_->writePalette(addr, data);
	}
}

// 映射PPU的VRAM
void Bus::mappingPPUNametables(uint8_t* pNametable) {
	mapper_->MappingNametableBanks(pNametable);
}


void Bus::saveDmaPage(uint8_t addrPage) {
	const uint16_t offset = (addrPage << 8) & 0x07FF;
	switch (addrPage >> 5) {
    case 0:
        // 系统内存
		ppu_->loadOMA(&mainMemory_[offset]);
		break;
    case 1:
        // PPU寄存器
        assert(!"No Implementation");
        break;
    case 2:
        // 扩展区
        assert(!"No Implementation");
        break;
    case 3:
        // 存档 SRAM区
		ppu_->loadOMA(&mapper_->loadState()[offset]);
        break;
    case 4: case 5: case 6: case 7:
        // 高一位为1, [$8000, $10000) 程序PRG-ROM区
		uint8_t ** prgBanks = mapper_->getPrgBanks();
        ppu_->loadOMA(prgBanks[addrPage >> 5] + offset);
    }
}

// 
void Bus::triggerNmi() {
	cpu_->nmi();
}

} // namespace fc_emulator 