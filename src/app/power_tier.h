#pragma once

/**
 * Power tier for low-battery degradation (brightness cap, GPS interval floor, audio block).
 * 0 = Normal, 1 = Low (<=20%), 2 = Critical (<=10%).
 * Set by handle_low_battery(); cleared to Normal when charging.
 */
int getPowerTier();
