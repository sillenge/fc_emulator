#pragma once


#include <array>
#include <vector>
#include <memory>
#include <stdint.h>
#include <iostream>

#include "mapper.h" 
namespace fc_emulator {
class Bus;
/*
+--------+------+------------------------------------------+
| Offset | Size | Content(s)                               |
+--------+------+------------------------------------------+
|   0    |  3   | 'NES'                                    |
|   3    |  1   | $1A                                      |
|   4    |  1   | 16K PRG-ROM page count                   |
|   5    |  1   | 8K CHR-ROM page count                    |
|        |      |   +-- 0 = CHR_RAM                        |
|        |      |   +-- 1 = 8K CHR-ROM                     |
|        |      |   +-- 2 = 16K CHR-ROM                    |
|   6    |  1   | ROM Control Byte #1                      |
|        |      |   %####vTsM                              |
|        |      |    |  ||||+- 0=Horizontal mirroring      |
|        |      |    |  ||||   1=Vertical mirroring        |
|        |      |    |  |||+-- 1=SRAM enabled              |
|        |      |    |  ||+--- 1=512-byte trainer present  |
|        |      |    |  |+---- 1=Four-screen mirroring     |
|        |      |    |  |                                  |
|        |      |    +--+----- Mapper # (lower 4-bits)     |
|   7    |  1   | ROM Control Byte #2                      |
|        |      |   %####0000                              |
|        |      |    |  |                                  |
|        |      |    +--+----- Mapper # (upper 4-bits)     |
|  8-15  |  8   | $00                                      |
| 16-..  |      | Actual 16K PRG-ROM pages (in linear      |
|  ...   |      | order). If a trainer exists, it precedes |
|  ...   |      | the first PRG-ROM page.                  |
| ..-EOF |      | CHR-ROM pages (in ascending order).      |
+--------+------+------------------------------------------+
*/

// iNES头结构 (16字节)
struct FC_Header {
    char magic[4];              // "NES\x1A"
    uint8_t prgRomBanks16k;     // PRG-ROM大小 (16KB单位)
    uint8_t chrRomBanks8k;      // CHR-ROM大小 (8KB单位)
    uint8_t romControl1;        // 控制字节1 ofset_6
    uint8_t romControl2;        // 控制字节2 ofset_7
    uint8_t prgRamBanks;        // PRG-RAM大小 (8KB单位)
    uint8_t flags9;             // 
    uint8_t flags10;            // 
    uint8_t unused[5];          // 保留字段

public:
    // 镜像模式：0=水平，1=垂直
    Mirroring getMirroring() const;
    // 是否使用chrRam
    inline bool hasChrRam() const { return chrRomBanks8k == 0; }
    // SRAM使能
    inline bool sramEnabled() const { return ((romControl1 >> 1) & 0x01); }
    // 训练器是否存在
    inline bool hasTrainer() const { return ((romControl1 >> 2) & 0x01); }
    // 四屏幕镜像
    inline bool hasFourScreen() const { return ((romControl1 >> 3) & 0x01); }
    // Mapper编号（低4位）
    inline uint8_t mapperLower() const { return ((romControl1 >> 4) & 0x0F); }
    // Mapper编号（高4位）
    inline uint8_t mapperUpper() const { return ((romControl2 >> 4) & 0x0F); }
    // 完整Mapper编号
    inline uint8_t mapperFull() const { return ((uint16_t)mapperUpper() << 4) | mapperLower(); }
    // 未使用位（控制字节2的低4位）
    inline uint8_t unusedBits() const { return (romControl2 & 0x0F); }
};


// 卡带抽象基类
class Cartridge {
public:
    Cartridge(const std::string& filePath);
    ~Cartridge() = default;

    // 获取信息
    bool insertCartridge(const std::string& filePath);
    std::unique_ptr<Mapper> createMapper();
    void connectBus(Bus* bus);
    // @data: 读取到的卡带数据
    bool parseHeader(const std::vector<uint8_t>& data);

	// CPU/PPU内存访问接口

	// 这里的转发是因为mapper掌管着prgBanks，这也是映射器能够映射的关键所在
    // 而Cartridge只是控制着一片的 PRG ROM，他自己并不知道要读哪里，还得找Mapper帮忙
    inline uint8_t readPrg(uint16_t addr) const         { return mapper_->readPrg(addr); }
    inline void writePrg(uint16_t addr, uint8_t data)   { mapper_->writePrg(addr, data); }
    inline uint8_t readChr(uint16_t addr) const         { return mapper_->readChr(addr); }
    inline void writeChr(uint16_t addr, uint8_t data)   { mapper_->writeChr(addr, data); }

    // 这交给映射器去实现，但NOROM没有RAM不需要实现
    //virtual uint8_t readSaveRam(uint16_t addr);
    //virtual void writeSaveRam(uint16_t addr, uint8_t data);

	inline const FC_Header& getFCHeader() { return header_; }
    //inline size_t getPrgRomSize() const { return prgRom_.size(); }
    //inline size_t getChrRomRize() const { return chrRom_.size(); }
    inline uint8_t getChrRomBanks() const   { return header_.chrRomBanks8k; }
    inline uint8_t getPrgRomBanks() const   { return header_.prgRomBanks16k; }
    inline bool hasChrRam() const           { return header_.hasChrRam(); }
    inline Mirroring getMirroring() const   { return mirroring_; }

    //// 用于banks的映射（供映射器使用）
    inline const uint8_t* getPrgRom() const                 { return prgRom_.data(); }
    inline const uint8_t* getChrRom() const                 { return chrRom_.data(); }

    // 名称表镜像
    //uint16_t mirrorNametable(uint16_t addr) const;
    // 名称用于调试
    const char* name() const { return "Cartridge"; }
private:
    Mirroring mirroring_ = Mirroring::Horizontal;
    uint8_t mapperId_ = 0;
    FC_Header header_;

    // ROM 不带const，简化banks映射
    std::vector<uint8_t>                prgRom_;        // 程序只读储存器全数据
    std::vector<uint8_t>                chrRom_;        // 角色只读储存器全数据
    std::array<uint8_t, 8 * 1024>       saveMemory_;    // 工作(work)/保存(save)内存
    // 显存移交给PPU管理
    //std::array<uint8_t, 2 * 1024>       videoMemory;    // 显存
    //std::array<uint8_t, 2 * 1024>       videoMemoryEx;  // 4屏用额外显存

    Bus* bus_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace fc_emulator