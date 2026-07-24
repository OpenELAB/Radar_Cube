# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

idf_component_register(
    SRCS
        "main.cpp"
        "ble_standby_scanner.cpp"
        "low_power_policy.cpp"
        "peer_manager.cpp"
        "wake_session_controller.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES framework-arduinoespressif32 bt nvs_flash esp_wifi esp_pm
)
