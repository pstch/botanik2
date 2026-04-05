#include <cstdint>

#include "serial.h"

/** TODO
 * 
 */
struct ChangeScript {
    uint16_t    head_pos;
    uint16_t    head_cols;

    uint16_t    body_pos;
    uint16_t    body_rows;

    uint16_t    tail_pos;
    uint16_t    tail_cols;

    int16_t     delta;
    uint16_t    span;
};

/** TODO
 * 
 */
class InputEditor {

    InputEditor(BufferedSerial& serial, uint16_t cols, uint16_t rows);

    uint16_t occupied_rows() const;

    void set_ps1(char* buf, uint16_t len);
    void set_ps2(char* buf, uint16_t len);

    void set_origin(uint16_t row, int16_t shift = 0);
    void set_contents(char* buf, uint16_t len);

    void reset();
    void redraw(bool clear = true);

    void remove_characters(uint16_t len);
    void insert_characters(char* buf, uint16_t len);
    void select_characters(uint16_t anchor, uint16_t len);

};
