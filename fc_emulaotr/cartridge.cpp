#include <fstream>
#include <cassert>

#include "bus.h"
#include "cartridge.h"
#include "mapper_00.hpp"
#include "logger.h"

namespace fc_emulator {

// 
Cartridge::Cartridge(const std::string& filePath) 
    : bus_(nullptr), mapper_(nullptr) 
{
	prgRom_.clear();
	chrRom_.clear();
	memset(static_cast<void*>(&header_), 0, sizeof(header_));
	memset(const_cast<uint8_t*>(saveMemory_.data()), 0, saveMemory_.max_size());
    if (!insertCartridge(filePath)) {
        throw std::string("Load cartridge error!");
        return;
    }
    return;
}


bool Cartridge::insertCartridge(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_STREAM_FATAL << "无法打开ROM文件:" << filePath << std::endl;
        return false;
    }

    // 读取文件
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> file_data(file_size);
    file.read(reinterpret_cast<char*>(file_data.data()), file_size);

    // 解析头部
    if (!parseHeader(file_data)) {
        LOG_STREAM_FATAL << "卡带解析失败" << std::endl;
        return false;
    }

	// 创建映射器，第二次创建会扔掉旧的，用新的
	mapper_ = createMapper();
	mapper_->setMorroring(this->getMirroring());
    return true;
}

std::unique_ptr<Mapper> Cartridge::createMapper() {
    std::unique_ptr<Mapper> mapper;
    switch (mapperId_) {
    case 0:
        mapper = std::make_unique<Mapper_00>();
        mapper->mappingPrgRomBanks(prgRom_.data(), header_.prgRomBanks16k);
        mapper->mappingChrRomBanks(chrRom_.data(), header_.chrRomBanks8k);
        break;

    case 1:
        assert(!"未实现 mapper_01");
        break;

    default:
        LOG_STREAM_FATAL << "不支持的映射器: " << static_cast<int>(mapperId_) << std::endl;
        assert(!"不支持的映射器: " && mapperId_);
        break;
    }
    return mapper;
}


void Cartridge::connectBus(Bus* bus) {
    bus_ = bus;
    bus_->setMapper(mapper_.get());
}

bool Cartridge::parseHeader(const std::vector<uint8_t>& data) {
    if (data.size() < 16) {
        LOG_STREAM_ERROR << "文件太小，不是有效的NES ROM" << std::endl;
        return false;
    }

    // 检查NES头标志
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A) {
        LOG_STREAM_ERROR << "无效的NES文件格式" << std::endl;
        return false;
    }

    // 复制头部
    std::memcpy(&header_, data.data(), 16);

    // 验证有效性
    if ((header_.romControl2 & 0x0C) == 0x08) {
        // NES 2.0格式，简化处理
        LOG_STREAM_INFO << "检测到NES 2.0格式，简化处理" << std::endl;
    }

    // 提取信息
    mapperId_ = header_.mapperFull();
    mirroring_ = header_.getMirroring();

    // 计算数据偏移
    size_t offset = 16;
    if (header_.hasTrainer()) {
        offset += 512;
    }

    // 加载PRG-ROM
    size_t prg_size = header_.prgRomBanks16k * 16 * 1024;
    if (offset + prg_size > data.size()) {
        LOG_STREAM_ERROR << "文件大小与PRG-ROM大小不匹配" << std::endl;
        return false;
    }

    prgRom_.resize(prg_size);
    std::memcpy(
        const_cast<uint8_t*>(prgRom_.data()),
        data.data() + offset,
        prg_size
    );
    offset += prg_size;

    // 加载CHR-ROM
    size_t chr_size = header_.chrRomBanks8k * 8 * 1024;
    if (chr_size > 0) {
        if (offset + chr_size > data.size()) {
            LOG_STREAM_ERROR << "文件大小与CHR-ROM大小不匹配" << std::endl;
            return false;
        }
        chrRom_.resize(chr_size);
        std::memcpy(const_cast<uint8_t*>(chrRom_.data()), data.data() + offset, chr_size);
    }
    else {
        // 使用CHR-RAM
        assert(!"using CHR-RAM");
    }

    // todo 初始化PRG-RAM （暂不支持）

    LOG_STREAM_INFO << "ROM加载成功: PRG=" << header_.prgRomBanks16k << "x16KB, "
        << "CHR=" << header_.chrRomBanks8k << "x8KB, "
        << "Mapper=" << static_cast<int>(mapperId_) << std::endl;

    return true;
}

// 获取映射模式
Mirroring FC_Header::getMirroring() const {
	Mirroring res = Mirroring::Horizontal;
	if ((romControl1 & 0x08) != 0)  res = Mirroring::FourScreen;
	else res = (Mirroring)(romControl1 & 0x01);
	return res;
}

} // namespace fc_emulator 