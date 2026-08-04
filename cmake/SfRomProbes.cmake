function(sf_add_game_probe target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE sf::game)
    sf_set_project_warnings(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Diagnostics/ROM")
endfunction()

if(SF_BUILD_ROM_PROBES)
    sf_add_game_probe(sf_h3_audio_probe apps/sf_h3_audio_probe.cpp)
    sf_add_game_probe(sf_h4_probe apps/sf_h4_probe.cpp)
    sf_add_game_probe(sf_h5_catalog_probe apps/sf_h5_catalog_probe.cpp)
    sf_add_game_probe(sf_h5_campaign_probe apps/sf_h5_campaign_probe.cpp)
    sf_add_game_probe(sf_h5_bootability_probe apps/sf_h5_bootability_probe.cpp)
    sf_add_game_probe(sf_g2_active_probe apps/sf_g2_active_probe.cpp)
    sf_add_game_probe(sf_g2_script_probe apps/sf_g2_script_probe.cpp)
    sf_add_game_probe(sf_g3_gameplay_probe apps/sf_g3_gameplay_probe.cpp)
    sf_add_game_probe(sf_g3_retail_spawn_probe apps/sf_g3_retail_spawn_probe.cpp)
    sf_add_game_probe(sf_retail_grenade_probe apps/sf_retail_grenade_probe.cpp)
    sf_add_game_probe(sf_g3_special_actor_probe apps/sf_g3_special_actor_probe.cpp)
    sf_add_game_probe(sf_g3_ai_combat_probe apps/sf_g3_ai_combat_probe.cpp)
    sf_add_game_probe(sf_g3_stealth_probe apps/sf_g3_stealth_probe.cpp)
    sf_add_game_probe(sf_g3_overlay_outcome_probe apps/sf_g3_overlay_outcome_probe.cpp)
    sf_add_game_probe(sf_g4_campaign_transition_probe
        apps/sf_g4_campaign_transition_probe.cpp)
    sf_add_game_probe(sf_retail_environment_probe
        apps/sf_retail_environment_probe.cpp)
    sf_add_game_probe(sf_retail_prop_state_probe
        apps/sf_retail_prop_state_probe.cpp)
    sf_add_game_probe(sf_sf2_native_mission_probe
        apps/sf_sf2_native_mission_probe.cpp)
endif()

function(sf_add_supported_rom_test name target labels timeout)
    add_test(NAME ${name} COMMAND ${target} "${SF_SUPPORTED_ROM_CUE}")
    set_tests_properties(${name} PROPERTIES
        LABELS "${labels}"
        TIMEOUT ${timeout})
endfunction()

function(sf_register_supported_rom_tests)
    if(SF_BUILD_ROM_PROBES AND SF_SUPPORTED_ROM_CUE)
        sf_add_supported_rom_test(sf_h5_bootability_rom sf_h5_bootability_probe
            "rom;h5;g1;g2.1" 300)
        sf_add_supported_rom_test(sf_h5_catalog_rom sf_h5_catalog_probe
            "rom;h5;g4;g4.3;catalog" 300)
        sf_add_supported_rom_test(sf_h5_campaign_rom sf_h5_campaign_probe
            "rom;h5;g4;g4.3;campaign" 600)
        sf_add_supported_rom_test(sf_g2_active_rom sf_g2_active_probe
            "rom;g2;g2.2" 600)
        sf_add_supported_rom_test(sf_g2_script_rom sf_g2_script_probe
            "rom;g2;g2.3" 600)
        sf_add_supported_rom_test(sf_g3_gameplay_rom sf_g3_gameplay_probe
            "rom;g3;g3.1" 900)
        sf_add_supported_rom_test(sf_g3_retail_spawn_rom sf_g3_retail_spawn_probe
            "rom;g3;g3.2" 300)
        sf_add_supported_rom_test(sf_g3_special_actor_rom sf_g3_special_actor_probe
            "rom;g3;g3.3" 300)
        sf_add_supported_rom_test(sf_g3_ai_combat_rom sf_g3_ai_combat_probe
            "rom;g3;g3.4" 900)
        sf_add_supported_rom_test(sf_g3_stealth_rom sf_g3_stealth_probe
            "rom;g3;g3.4;stealth" 900)
        sf_add_supported_rom_test(sf_g3_overlay_outcome_rom
            sf_g3_overlay_outcome_probe "rom;g3;g3.5" 300)
        sf_add_supported_rom_test(sf_g4_campaign_transition_rom
            sf_g4_campaign_transition_probe "rom;g4;g4.3;campaign;checkpoint" 900)
        sf_add_supported_rom_test(sf_retail_environment_rom
            sf_retail_environment_probe "rom;rendering;environment;effects" 900)
        sf_add_supported_rom_test(sf_retail_grenade_rom sf_retail_grenade_probe
            "rom;gameplay;rendering;grenade" 300)
    endif()

    if(SF3_SUPPORTED_ROM_CUE)
        add_test(NAME sf3_product_runtime_rom
            COMMAND "${CMAKE_COMMAND}"
                "-DSF_PROBE=$<TARGET_FILE:sf_tool>"
                "-DSF_ROM_CUE=${SF3_SUPPORTED_ROM_CUE}"
                "-DSF_FRAMES=300"
                -P "${CMAKE_SOURCE_DIR}/cmake/RunSf3ProductRuntimeProbe.cmake")
        set_tests_properties(sf3_product_runtime_rom PROPERTIES
            LABELS "rom;sf3;s3;product-runtime;deterministic"
            TIMEOUT 900)
    endif()
endfunction()
