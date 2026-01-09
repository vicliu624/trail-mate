/**
 * @file      LilyGoKeyboard.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */

#pragma once
#include <Arduino.h>

#ifdef USING_INPUT_DEV_KEYBOARD
#include <Adafruit_TCA8418.h>

#define KB_NONE     -1
#define KB_PRESSED  1
#define KB_RELEASED 0

typedef struct LilyGoKeyboardConfigure {
    uint8_t kb_rows;
    uint8_t kb_cols;
    const char *current_keymap;
    const char *current_symbol_map;
    uint8_t symbol_key_value;
    uint8_t alt_key_value;
    uint8_t caps_key_value;
    uint8_t caps_b_key_value;
    uint8_t char_b_value;
    uint8_t backspace_value;
    // Is there a symbol combination key?
    bool has_symbol_key;
} LilyGoKeyboardConfigure_t;;

// This class, LilyGoKeyboard, inherits from Adafruit_TCA8418 and is designed to handle keyboard operations.
class LilyGoKeyboard : public Adafruit_TCA8418
{
public:
    // Typedef for a callback function that is invoked when a key is read.
    // It takes an integer representing the key state and a reference to a character to store the key value.
    using KeyboardReadCallback = void (*)(int state, char &c);

    // Typedef for a callback function that is called when defined as a gpio.
    using GpioEventCallback = void (*)(bool pressed, uint8_t gpio_idx);

    // Typedef for a callback function called when adjusting the keyboard backlight.
    using BacklightCallback = void (*)(uint8_t level);

    // Typedef for a callback function called when keyboard press or release raw code.
    using KeyboardRawCallback = void (*)(bool pressed, uint8_t raw);

    /**
     * @brief Default constructor for the LilyGoKeyboard class.
     * Initializes the keyboard object.
     */
    LilyGoKeyboard();

    /**
     * @brief Destructor for the LilyGoKeyboard class.
     * Cleans up any resources allocated by the keyboard object.
     */
    ~LilyGoKeyboard();

    /**
     * @brief Sets the pin number for the keyboard backlight.
     *
     * @param backlight The pin number to which the backlight is connected.
     */
    void setPins(int backlight);

    // /**
    //  * @brief Set the keyboard mapping index.
    //  *
    //  * @param maps T-LoRa-Pager uses 0, T-DeckV2 uses 1.
    //  */
    // void setMaps(uint8_t maps);

    /**
     * @brief Initializes the keyboard and sets up the I2C communication.
     *
     * @param w A reference to a TwoWire object for I2C communication.
     * @param irq The interrupt request pin number.
     * @param sda The I2C data line pin number. Defaults to the SDA macro.
     * @param scl The I2C clock line pin number. Defaults to the SCL macro.
     * @return true if the initialization is successful, false otherwise.
     */
    bool begin(const LilyGoKeyboardConfigure_t &config, TwoWire &w, uint8_t irq, uint8_t sda = SDA, uint8_t scl = SCL);

    /**
     * @brief Ends the keyboard operation and releases associated resources.
     */
    void end();

    /**
     * @brief Retrieves the currently pressed key and stores its character value.
     *
     * @param c A pointer to a character where the key value will be stored.
     * @return An integer representing the state of the key press.
     */
    int getKey(char *c);

    /**
     * @brief Sets the brightness level of the keyboard backlight.
     *
     * @param level The brightness level, with a valid range of 0-255.
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Gets the current brightness level of the keyboard backlight.
     *
     * @return The current brightness level as an 8-bit unsigned integer.
     */
    uint8_t getBrightness();

    /**
     * @brief Sets the callback function to be executed when a key is read.
     *
     * @param cb A pointer to the callback function of type KeyboardReadCallback.
     */
    void setCallback(KeyboardReadCallback cb);

    /**
     * @brief When the row and column gpio of an undefined keyboard are changed,
     *      the host is notified through this callback function.
     *
     * @param cb A pointer to the callback function of type GpioEventCallback.
     */
    void setGpioEventCallback(GpioEventCallback cb);

    /**
     * @brief When the backlight callback function is set, the host will be
     *      notified of the backlight adjustment through the callback function.
     *
     * @param cb A pointer to the callback function of type BacklightCallback.
     */
    void setBacklightChangeCallback(BacklightCallback cb);

    /**
     * @brief Set keyboard press or release raw callback function
     * @note  When this callback is set, the program only returns the original key value and does
     *          not continue with subsequent processing. The user needs to handle it by himself.
     *
     * @param cb A pointer to the callback function of type KeyboardRawCallback.
     */
    void setRawCallback(KeyboardRawCallback cb);

    /**
     * @brief Enables or disables the key repeat functionality.
     *
     * @param enable true to enable key repeat, false to disable it.
     */
    void setRepeat(bool enable);

private:
    /**
     * @brief Updates the keyboard state and retrieves the currently pressed key.
     *
     * @param c A pointer to a character where the key value will be stored.
     * @return An integer representing the state of the key press.
     */
    int update(char *c);

    /**
     * @brief Prints debug information about the key press event.
     *
     * @param pressed true if the key is currently pressed, false if released.
     * @param k The key code of the key event.
     * @param keyVal The character value corresponding to the key code.
     */
    void printDebugInfo(bool pressed, uint8_t k, char keyVal);

    /**
     * @brief Handles special cases for space and null characters.
     *
     * @param keyVal The character value of the currently pressed key.
     * @param lastKeyVal A reference to the last key value.
     * @param pressed A reference to a boolean indicating if the key is pressed.
     * @return The processed character value.
     */
    char handleSpaceAndNullChar(char keyVal, char &lastKeyVal, bool &pressed);

    /**
     * @brief Converts a key code to its corresponding character value.
     *
     * @param k The key code.
     * @return The character value corresponding to the key code.
     */
    char getKeyChar(uint8_t k);

    /**
     * @brief Handles brightness adjustment based on key presses.
     *
     * @param k The key code of the key press.
     * @param pressed true if the key is pressed, false if released.
     * @return true if the brightness adjustment was handled, false otherwise.
     */
    bool handleBrightnessAdjustment(uint8_t k, bool pressed);

    /**
     * @brief Handles special keys and their associated actions.
     *
     * @param k The key code of the special key.
     * @param pressed true if the key is pressed, false if released.
     * @param c A pointer to a character to store the result of the special key action.
     * @return An integer representing the state of the special key action.
     */
    int handleSpecialKeys(uint8_t k, bool pressed, char *c);

    // Stores the last key value.
    char lastKeyVal = '\n';
    // The pin number for the backlight.
    int _backlight = -1;
    // The current brightness level of the backlight.
    uint8_t _brightness;
    // The interrupt request pin number.
    uint8_t _irq;
    // Flag indicating if the symbol key is pressed.
    bool symbol_key_pressed = false;
    // Flag indicating if the cap key is pressed.
    bool cap_key_pressed = false;
    // Flag indicating if the alt key is pressed.
    bool alt_key_pressed = false;
    // Flag indicating if the key repeat function is enabled.
    bool repeat_function = true;
    // The last state of the key press.
    bool lastState = false;
    // Pointer to the callback function.
    KeyboardReadCallback cb = NULL;
    // Pointer to the gpio change callback function.
    GpioEventCallback gpio_cb = NULL;
    // Pointer to the backlight change callback function.
    BacklightCallback bl_cb = NULL;
    // Pointer to the keyboard press or release callback function.
    KeyboardRawCallback raw_cb = NULL;
    // The time when the last key was pressed.
    uint32_t lastPressedTime = 0;
    // Pointer to the storage keyboard config
    const LilyGoKeyboardConfigure_t *_config;

};
#endif

