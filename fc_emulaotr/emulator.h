#pragma once
#include <memory>
#include <chrono>
#include <optional>
#include "cpu.h"
#include "ppu.h"
#include "bus.h"
#include "cartridge.h"
#include "controller.h"

namespace fc_emulator {
using MicroSeconds = uint64_t;
class Emulator {
public:
	Emulator();
	~Emulator();

	// 初始化：插入卡带
	bool insertCartridge(const std::string& filePath);

	void reset();
	void clock();
	// 模拟一步（驱动 CPU 一次 + PPU 三次），返回 true 表示一帧已完成
	bool stepFrame();
	const void stepInstruction();
	// 获取当前帧缓冲区（RGBA 格式，256x240）
	std::optional<FrameBuffer> getFrameBuffer() const { return ppu_->getFrameBuffer(); }
	// 处理按键输入
	void setButton(int player, CtrlButton button, bool pressed);
	CPU* getCPU() { return cpu_.get(); }
	PPU* getPPU() { return ppu_.get(); }
private:
	std::unique_ptr<CPU> cpu_;
	std::unique_ptr<PPU> ppu_;
	std::unique_ptr<Bus> bus_;
	std::unique_ptr<Cartridge> cartridge_;
	std::unique_ptr<Controller> controller1_;
	std::unique_ptr<Controller> controller2_;

    inline uint64_t getCurMicros() {
        using namespace std::chrono;
        return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    }
};

} // namespace fc_emulator