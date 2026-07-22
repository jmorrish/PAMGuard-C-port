cmake_minimum_required(VERSION 3.20)

project(pamguard_cpp_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

enable_testing()

find_package(Python3 COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pamguard_engine
    src/core/AnalysisSession.cpp
    src/core/SessionManager.cpp
    src/detectors/BasicClickClassifier.cpp
    src/detectors/ClickDetectorEngine.cpp
    src/detectors/ClickFeatureExtractor.cpp
    src/detectors/ClickTrainTracker.cpp
    src/detectors/MhtIdiChi2.cpp
    src/detectors/CtTrainSpectrum.cpp
    src/detectors/MhtSimpleChi2Vars.cpp
    src/detectors/StandardMhtChi2.cpp
    src/detectors/ConnectedRegionTracker.cpp
    src/detectors/WhistlePeakDetector.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpectrogramEngine.cpp
    src/dsp/WindowFunction.cpp
    src/io/WavReader.cpp
    src/localisation/ArrayShape.cpp
    src/localisation/BearingLocaliserSelector.cpp
    src/localisation/CorrelationDelayEstimator.cpp
    src/localisation/DelayGroupEstimator.cpp
    src/localisation/FarFieldBearingLocaliser.cpp
    src/localisation/JamaMatrix.cpp
    src/localisation/LsqBearingLocaliser.cpp
    src/localisation/MlGridBearingLocaliser.cpp
    src/localisation/PairBearingLocaliser.cpp
    src/localisation/StreamerOrientation.cpp
    src/localisation/WhistleDelayEstimator.cpp
    src/localisation/WorldVectors.cpp
)

target_include_directories(pamguard_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(pamguard_engine PUBLIC cxx_std_20)

add_executable(pamguard_engine_cli
    tools/pamguard_engine_cli.cpp
)

target_link_libraries(pamguard_engine_cli PRIVATE pamguard_engine)

add_executable(spectrogram_file_cli
    tools/spectrogram_file_cli.cpp
)

target_link_libraries(spectrogram_file_cli PRIVATE pamguard_engine)

add_executable(raw_pcm_stream_cli
    tools/raw_pcm_stream_cli.cpp
)

target_link_libraries(raw_pcm_stream_cli PRIVATE pamguard_engine)

add_executable(ffmpeg_stream_ingest
    tools/ffmpeg_stream_ingest.cpp
)

target_include_directories(ffmpeg_stream_ingest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(ffmpeg_stream_ingest PRIVATE pamguard_engine)

add_executable(pamguard_engine_service
    tools/pamguard_engine_service.cpp
)

target_include_directories(pamguard_engine_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(pamguard_engine_service PRIVATE pamguard_engine)

if(WIN32)
    target_link_libraries(pamguard_engine_service PRIVATE ws2_32)
    target_link_libraries(ffmpeg_stream_ingest PRIVATE ws2_32)
endif()

add_executable(window_fixture_check
    tools/window_fixture_check.cpp
)

target_link_libraries(window_fixture_check PRIVATE pamguard_engine)

add_executable(spectrogram_chunking_check
    tools/spectrogram_chunking_check.cpp
)

target_link_libraries(spectrogram_chunking_check PRIVATE pamguard_engine)

add_executable(fft_fixture_check
    tools/fft_fixture_check.cpp
)

target_link_libraries(fft_fixture_check PRIVATE pamguard_engine)

add_executable(pamfft_frame_fixture_check
    tools/pamfft_frame_fixture_check.cpp
)

target_link_libraries(pamfft_frame_fixture_check PRIVATE pamguard_engine)

add_executable(click_trigger_fixture_check
    tools/click_trigger_fixture_check.cpp
)

target_link_libraries(click_trigger_fixture_check PRIVATE pamguard_engine)

add_executable(click_feature_fixture_check
    tools/click_feature_fixture_check.cpp
)

target_link_libraries(click_feature_fixture_check PRIVATE pamguard_engine)

add_executable(click_basic_classifier_fixture_check
    tools/click_basic_classifier_fixture_check.cpp
)

target_link_libraries(click_basic_classifier_fixture_check PRIVATE pamguard_engine)

add_executable(click_train_tracker_check
    tools/click_train_tracker_check.cpp
)

target_link_libraries(click_train_tracker_check PRIVATE pamguard_engine)

add_executable(click_train_idi_fixture_check
    tools/click_train_idi_fixture_check.cpp
)

target_link_libraries(click_train_idi_fixture_check PRIVATE pamguard_engine)

add_executable(mht_idi_chi2_fixture_check
    tools/mht_idi_chi2_fixture_check.cpp
)

target_link_libraries(mht_idi_chi2_fixture_check PRIVATE pamguard_engine)

add_executable(mht_simple_chi2_vars_fixture_check
    tools/mht_simple_chi2_vars_fixture_check.cpp
)

target_link_libraries(mht_simple_chi2_vars_fixture_check PRIVATE pamguard_engine)

add_executable(mht_bearing_chi2_fixture_check
    tools/mht_bearing_chi2_fixture_check.cpp
)

target_link_libraries(mht_bearing_chi2_fixture_check PRIVATE pamguard_engine)

add_executable(ct_chi2_classifier_check
    tools/ct_chi2_classifier_check.cpp
)

target_link_libraries(ct_chi2_classifier_check PRIVATE pamguard_engine)

add_executable(ct_classifiers_check
    tools/ct_classifiers_check.cpp
)

target_link_libraries(ct_classifiers_check PRIVATE pamguard_engine)

add_executable(ct_spectrum_template_check
    tools/ct_spectrum_template_check.cpp
)

target_link_libraries(ct_spectrum_template_check PRIVATE pamguard_engine)

add_executable(whistle_grouper_check
    tools/whistle_grouper_check.cpp
)

target_link_libraries(whistle_grouper_check PRIVATE pamguard_engine)

add_executable(mht_time_delay_chi2_fixture_check
    tools/mht_time_delay_chi2_fixture_check.cpp
)

target_link_libraries(mht_time_delay_chi2_fixture_check PRIVATE pamguard_engine)

add_executable(mht_kernel_fixture_check
    tools/mht_kernel_fixture_check.cpp
)

target_link_libraries(mht_kernel_fixture_check PRIVATE pamguard_engine)

add_executable(standard_mht_chi2_fixture_check
    tools/standard_mht_chi2_fixture_check.cpp
)

target_link_libraries(standard_mht_chi2_fixture_check PRIVATE pamguard_engine)

add_executable(click_train_bearing_summary_check
    tools/click_train_bearing_summary_check.cpp
)

target_link_libraries(click_train_bearing_summary_check PRIVATE pamguard_engine)

add_executable(click_train_localisation_summary_check
    tools/click_train_localisation_summary_check.cpp
)

target_link_libraries(click_train_localisation_summary_check PRIVATE pamguard_engine)

add_executable(session_manager_check
    tools/session_manager_check.cpp
)

target_link_libraries(session_manager_check PRIVATE pamguard_engine)

add_executable(correlation_delay_fixture_check
    tools/correlation_delay_fixture_check.cpp
)

target_link_libraries(correlation_delay_fixture_check PRIVATE pamguard_engine)

add_executable(delay_group_fixture_check
    tools/delay_group_fixture_check.cpp
)

target_link_libraries(delay_group_fixture_check PRIVATE pamguard_engine)

add_executable(far_field_bearing_check
    tools/far_field_bearing_check.cpp
)

target_link_libraries(far_field_bearing_check PRIVATE pamguard_engine)

add_executable(pair_bearing_fixture_check
    tools/pair_bearing_fixture_check.cpp
)

target_link_libraries(pair_bearing_fixture_check PRIVATE pamguard_engine)

add_executable(lsq_bearing_fixture_check
    tools/lsq_bearing_fixture_check.cpp
)

target_link_libraries(lsq_bearing_fixture_check PRIVATE pamguard_engine)

add_executable(session_lsq_bearing_check
    tools/session_lsq_bearing_check.cpp
)

target_link_libraries(session_lsq_bearing_check PRIVATE pamguard_engine)

add_executable(pair_bearing_physical_check
    tools/pair_bearing_physical_check.cpp
)

target_link_libraries(pair_bearing_physical_check PRIVATE pamguard_engine)

add_executable(array_shape_fixture_check
    tools/array_shape_fixture_check.cpp
)

target_link_libraries(array_shape_fixture_check PRIVATE pamguard_engine)

add_executable(world_vector_fixture_check
    tools/world_vector_fixture_check.cpp
)

target_link_libraries(world_vector_fixture_check PRIVATE pamguard_engine)

add_executable(ml_grid_bearing_fixture_check
    tools/ml_grid_bearing_fixture_check.cpp
)

target_link_libraries(ml_grid_bearing_fixture_check PRIVATE pamguard_engine)

add_executable(bearing_localiser_selector_check
    tools/bearing_localiser_selector_check.cpp
)

target_link_libraries(bearing_localiser_selector_check PRIVATE pamguard_engine)

add_executable(streamer_orientation_fixture_check
    tools/streamer_orientation_fixture_check.cpp
)

target_link_libraries(streamer_orientation_fixture_check PRIVATE pamguard_engine)

add_executable(whistle_delay_fixture_check
    tools/whistle_delay_fixture_check.cpp
)

target_link_libraries(whistle_delay_fixture_check PRIVATE pamguard_engine)

add_executable(session_whistle_delay_check
    tools/session_whistle_delay_check.cpp
)

target_link_libraries(session_whistle_delay_check PRIVATE pamguard_engine)

add_executable(session_mht_train_check
    tools/session_mht_train_check.cpp
)

target_link_libraries(session_mht_train_check PRIVATE pamguard_engine)

add_executable(whistle_peak_fixture_check
    tools/whistle_peak_fixture_check.cpp
)

target_link_libraries(whistle_peak_fixture_check PRIVATE pamguard_engine)

add_executable(connected_region_fixture_check
    tools/connected_region_fixture_check.cpp
)

target_link_libraries(connected_region_fixture_check PRIVATE pamguard_engine)

add_executable(connected_region_stub_fixture_check
    tools/connected_region_stub_fixture_check.cpp
)

target_link_libraries(connected_region_stub_fixture_check PRIVATE pamguard_engine)

add_executable(connected_region_discard_fixture_check
    tools/connected_region_discard_fixture_check.cpp
)

target_link_libraries(connected_region_discard_fixture_check PRIVATE pamguard_engine)

add_executable(connected_region_fragment_fixture_check
    tools/connected_region_fragment_fixture_check.cpp
)

target_link_libraries(connected_region_fragment_fixture_check PRIVATE pamguard_engine)

add_executable(connected_region_rejoin_fixture_check
    tools/connected_region_rejoin_fixture_check.cpp
)

target_link_libraries(connected_region_rejoin_fixture_check PRIVATE pamguard_engine)

add_test(
    NAME window_hann_1024_parity
    COMMAND window_fixture_check 2 1024 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/hann-1024.csv
)

add_test(
    NAME window_rectangular_8_parity
    COMMAND window_fixture_check 0 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/rectangular-8.csv
)

add_test(
    NAME window_hamming_8_parity
    COMMAND window_fixture_check 1 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/hamming-8.csv
)

add_test(
    NAME window_hann_8_parity
    COMMAND window_fixture_check 2 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/hann-8.csv
)

add_test(
    NAME window_bartlett_8_parity
    COMMAND window_fixture_check 3 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/bartlett-8.csv
)

add_test(
    NAME window_blackman_8_parity
    COMMAND window_fixture_check 4 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/blackman-8.csv
)

add_test(
    NAME window_blackman_harris_8_parity
    COMMAND window_fixture_check 5 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/window/blackman-harris-8.csv
)

add_test(
    NAME spectrogram_chunking_invariance
    COMMAND spectrogram_chunking_check
)

add_test(
    NAME fft_hann_8_parity
    COMMAND fft_fixture_check 2 8 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fft/hann-8.csv
)

add_test(
    NAME fft_hann_1024_parity
    COMMAND fft_fixture_check 2 1024 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fft/hann-1024.csv
)

add_test(
    NAME pamfft_frame_hann_8_hop4_parity
    COMMAND pamfft_frame_fixture_check 2 8 4 16 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pamfft-frame/hann-8-hop-4-sr-8000-chunk-16.csv
)

add_test(
    NAME click_trigger_basic_2ch_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/basic-2ch-threshold10.csv
)

add_test(
    NAME click_trigger_double_split_minsep8_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/double-split-minsep8.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 8 128 1 48000 256 double-transient 2
)

add_test(
    NAME click_trigger_double_merge_minsep16_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/double-merge-minsep16.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 16 128 1 48000 256 double-transient 1
)

add_test(
    NAME click_trigger_long_maxlen64_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/long-maxlen64.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 8 64 1 48000 1024 long-transient 1
)

add_test(
    NAME click_trigger_single_channel_min1_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/single-channel-min1.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 8 128 1 48000 256 single-channel-transient 1
)

add_test(
    NAME click_trigger_single_channel_min2_suppressed_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/single-channel-min2-suppressed.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 8 128 2 48000 256 single-channel-transient 0
)

add_test(
    NAME click_trigger_threshold6_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/single-threshold6.csv
        0x3 0x3 6.0 0.2 0.0001 10 12 8 128 1 48000 256 single-transient 1
)

add_test(
    NAME click_feature_basic_parity
    COMMAND click_feature_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-feature/basic-features.csv
)

add_test(
    NAME click_basic_classifier_parity
    COMMAND click_basic_classifier_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-classifier/basic-click-classifier.csv
)

add_test(
    NAME click_train_tracker_foundation
    COMMAND click_train_tracker_check
)

add_test(
    NAME click_train_idi_stats_parity
    COMMAND click_train_idi_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/idi-stats.csv
)

add_test(
    NAME mht_idi_chi2_parity
    COMMAND mht_idi_chi2_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/mht-idi-chi2.csv
)

add_test(
    NAME mht_simple_chi2_vars_parity
    COMMAND mht_simple_chi2_vars_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/mht-simple-chi2-vars.csv
)

add_test(
    NAME mht_bearing_chi2_parity
    COMMAND mht_bearing_chi2_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/mht-bearing-chi2.csv
)

add_test(
    NAME ct_chi2_classifier_branches
    COMMAND ct_chi2_classifier_check
)

add_test(
    NAME ct_idi_bearing_classifier_branches
    COMMAND ct_classifiers_check
)

add_test(
    NAME ct_spectrum_template_parity
    COMMAND ct_spectrum_template_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/spectrum-templates.csv
)

add_test(
    NAME whistle_grouper_coverage
    COMMAND whistle_grouper_check
)

add_test(
    NAME mht_time_delay_chi2_parity
    COMMAND mht_time_delay_chi2_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/mht-time-delay-chi2.csv
)

add_test(
    NAME mht_kernel_parity
    COMMAND mht_kernel_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/mht-kernel.csv
)

add_test(
    NAME standard_mht_chi2_stack_parity
    COMMAND standard_mht_chi2_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/standard-mht-chi2.csv
)

add_test(
    NAME click_train_bearing_summary_foundation
    COMMAND click_train_bearing_summary_check
)

add_test(
    NAME click_train_localisation_summary_foundation
    COMMAND click_train_localisation_summary_check
)

add_test(
    NAME session_manager_instanced_pipeline
    COMMAND session_manager_check
)

add_test(
    NAME correlation_delay_basic_parity
    COMMAND correlation_delay_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/correlation-delay-basic.csv
)

add_test(
    NAME delay_group_3ch_basic_parity
    COMMAND delay_group_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/delay-group-3ch-basic.csv
)

add_test(
    NAME far_field_bearing_foundation
    COMMAND far_field_bearing_check
)

add_test(
    NAME pair_bearing_basic_parity
    COMMAND pair_bearing_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/pair-bearing.csv
)

add_test(
    NAME lsq_bearing_basic_parity
    COMMAND lsq_bearing_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/lsq-bearing.csv
)

add_test(
    NAME session_lsq_bearing_wiring
    COMMAND session_lsq_bearing_check
)

add_test(
    NAME pair_bearing_physical_consistency
    COMMAND pair_bearing_physical_check
)

add_test(
    NAME array_shape_basic_parity
    COMMAND array_shape_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/array-shape.csv
)

add_test(
    NAME world_vector_parity
    COMMAND world_vector_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/world-vector.csv
)

add_test(
    NAME earth_vector_parity
    COMMAND world_vector_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/earth-vector.csv earth
)

add_test(
    NAME ml_grid_bearing_parity
    COMMAND ml_grid_bearing_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/ml-grid-bearing.csv
)

add_test(
    NAME ml_line_bearing_parity
    COMMAND ml_grid_bearing_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/ml-line-bearing.csv line
)

add_test(
    NAME bearing_localiser_selection
    COMMAND bearing_localiser_selector_check
)

add_test(
    NAME streamer_orientation_parity
    COMMAND streamer_orientation_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/streamer-orientation.csv
)

add_test(
    NAME whistle_delay_basic_parity
    COMMAND whistle_delay_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/whistle-delay.csv
)

add_test(
    NAME session_whistle_delay_wiring
    COMMAND session_whistle_delay_check
)

add_test(
    NAME session_mht_train_wiring
    COMMAND session_mht_train_check
)

add_test(
    NAME whistle_peak_basic_parity
    COMMAND whistle_peak_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/better-peak-basic.csv
)

add_test(
    NAME connected_region_basic_parity
    COMMAND connected_region_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-basic.csv
)

add_test(
    NAME connected_region_flush_parity
    COMMAND connected_region_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-basic.csv flush
)

add_test(
    NAME connected_region_stub_removal_parity
    COMMAND connected_region_stub_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-stub-removal.csv
)

add_test(
    NAME connected_region_discard_fragmenter_parity
    COMMAND connected_region_discard_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-discard.csv
)

add_test(
    NAME connected_region_branch_fragmenter_parity
    COMMAND connected_region_fragment_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-fragment.csv
)

add_test(
    NAME connected_region_rejoin_fragmenter_parity
    COMMAND connected_region_rejoin_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-rejoin.csv
)

add_test(
    NAME connected_region_rejoin_cross_fragmenter_parity
    COMMAND connected_region_rejoin_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-rejoin-cross.csv cross
)

add_test(
    NAME connected_region_rejoin_split_fragmenter_parity
    COMMAND connected_region_rejoin_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/connected-region-rejoin-split.csv split
)

add_test(
    NAME ffmpeg_stream_ingest_help
    COMMAND ffmpeg_stream_ingest --help
)
add_test(
    NAME ffmpeg_stream_ingest_help_input_option
    COMMAND ffmpeg_stream_ingest --help
)
add_test(
    NAME ffmpeg_stream_ingest_help_source_id
    COMMAND ffmpeg_stream_ingest --help
)
set_tests_properties(
    ffmpeg_stream_ingest_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--api-key-env"
)
set_tests_properties(
    ffmpeg_stream_ingest_help_input_option
    PROPERTIES PASS_REGULAR_EXPRESSION "--ffmpeg-input-option"
)
set_tests_properties(
    ffmpeg_stream_ingest_help_source_id
    PROPERTIES PASS_REGULAR_EXPRESSION "--source-id"
)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME archive_event_index_rebuild_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/rebuild_archive_event_index_smoke.py
    )
    add_test(
        NAME archive_retention_grouped_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/archive_retention_smoke.py
    )
    add_test(
        NAME ingest_supervisor_status_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/ingest_supervisor_status_smoke.py
    )
    add_test(
        NAME ingest_supervisor_command_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/ingest_supervisor_command_smoke.py
    )
endif()

if(WIN32)
    find_program(PAMGUARD_POWERSHELL NAMES powershell pwsh)
    if(PAMGUARD_POWERSHELL)
        add_test(
            NAME service_smoke_http
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/service-smoke.ps1 -Port 18180 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME service_smoke_api_key
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/service-smoke.ps1 -Port 18181 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -ApiKey dev-secret
        )
        add_test(
            NAME service_smoke_api_key_file
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/service-smoke.ps1 -Port 18183 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -ApiKey dev-secret -ApiKeyFile
        )
        add_test(
            NAME service_load_smoke_foundation
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/service-load-smoke.ps1 -Port 18182 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -Sessions 4 -Chunks 2
        )
        set_tests_properties(
            service_smoke_http
            service_smoke_api_key
            service_smoke_api_key_file
            service_load_smoke_foundation
            PROPERTIES TIMEOUT 90
        )
    endif()
endif()
