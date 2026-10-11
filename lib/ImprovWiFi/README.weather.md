# Vendored Improv library

Copied from cyd-web-installer copy-in at commit
748276006bf57ccb5e7445f7bf6822973cbc6b31 (Improv WiFi Library 0.0.2, MIT).
Shared fixes reset adjacent-frame parsing and start responses on a new line.
Weather adds bounds checks for the 128-byte receive buffer, short RPC payloads
and nested SSID/password lengths. Native sanitizer regressions live in
`tests/improv_protocol_test.cpp`; USB/WiFi are mocked there.
