#pragma once
#include <cstdint>
struct PadState {};
struct HidSixAxisSensorHandle {};
struct HidSixAxisSensorState {};
struct AccountUid { uint64_t uid[2]; };

enum : uint64_t {
 HidNpadButton_A = 1ull << 0,
 HidNpadButton_B = 1ull << 1,
 HidNpadButton_X = 1ull << 2,
 HidNpadButton_Y = 1ull << 3,
 HidNpadButton_Plus = 1ull << 4,
 HidNpadButton_Minus = 1ull << 5,
 HidNpadButton_Left = 1ull << 6,
 HidNpadButton_Right = 1ull << 7,
 HidNpadButton_Up = 1ull << 8,
 HidNpadButton_Down = 1ull << 9,
 HidNpadButton_L = 1ull << 10,
 HidNpadButton_R = 1ull << 11,
 HidNpadButton_ZL = 1ull << 12,
 HidNpadButton_ZR = 1ull << 13,
 HidNpadButton_StickL = 1ull << 14,
 HidNpadButton_StickR = 1ull << 15,
 HidNpadButton_StickLLeft = 1ull << 16,
 HidNpadButton_StickLRight = 1ull << 17,
 HidNpadButton_StickLUp = 1ull << 18,
 HidNpadButton_StickLDown = 1ull << 19,
 HidNpadButton_StickRLeft = 1ull << 20,
 HidNpadButton_StickRRight = 1ull << 21,
 HidNpadButton_StickRUp = 1ull << 22,
 HidNpadButton_StickRDown = 1ull << 23,
};

using Result = uint32_t;
struct NsApplicationRecord { uint8_t bytes[24]; };
struct NsApplicationView;
Result nsGetApplicationView(NsApplicationView*, const uint64_t*, int);
Result nsCheckApplicationLaunchVersion(uint64_t);
Result nsGetLastGameCardMountFailureResult();
