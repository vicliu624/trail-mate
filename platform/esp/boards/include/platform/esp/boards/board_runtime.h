#pragma once

class BoardBase;
class GpsBoard;
class LoraBoard;
class MotionBoard;

namespace platform::esp::boards
{

struct AppContextInitHandles
{
    AppContextInitHandles(BoardBase* board_in = nullptr, LoraBoard* lora_board_in = nullptr,
                          GpsBoard* gps_board_in = nullptr, MotionBoard* motion_board_in = nullptr)
        : board(board_in), lora_board(lora_board_in), gps_board(gps_board_in), motion_board(motion_board_in)
    {
    }

    bool isValid() const
    {
        return board != nullptr;
    }

    BoardBase* board;
    LoraBoard* lora_board;
    GpsBoard* gps_board;
    MotionBoard* motion_board;
};

void initializeBoard(bool waking_from_sleep);
void initializeDisplay();
bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles);
AppContextInitHandles resolveAppContextInitHandles();

} // namespace platform::esp::boards
