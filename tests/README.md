# Local checks

Build both environments without secrets:

```sh
pio run -e esp32s3-7inch -e esp32s3-7inch-public
INSTALLER_TOOLS_ROOT=../cyd-web-installer python3 -m unittest discover -s tests -v
```

The native C++ tests use ArduinoJson from the installed PlatformIO dependency:

```sh
c++ -std=c++17 -I include -I .pio/libdeps/esp32s3-7inch/ArduinoJson/src tests/weather_validation_test.cpp -o /tmp/weather-validation
/tmp/weather-validation
c++ -std=c++17 -I include -I .pio/libdeps/esp32s3-7inch/ArduinoJson/src tests/device_settings_test.cpp -o /tmp/weather-settings
/tmp/weather-settings
c++ -std=c++17 -fsanitize=address,undefined -DARDUINO_ARCH_ESP32 -I tests/improv_stubs -I lib/ImprovWiFi/src tests/improv_protocol_test.cpp lib/ImprovWiFi/src/ImprovWiFiLibrary.cpp -o /tmp/weather-improv
/tmp/weather-improv
```

The Improv harness compiles the actual vendored parser but mocks its stream and
WiFi; it cannot verify USB task timing or physical network association. Native
settings tests cannot verify NVS persistence on hardware. See WEB_INSTALLER.md
and PHASE_2_REVIEW.md for the pending hardware tests.
