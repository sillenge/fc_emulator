#pragma once

#include <cstdint>
#include <array>

namespace fc_emulator {

// 基础控制器按钮枚举（与 FC 硬件定义一致）
enum class CtrlButton : uint8_t {
	A = 0x00,
	B,
	SELECT,
	START,
	UP,
	DOWN,
	LEFT,
	RIGHT,
	COUNT
};


// 控制器类
class Controller {
public:
	Controller() = default;
	~Controller() = default;

	// 设置按钮状态（由外部调用）
	void setButton(CtrlButton button, bool pressed);

	// CPU 读取控制器状态（串行协议）
	uint8_t read();

	// CPU 写入控制器（设置 strobe）
	void write(uint8_t data);

	// 重置
	void reset();

private:
	std::array<bool, static_cast<int>(CtrlButton::COUNT)> btnStatus_;
	int btnReadIndex_;
	uint8_t btnMask_;
};

} // namespace fc_emulator