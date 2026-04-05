#include <cstdint>

struct InputEvent {
    enum class Type {
        NONE,           // No complete sequence yet
        CONTENT,        // Standard printable text (char in 'value')
        COMMAND,        // Single-byte control or CSI action (Ctrl+A, Arrows, etc.)
        MODE            // Alt+1, Alt+2, etc. (Index in 'value')
    };

    enum class Command : uint16_t {
        NONE = 0,
        ENTER,
        BACKSPACE,
        DELETE,
        TAB,
        ESC,
        UP, DOWN, LEFT, RIGHT,
        HOME, END, PAGE_UP, PAGE_DOWN,
        
        // Control Combinations (Physical)
        CTRL_A, CTRL_B, CTRL_C, CTRL_D, CTRL_E, CTRL_F, CTRL_G,
        CTRL_K, CTRL_L, CTRL_N, CTRL_P, CTRL_R, CTRL_U, CTRL_W, CTRL_Y,
        CTRL_SPACE,
        
        // Meta/Alt Combinations (Physical)
        ALT_B, ALT_F, ALT_D, ALT_BACKSPACE
    };
    Type     type = Type::NONE;
    Command  command = Command::NONE;
    char     value = 0;
};


class InputParser {
public:
    InputEvent process(uint8_t byte);
    void reset();
};
