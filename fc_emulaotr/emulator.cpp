#include <cassert>


#include "emulator.h"

namespace fc_emulator {

Emulator::Emulator()
	: cpu_(std::make_unique<CPU>()),
	ppu_(std::make_unique<PPU>()),
	controller1_(std::make_unique<Controller>()),
	controller2_(std::make_unique<Controller>()),
	cartridge_(nullptr)
{
	bus_ = std::make_unique<Bus>(cpu_.get(), ppu_.get(),
		controller1_.get(), controller2_.get());
	cpu_->connectBus(bus_.get());
	ppu_->connectBus(bus_.get());

	// 可以注册帧完成回调，但这里我们通过 stepFrame 同步检测，所以不需要
	// ppu_->regFrameReadyCallback(...);
}

Emulator::~Emulator() = default;

bool Emulator::insertCartridge(const std::string& filePath) {
	try {
		cartridge_ = std::make_unique<Cartridge>(filePath);
	} catch (...) {
		return false;
	}
	cartridge_->connectBus(bus_.get());
	bus_->connectCartridge(cartridge_.get());

	return true;
}

bool Emulator::stepFrame() {
	while (!ppu_->isFrameComplete()) {
		bus_->clock();
	}
	return true;                  // 帧已完成
}

// 复位系统
void Emulator::reset() {
	cpu_->reset();
	ppu_->reset();
	controller1_->reset();
	controller2_->reset();
}

// 
const void Emulator::stepInstruction() {

	// 空转
	while (!cpu_->isComplete()) {
		bus_->clock();
	}

	// 一个真的周期
	bus_->clock();

}

void Emulator::setButton(int player, CtrlButton button, bool pressed) {
		controller1_->setButton(button, pressed);

	// 若支持玩家2，可添加 player == 2 的处理
}

} // namespace fc_emulator