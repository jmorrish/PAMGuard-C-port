cmake_minimum_required(VERSION 3.20)

project(pamguard_cpp_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

enable_testing()

find_package(Python3 COMPONENTS Interpreter)
find_program(PAMGUARD_NODE NAMES node)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pamguard_engine
    src/core/AnalysisSession.cpp
    src/core/BuiltInModules.cpp
    src/core/ClipGeneratorSettings.cpp
    src/core/DataModel.cpp
    src/core/DetectorNodes.cpp
    src/core/FftDetectorNodes.cpp
    src/core/FilterDecimatorSettings.cpp
    src/core/GroupedSource.cpp
    src/core/IshmaelSettings.cpp
    src/core/LevelMeterSettings.cpp
    src/core/LocalisationNodes.cpp
    src/core/MatchedTemplateSettings.cpp
    src/core/MhtClickTrainSettings.cpp
    src/core/ModuleGraph.cpp
    src/core/ModuleGraphJson.cpp
    src/core/ModuleRuntime.cpp
    src/core/NoiseLtsaSettings.cpp
    src/core/OperatorNodes.cpp
    src/core/SessionManager.cpp
    src/core/SignalNodes.cpp
    src/core/SignalRoutingSettings.cpp
    src/core/SoundRecorderSettings.cpp
    src/core/WhistleMoanSettings.cpp
    src/project/AlarmEffortControlledUnits.cpp
    src/project/AuralListeningControlledUnit.cpp
    src/project/BuiltInControlledUnits.cpp
    src/project/IshmaelControlledUnits.cpp
    src/project/CanonicalJson.cpp
    src/project/ClickDetectorControlledUnit.cpp
    src/project/ClipGeneratorControlledUnit.cpp
    src/project/ControlledUnitJson.cpp
    src/project/ControlledUnitRegistry.cpp
    src/project/FilterDecimatorControlledUnits.cpp
    src/project/GlobalSettingsAdapters.cpp
    src/project/LevelMeterControlledUnit.cpp
    src/project/MatchedTemplateControlledUnit.cpp
    src/project/MhtClickTrainControlledUnit.cpp
    src/project/NoiseLtsaControlledUnits.cpp
    src/project/ProjectAuthority.cpp
    src/project/ProjectAuthorityJson.cpp
    src/project/ProjectIdentity.cpp
    src/project/ProjectJson.cpp
    src/project/ProjectMigration.cpp
    src/project/ProjectProjection.cpp
    src/project/ProjectStore.cpp
    src/project/Sha256.cpp
    src/project/SignalRoutingControlledUnits.cpp
    src/project/SoundRecorderControlledUnit.cpp
    src/project/SoundOutputControlledUnit.cpp
    src/project/UserInputControlledUnit.cpp
    src/project/WhistleMoanControlledUnit.cpp
    src/detectors/BasicClickClassifier.cpp
    src/detectors/SweepClickClassifier.cpp
    src/detectors/ClickDetectorEngine.cpp
    src/detectors/ClickAngleVeto.cpp
    src/detectors/FftNoiseMonitor.cpp
    src/detectors/ClickFeatureExtractor.cpp
    src/detectors/ClickTrainTracker.cpp
    src/detectors/MhtIdiChi2.cpp
    src/detectors/CtTrainSpectrum.cpp
    src/detectors/MhtSimpleChi2Vars.cpp
    src/detectors/StandardMhtChi2.cpp
    src/detectors/ConnectedRegionTracker.cpp
    src/detectors/IshmaelDetector.cpp
    src/detectors/MatchedTemplateClassifier.cpp
    src/detectors/MatchFiltDetector.cpp
    src/detectors/SgramCorrDetector.cpp
    src/dsp/JtFft.cpp
    src/detectors/LtsaMonitor.cpp
    src/detectors/NoiseBandMonitor.cpp
    src/detectors/SpectrogramNoiseReducer.cpp
    src/detectors/SpectrumBackground.cpp
    src/detectors/WhistlePeakDetector.cpp
    src/dsp/ClickRemoval.cpp
    src/dsp/IirFilter.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpectrogramEngine.cpp
    src/dsp/WavInterpolator.cpp
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
    src/localisation/TrackedTargetMotion.cpp
    src/localisation/WhistleDelayEstimator.cpp
    src/localisation/WorldVectors.cpp
    src/service/CaptureService.cpp
    src/service/TrackedClickEvents.cpp
)

target_include_directories(pamguard_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
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

if(MSVC)
    # The single-file HTTP contract intentionally keeps request parsing,
    # validation, and serialization together; debug builds now exceed COFF's
    # default section count.
    target_compile_options(pamguard_engine_service PRIVATE /bigobj)
endif()

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

add_executable(click_detector_settings_check
    tools/click_detector_settings_check.cpp
)

target_link_libraries(click_detector_settings_check PRIVATE pamguard_engine)

add_executable(click_wav_parity_cli
    tools/click_wav_parity_cli.cpp
)

target_link_libraries(click_wav_parity_cli PRIVATE pamguard_engine)

add_executable(click_feature_fixture_check
    tools/click_feature_fixture_check.cpp
)

target_link_libraries(click_feature_fixture_check PRIVATE pamguard_engine)

add_executable(click_basic_classifier_fixture_check
    tools/click_basic_classifier_fixture_check.cpp
)

target_link_libraries(click_basic_classifier_fixture_check PRIVATE pamguard_engine)

add_executable(click_sweep_classifier_fixture_check
    tools/click_sweep_classifier_fixture_check.cpp
)

target_link_libraries(click_sweep_classifier_fixture_check PRIVATE pamguard_engine)

add_executable(click_angle_veto_fixture_check
    tools/click_angle_veto_fixture_check.cpp
)

target_link_libraries(click_angle_veto_fixture_check PRIVATE pamguard_engine)

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

add_executable(click_localiser_platform_pose_check
    tools/click_localiser_platform_pose_check.cpp
)

target_link_libraries(click_localiser_platform_pose_check PRIVATE pamguard_engine)

add_executable(session_manager_check
    tools/session_manager_check.cpp
)

target_link_libraries(session_manager_check PRIVATE pamguard_engine)

add_executable(module_graph_check
    tools/module_graph_check.cpp
)

target_include_directories(module_graph_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(module_graph_check PRIVATE pamguard_engine)

add_executable(controlled_unit_manifest_check
    tools/controlled_unit_manifest_check.cpp
)

target_include_directories(controlled_unit_manifest_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(controlled_unit_manifest_check PRIVATE pamguard_engine)

add_executable(project_document_check
    tools/project_document_check.cpp
)

target_link_libraries(project_document_check PRIVATE pamguard_engine)

add_executable(project_store_check
    tools/project_store_check.cpp
)

target_link_libraries(project_store_check PRIVATE pamguard_engine)

add_executable(project_identity_check
    tools/project_identity_check.cpp
)

target_link_libraries(project_identity_check PRIVATE pamguard_engine)

add_executable(controlled_unit_registry_check
    tools/controlled_unit_registry_check.cpp
)

target_include_directories(controlled_unit_registry_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(controlled_unit_registry_check PRIVATE pamguard_engine)

add_executable(controlled_unit_json_check
    tools/controlled_unit_json_check.cpp
)

target_include_directories(controlled_unit_json_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(controlled_unit_json_check PRIVATE pamguard_engine)

add_executable(filter_decimator_settings_check
    tools/filter_decimator_settings_check.cpp
)

target_include_directories(filter_decimator_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(filter_decimator_settings_check PRIVATE pamguard_engine)

add_executable(whistle_moan_settings_check
    tools/whistle_moan_settings_check.cpp
)

target_include_directories(whistle_moan_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(whistle_moan_settings_check PRIVATE pamguard_engine)

add_executable(mht_click_train_settings_check
    tools/mht_click_train_settings_check.cpp
)

target_include_directories(mht_click_train_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(mht_click_train_settings_check PRIVATE pamguard_engine)

add_executable(matched_template_settings_check
    tools/matched_template_settings_check.cpp
)

target_include_directories(matched_template_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(matched_template_settings_check PRIVATE pamguard_engine)

add_executable(noise_ltsa_controlled_units_check
    tools/noise_ltsa_controlled_units_check.cpp
)

target_include_directories(noise_ltsa_controlled_units_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(noise_ltsa_controlled_units_check PRIVATE pamguard_engine)

add_executable(level_meter_settings_check
    tools/level_meter_settings_check.cpp
)

target_include_directories(level_meter_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(level_meter_settings_check PRIVATE pamguard_engine)

add_executable(array_manager_settings_check
    tools/array_manager_settings_check.cpp
)

target_include_directories(array_manager_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(array_manager_settings_check PRIVATE pamguard_engine)

add_executable(sound_output_settings_check
    tools/sound_output_settings_check.cpp
)

target_include_directories(sound_output_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(sound_output_settings_check PRIVATE pamguard_engine)

add_executable(sound_recorder_settings_check
    tools/sound_recorder_settings_check.cpp
)

target_include_directories(sound_recorder_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(sound_recorder_settings_check PRIVATE pamguard_engine)

add_executable(sound_recorder_node_check
    tools/sound_recorder_node_check.cpp
)

target_link_libraries(sound_recorder_node_check PRIVATE pamguard_engine)

add_executable(clip_generator_settings_check
    tools/clip_generator_settings_check.cpp
)

target_include_directories(clip_generator_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(clip_generator_settings_check PRIVATE pamguard_engine)

add_executable(clip_generator_runtime_check
    tools/clip_generator_runtime_check.cpp
)

target_include_directories(clip_generator_runtime_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(clip_generator_runtime_check PRIVATE pamguard_engine)

add_executable(sound_recorder_controlled_unit_check
    tools/sound_recorder_controlled_unit_check.cpp
)

target_include_directories(sound_recorder_controlled_unit_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(
    sound_recorder_controlled_unit_check
    PRIVATE pamguard_engine
)

add_executable(clip_generator_controlled_unit_check
    tools/clip_generator_controlled_unit_check.cpp
)

target_include_directories(clip_generator_controlled_unit_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(
    clip_generator_controlled_unit_check
    PRIVATE pamguard_engine
)

add_executable(user_input_controlled_unit_check
    tools/user_input_controlled_unit_check.cpp
)

target_include_directories(user_input_controlled_unit_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(
    user_input_controlled_unit_check
    PRIVATE pamguard_engine
)

add_executable(aural_listening_controlled_unit_check
    tools/aural_listening_controlled_unit_check.cpp
)

target_include_directories(aural_listening_controlled_unit_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(
    aural_listening_controlled_unit_check
    PRIVATE pamguard_engine
)

add_executable(alarm_effort_controlled_units_check
    tools/alarm_effort_controlled_units_check.cpp
)

target_include_directories(alarm_effort_controlled_units_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(
    alarm_effort_controlled_units_check
    PRIVATE pamguard_engine
)

add_executable(project_projection_check
    tools/project_projection_check.cpp
)

target_include_directories(project_projection_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(project_projection_check PRIVATE pamguard_engine)

add_executable(ishmael_settings_check
    tools/ishmael_settings_check.cpp
)

target_include_directories(ishmael_settings_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(ishmael_settings_check PRIVATE pamguard_engine)

add_executable(project_authority_json_check
    tools/project_authority_json_check.cpp
)

target_include_directories(project_authority_json_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(project_authority_json_check PRIVATE pamguard_engine)

add_executable(project_authority_check
    tools/project_authority_check.cpp
)

target_link_libraries(project_authority_check PRIVATE pamguard_engine)

add_executable(capture_service_check
    tools/capture_service_check.cpp
)

target_link_libraries(capture_service_check PRIVATE pamguard_engine)

add_executable(capture_lifecycle_test_child
    tools/capture_lifecycle_test_child.cpp
)

add_executable(decimator_fixture_check
    tools/decimator_fixture_check.cpp
)

target_link_libraries(decimator_fixture_check PRIVATE pamguard_engine)

add_executable(signal_routing_fixture_check
    tools/signal_routing_fixture_check.cpp
)

target_include_directories(signal_routing_fixture_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(signal_routing_fixture_check PRIVATE pamguard_engine)

add_executable(tracked_click_events_check
    tools/tracked_click_events_check.cpp
)

target_include_directories(tracked_click_events_check
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(tracked_click_events_check PRIVATE pamguard_engine)

add_executable(tracked_target_motion_fixture_check
    tools/tracked_target_motion_fixture_check.cpp
)

target_link_libraries(
    tracked_target_motion_fixture_check
    PRIVATE pamguard_engine
)

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

add_executable(spectrogram_noise_fixture_check
    tools/spectrogram_noise_fixture_check.cpp
)

target_link_libraries(spectrogram_noise_fixture_check PRIVATE pamguard_engine)

add_executable(noise_band_fixture_check
    tools/noise_band_fixture_check.cpp
)

target_link_libraries(noise_band_fixture_check PRIVATE pamguard_engine)

add_executable(fft_noise_fixture_check
    tools/fft_noise_fixture_check.cpp
)
target_link_libraries(fft_noise_fixture_check PRIVATE pamguard_engine)

add_executable(spectrum_background_fixture_check
    tools/spectrum_background_fixture_check.cpp
)
target_link_libraries(spectrum_background_fixture_check PRIVATE pamguard_engine)

add_executable(grouped_source_fixture_check
    tools/grouped_source_fixture_check.cpp
)
target_link_libraries(grouped_source_fixture_check PRIVATE pamguard_engine)

add_executable(ltsa_fixture_check
    tools/ltsa_fixture_check.cpp
)

target_link_libraries(ltsa_fixture_check PRIVATE pamguard_engine)

add_executable(ishmael_fixture_check
    tools/ishmael_fixture_check.cpp
)

target_link_libraries(ishmael_fixture_check PRIVATE pamguard_engine)

add_executable(matched_template_fixture_check
    tools/matched_template_fixture_check.cpp
)

target_link_libraries(matched_template_fixture_check PRIVATE pamguard_engine)

add_executable(sgram_corr_fixture_check
    tools/sgram_corr_fixture_check.cpp
)

target_link_libraries(sgram_corr_fixture_check PRIVATE pamguard_engine)

add_executable(match_filt_fixture_check
    tools/match_filt_fixture_check.cpp
)

target_link_libraries(match_filt_fixture_check PRIVATE pamguard_engine)

add_executable(iir_filter_fixture_check
    tools/iir_filter_fixture_check.cpp
)

target_link_libraries(iir_filter_fixture_check PRIVATE pamguard_engine)

add_executable(simple_echo_fixture_check
    tools/simple_echo_fixture_check.cpp
)

target_link_libraries(simple_echo_fixture_check PRIVATE pamguard_engine)

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
    NAME pamfft_frame_click_removal_parity
    COMMAND pamfft_frame_fixture_check 2 16 8 32 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pamfft-frame/hann-16-hop-8-click-removal.csv true 5.0 6
)

add_test(
    NAME click_trigger_basic_2ch_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/basic-2ch-threshold10.csv
)

add_test(
    NAME click_detector_settings_parity
    COMMAND click_detector_settings_check
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
    NAME click_trigger_cross_chunk_parity
    COMMAND click_trigger_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-trigger/boundary-transient-chunk128.csv
        0x3 0x3 10.0 0.1 0.00001 10 12 8 128 1 48000 256 boundary-transient 1 128
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
    NAME click_sweep_classifier_parity
    COMMAND click_sweep_classifier_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-classifier/sweep-click-classifier.csv
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-classifier/sweep-click-defaults.csv
)

add_test(
    NAME click_angle_veto_parity
    COMMAND click_angle_veto_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-angle-veto/angle-veto.csv
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
    NAME click_localiser_platform_pose
    COMMAND click_localiser_platform_pose_check
)

add_test(
    NAME session_manager_instanced_pipeline
    COMMAND session_manager_check
)

add_test(
    NAME module_graph_and_typed_data_blocks
    COMMAND module_graph_check
)

add_test(
    NAME controlled_unit_parity_manifest
    COMMAND controlled_unit_manifest_check
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/controlled-unit-parity-manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/../PAMGuard_Java
)

add_test(
    NAME project_document_contract
    COMMAND project_document_check
)

add_test(
    NAME project_store_atomic_persistence
    COMMAND project_store_check
)

add_test(
    NAME project_identity_contract
    COMMAND project_identity_check
)

add_test(
    NAME controlled_unit_registry_first_slice
    COMMAND controlled_unit_registry_check
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/controlled-unit-parity-manifest.json
)

add_test(
    NAME controlled_unit_catalogue_json
    COMMAND controlled_unit_json_check
)

add_test(
    NAME filter_decimator_controlled_units
    COMMAND filter_decimator_settings_check
)

add_test(
    NAME whistle_moan_controlled_unit
    COMMAND whistle_moan_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/whistle-moan-settings-defaults.json
)

add_test(
    NAME mht_click_train_controlled_unit
    COMMAND mht_click_train_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-train/settings-defaults.json
)

add_test(
    NAME matched_template_controlled_unit
    COMMAND matched_template_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/matched-template/settings-defaults.json
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/matched-template/template-resample.csv
        ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/matched-template-default-templates.json
)

add_test(
    NAME noise_ltsa_controlled_units
    COMMAND noise_ltsa_controlled_units_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/noise-ltsa/settings-defaults.json
)

add_test(
    NAME level_meter_controlled_unit
    COMMAND level_meter_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/level-meter/settings-defaults.csv
)

add_test(
    NAME array_manager_global_settings
    COMMAND array_manager_settings_check
)

add_test(
    NAME sound_output_portable_settings
    COMMAND sound_output_settings_check
)

add_test(
    NAME sound_recorder_portable_settings
    COMMAND sound_recorder_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sound-recorder/settings-defaults.json
)

add_test(
    NAME sound_recorder_node_runtime
    COMMAND sound_recorder_node_check
)

add_test(
    NAME clip_generator_portable_settings
    COMMAND clip_generator_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clip-generator/settings-defaults.json
)

add_test(
    NAME clip_generator_runtime_parity
    COMMAND clip_generator_runtime_check
)

add_test(
    NAME sound_recorder_controlled_unit
    COMMAND sound_recorder_controlled_unit_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sound-recorder/settings-defaults.json
)

add_test(
    NAME clip_generator_controlled_unit
    COMMAND clip_generator_controlled_unit_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clip-generator/settings-defaults.json
)

add_test(
    NAME user_input_controlled_unit
    COMMAND user_input_controlled_unit_check
)

add_test(
    NAME aural_listening_controlled_unit
    COMMAND aural_listening_controlled_unit_check
)

add_test(
    NAME alarm_effort_controlled_units
    COMMAND alarm_effort_controlled_units_check
)

add_test(
    NAME project_projection_first_slice
    COMMAND project_projection_check
)

add_test(
    NAME ishmael_controlled_units
    COMMAND ishmael_settings_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ishmael/ishmael-settings.json
)

add_test(
    NAME project_authority_json_contract
    COMMAND project_authority_json_check
)

add_test(
    NAME project_authority_transactions
    COMMAND project_authority_check
)

add_test(
    NAME capture_service_command_trust_and_reaping
    COMMAND capture_service_check
)

add_test(
    NAME decimator_java_stream_parity
    COMMAND decimator_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/decimator/decimator-stream.csv
)

add_test(
    NAME signal_routing_java_parity
    COMMAND signal_routing_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/signal-routing/signal-routing.csv
)

add_test(
    NAME tracked_click_events_java_semantics
    COMMAND tracked_click_events_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click-localisation/tracked-click-params.csv
)

add_test(
    NAME tracked_target_motion_java_parity
    COMMAND tracked_target_motion_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/tracked-target-motion.csv
)

add_test(
    NAME correlation_delay_basic_parity
    COMMAND correlation_delay_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/correlation-delay-basic.csv
)

add_test(
    NAME delay_group_3ch_basic_parity
    COMMAND delay_group_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/delay-group-3ch-basic.csv
)
foreach(delay_mode restricted upsample filter envelope leading combined)
    add_test(
        NAME delay_group_3ch_${delay_mode}_parity
        COMMAND delay_group_fixture_check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/localisation/delay-group-3ch-${delay_mode}.csv
            ${delay_mode}
    )
endforeach()

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
    NAME spectrogram_noise_parity
    COMMAND spectrogram_noise_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/whistle/spectrogram-noise.csv
)

add_test(
    NAME noise_band_parity
    COMMAND noise_band_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/noise/noise-bands.csv
)

add_test(
    NAME fft_noise_monitor_parity
    COMMAND fft_noise_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fft-noise/fft-noise.csv
)

add_test(
    NAME spectrum_background_parity
    COMMAND spectrum_background_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/spectrum-background/spectrum-background.csv
)

add_test(
    NAME grouped_source_parity
    COMMAND grouped_source_fixture_check
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/grouped-source/grouped-source.csv
)

add_test(
    NAME ltsa_parity
    COMMAND ltsa_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ltsa/ltsa.csv
)

add_test(
    NAME ishmael_parity
    COMMAND ishmael_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ishmael/ishmael-energy-sum.csv
)

add_test(
    NAME matched_template_parity
    COMMAND matched_template_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/matched-template/matched-template.csv
)

add_test(
    NAME sgram_corr_parity
    COMMAND sgram_corr_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sgram-corr/sgram-corr.csv
)

add_test(
    NAME match_filt_parity
    COMMAND match_filt_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/match-filt/match-filt.csv
)

add_test(
    NAME iir_filter_parity
    COMMAND iir_filter_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click/iir-filter.csv
)

add_test(
    NAME simple_echo_parity
    COMMAND simple_echo_fixture_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/click/simple-echo.csv
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
set_tests_properties(
    session_manager_instanced_pipeline
    session_lsq_bearing_wiring
    session_whistle_delay_wiring
    session_mht_train_wiring
    PROPERTIES LABELS "compatibility;legacy-session;scientific-parity"
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
add_test(
    NAME ffmpeg_stream_ingest_help_module
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
set_tests_properties(
    ffmpeg_stream_ingest_help_module
    PROPERTIES PASS_REGULAR_EXPRESSION "--module <id>"
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
    set_tests_properties(
        archive_event_index_rebuild_smoke
        archive_retention_grouped_smoke
        PROPERTIES LABELS "archive;compatibility;legacy-session"
    )
    add_test(
        NAME ingest_supervisor_status_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/ingest_supervisor_status_smoke.py
    )
    add_test(
        NAME ingest_supervisor_command_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/ingest_supervisor_command_smoke.py
    )
    add_test(
        NAME ingest_supervisor_project_api_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../ops/ingest_supervisor_project_smoke.py
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
        add_test(
            NAME service_job_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/service-job-smoke.ps1 -Port 18189 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME project_authority_service_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-service-smoke.ps1 -Port 18201 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME project_acquisition_http_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-acquisition-http-smoke.ps1 -Port 18204 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -IngestExe $<TARGET_FILE:ffmpeg_stream_ingest> -ChildExe $<TARGET_FILE:capture_lifecycle_test_child>
        )
        add_test(
            NAME project_sound_recorder_http_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-sound-recorder-http-smoke.ps1 -Port 18205 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME project_tracked_click_localisation_http_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-tracked-click-localisation-http-smoke.ps1 -Port 18206 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME module_runtime_http_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/module-runtime-smoke.ps1 -Port 18191 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME module_runtime_concurrency_soak
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/module-runtime-soak.ps1 -Port 18194 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -DurationSeconds 5
        )
        add_test(
            NAME project_runtime_concurrency_soak
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-runtime-soak.ps1 -Port 18207 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -DurationSeconds 5
        )
        set_tests_properties(
            project_runtime_concurrency_soak
            PROPERTIES
                LABELS "project-authority;runtime;soak"
                TIMEOUT 60
        )
        add_test(
            NAME capture_service_http_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture-service-http-smoke.ps1 -Port 18198 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME capture_lifecycle_browser_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/capture-lifecycle-browser-smoke.ps1 -Port 18199 -DebugPort 19229 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -ChildExe $<TARGET_FILE:capture_lifecycle_test_child>
        )
        set_tests_properties(
            capture_lifecycle_browser_smoke
            PROPERTIES
                LABELS "browser;capture;compatibility;legacy-session;lifecycle"
                TIMEOUT 120
        )
        add_test(
            NAME web_assets_service_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/web-assets-service-smoke.ps1 -Port 18197 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        add_test(
            NAME pamguard_operator_shell_browser_contract
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pamguard-operator-shell-contract.ps1 -Port 18196 -DebugPort 19228 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service> -EnforceTarget
        )
        add_test(
            NAME project_shell_browser_smoke
            COMMAND ${PAMGUARD_POWERSHELL} -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/project-shell-browser-smoke.ps1 -Port 18203 -DebugPort 19231 -BuildDir $<TARGET_FILE_DIR:pamguard_engine_service>
        )
        set_tests_properties(
            pamguard_operator_shell_browser_contract
            PROPERTIES
                LABELS "browser;compatibility-isolation;operator-shell;target-contract"
                TIMEOUT 90
        )
        set_tests_properties(
            project_shell_browser_smoke
            PROPERTIES
                LABELS "browser;operator-shell;project-authority"
                TIMEOUT 120
        )
        set_tests_properties(
            project_authority_service_smoke
            PROPERTIES
                LABELS "compatibility-isolation;project-authority;service"
        )
        set_tests_properties(
            service_smoke_http
            service_smoke_api_key
            service_smoke_api_key_file
            service_load_smoke_foundation
            PROPERTIES
                LABELS "compatibility;legacy-session;service"
        )
        set_tests_properties(
            service_job_smoke
            PROPERTIES
                LABELS "archive;compatibility;job;legacy-session;service"
        )
        set_tests_properties(
            module_runtime_http_smoke
            module_runtime_concurrency_soak
            capture_service_http_smoke
            PROPERTIES
                LABELS "compatibility;low-level-runtime;service"
        )
        set_tests_properties(
            service_smoke_http
            service_smoke_api_key
            service_smoke_api_key_file
            service_load_smoke_foundation
            service_job_smoke
            project_authority_service_smoke
            project_acquisition_http_smoke
            project_tracked_click_localisation_http_smoke
            module_runtime_http_smoke
            module_runtime_concurrency_soak
            capture_service_http_smoke
            web_assets_service_smoke
            PROPERTIES TIMEOUT 90
        )
    endif()
endif()

if(PAMGUARD_NODE)
    add_test(
        NAME production_compatibility_isolation_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/production-compatibility-isolation.contract.test.js
    )
    set_tests_properties(
        production_compatibility_isolation_browser_contract
        PROPERTIES
            LABELS "browser;compatibility-isolation;project-authority"
    )
    add_test(
        NAME project_shell_dialog_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-shell-dialog.contract.test.js
    )
    set_tests_properties(
        project_shell_dialog_browser_contract
        PROPERTIES
            LABELS "browser;project-authority"
    )
    add_test(
        NAME project_displays_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-displays.contract.test.js
    )
    add_test(
        NAME project_audio_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-audio.contract.test.js
    )
    add_test(
        NAME project_click_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-click-settings.contract.test.js
    )
    add_test(
        NAME project_tracked_clicks_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-tracked-clicks.contract.test.js
    )
    add_test(
        NAME project_signal_routing_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-signal-routing-settings.contract.test.js
    )
    add_test(
        NAME project_filter_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-filter-settings.contract.test.js
    )
    add_test(
        NAME project_level_meter_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-level-meter-settings.contract.test.js
    )
    add_test(
        NAME project_level_meter_display_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-level-meter-display.contract.test.js
    )
    add_test(
        NAME project_noise_ltsa_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-noise-ltsa-settings.contract.test.js
    )
    add_test(
        NAME project_whistle_moan_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-whistle-moan-settings.contract.test.js
    )
    add_test(
        NAME project_ishmael_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-ishmael-settings.contract.test.js
    )
    add_test(
        NAME project_mht_click_train_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-mht-click-train-settings.contract.test.js
    )
    add_test(
        NAME project_matched_template_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-matched-template-settings.contract.test.js
    )
    add_test(
        NAME project_sound_recorder_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-sound-recorder-settings.contract.test.js
    )
    add_test(
        NAME project_clip_generator_settings_browser_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/project-clip-generator-settings.contract.test.js
    )
    set_tests_properties(
        project_audio_browser_contract
        PROPERTIES LABELS "audio;browser;project-authority"
    )
    set_tests_properties(
        project_click_settings_browser_contract
        PROPERTIES LABELS "click;browser;project-authority"
    )
    set_tests_properties(
        project_tracked_clicks_browser_contract
        PROPERTIES LABELS "click;browser;project-authority"
    )
    set_tests_properties(
        project_signal_routing_settings_browser_contract
        PROPERTIES LABELS "signal-routing;browser;project-authority"
    )
    set_tests_properties(
        project_filter_settings_browser_contract
        PROPERTIES LABELS "filter;decimator;browser;project-authority"
    )
    set_tests_properties(
        project_level_meter_settings_browser_contract
        project_level_meter_display_browser_contract
        PROPERTIES LABELS "level-meter;browser;project-authority"
    )
    set_tests_properties(
        project_noise_ltsa_settings_browser_contract
        PROPERTIES LABELS "noise;noise-band;ltsa;browser;project-authority"
    )
    set_tests_properties(
        project_whistle_moan_settings_browser_contract
        PROPERTIES LABELS "whistle;moan;browser;project-authority"
    )
    set_tests_properties(
        project_ishmael_settings_browser_contract
        PROPERTIES LABELS "ishmael;browser;project-authority"
    )
    set_tests_properties(
        project_mht_click_train_settings_browser_contract
        PROPERTIES LABELS "click;mht;click-train;browser;project-authority"
    )
    set_tests_properties(
        project_matched_template_settings_browser_contract
        PROPERTIES LABELS "click;matched-template;browser;project-authority"
    )
    set_tests_properties(
        project_sound_recorder_settings_browser_contract
        PROPERTIES LABELS "sound-recorder;browser;project-authority"
    )
    set_tests_properties(
        project_clip_generator_settings_browser_contract
        PROPERTIES LABELS "clip-generator;browser;project-authority"
    )
    add_test(
        NAME project_browser_client_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/platform/project-client.contract.test.js
    )
    add_test(
        NAME project_browser_identifier_contract
        COMMAND ${PAMGUARD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/../web-ui/assets/platform/identifiers.contract.test.js
    )
endif()
