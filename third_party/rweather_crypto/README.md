# rweather Crypto subset

Unmodified files from [rweather/arduinolibs](https://github.com/rweather/arduinolibs/tree/37a76b8f7516568e1c575b6dc9268da1ccaac6b6/libraries/Crypto), commit `37a76b8f7516568e1c575b6dc9268da1ccaac6b6`.

This subset contains SHA-256, the Hash HMAC helpers, Crypto utilities, and the three utility headers they require. Each source file retains its upstream license notice (MIT terms, Southern Storm Software).

ESP-IDF builds and Reticulum host tests use these files for interface discovery stamp verification and IFAC. Arduino builds continue to use their declared Crypto library dependency. Vendoring this minimal subset avoids requiring an Arduino library installation or a network download during ESP-IDF configuration.
