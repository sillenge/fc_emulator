#include "controller.h"

namespace fc_emulator {

void Controller::setButton(CtrlButton button, bool pressed) {
    btnStatus_[static_cast<int>(button)] = pressed;
    return;
}

void Controller::write(uint8_t data) {
    btnMask_ = (data & 0x01) ? 0x00 : 0x07;
    if (data & 0x01) {
        btnReadIndex_ = 0;
    }
    return;
}

uint8_t Controller::read() {
    // 正常读取：返回移位寄存器最低位，然后右移
    uint8_t result = btnStatus_[btnReadIndex_ & btnMask_];
    btnReadIndex_++;
    return result;
}

void Controller::reset() {
    memset(btnStatus_.data(), 0, btnStatus_.size());
    btnReadIndex_ = 0x00;
    btnMask_ = 0x00;
}

} // namespace fc_emulator