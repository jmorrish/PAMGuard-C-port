#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "pamguard/project/ProjectDocument.h"
#include "pamguard/project/ProjectJson.h"

namespace {

using pamguard::project::ControlledUnitInstance;
using pamguard::project::DataModelNodePosition;
using pamguard::project::DisplayGridItem;
using pamguard::project::DisplayInstance;
using pamguard::project::DisplayOwner;
using pamguard::project::DisplayTab;
using pamguard::project::ExpansionRecipeReference;
using pamguard::project::GlobalSettingsComponent;
using pamguard::project::InputBinding;
using pamguard::project::ProjectDocument;
using pamguard::project::ProjectMode;
using pamguard::project::SourceReference;

constexpr const char* kProjectId =
    "11111111-1111-4111-8111-111111111111";
constexpr const char* kAcquisitionId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kFftId =
    "ffffffff-ffff-4fff-bfff-ffffffffffff";
constexpr const char* kUserDisplayId =
    "22222222-2222-4222-8222-222222222222";
constexpr const char* kSpectrogramId =
    "33333333-3333-4333-8333-333333333333";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
void require_project_error(
    Operation&& operation,
    const std::string& message) {
    try {
        operation();
    }
    catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

ProjectDocument example_project() {
    ProjectDocument document;
    document.project_id = kProjectId;
    document.metadata = {
        "Click monitoring",
        "Canonical project document fixture",
    };
    document.mode = ProjectMode::Normal;
    document.descriptor_set = {"pamguard-2.02.18e", 1};

    ControlledUnitInstance acquisition;
    acquisition.id = kAcquisitionId;
    acquisition.type_id = "pamguard.acquisition";
    acquisition.descriptor_version = 1;
    acquisition.recipe = {
        "pamguard.acquisition.runtime",
        1,
    };
    acquisition.name = "Sound Acquisition";
    acquisition.settings_version = 1;
    acquisition.settings_json =
        R"({"channelCount":2,"sampleRateHz":48000})";

    ControlledUnitInstance fft;
    fft.id = kFftId;
    fft.type_id = "pamguard.fft";
    fft.descriptor_version = 1;
    fft.recipe = {"pamguard.fft.runtime", 1};
    fft.name = "FFT (Spectrogram) Engine";
    fft.settings_version = 1;
    fft.settings_json =
        R"({"fftHop":512,"fftLength":1024,"windowType":"Hann"})";
    fft.bindings.push_back(InputBinding{
        "rawAudio",
        {SourceReference{kAcquisitionId, "rawAudio"}},
    });

    ControlledUnitInstance user_display;
    user_display.id = kUserDisplayId;
    user_display.type_id = "pamguard.user-display";
    user_display.descriptor_version = 1;
    user_display.recipe = {
        "pamguard.user-display.runtime",
        1,
    };
    user_display.name = "User Display";
    user_display.settings_version = 1;
    user_display.settings_json = "{}";

    document.controlled_units = {
        acquisition,
        fft,
        user_display,
    };
    document.global_settings.components.push_back(
        GlobalSettingsComponent{
            "pamguard.array-manager",
            1,
            R"({"medium":"water"})",
        });

    DisplayTab tab;
    tab.id =
        "tab:22222222-2222-4222-8222-222222222222:main";
    tab.name = "User Display";
    tab.owner = DisplayOwner{kUserDisplayId, "main"};
    DisplayInstance spectrogram;
    spectrogram.id = kSpectrogramId;
    spectrogram.provider_type_id =
        "pamguard.spectrogram-display";
    spectrogram.provider_version = 1;
    spectrogram.owner =
        DisplayOwner{kUserDisplayId, "provider"};
    spectrogram.source =
        SourceReference{kFftId, "fft"};
    spectrogram.settings_version = 2;
    spectrogram.settings_json =
        R"({"amplitudeLimits":[50,120],"channelList":[0],"colourMap":"GREY","displayLength":20,"frequencyLimits":[0,24000],"nPanels":1,"pixelsPerSlics":1,"showScale":true,"timeScaleFixed":false,"wrapDisplay":true})";
    tab.displays.push_back(spectrogram);
    tab.layout.columns = 12;
    tab.layout.selected_display_id = kSpectrogramId;
    tab.layout.items.push_back(
        DisplayGridItem{
            kSpectrogramId,
            0,
            0,
            12,
            6,
        });
    document.display_tabs.push_back(tab);

    document.data_model_layout.nodes = {
        DataModelNodePosition{kAcquisitionId, 10.0, 20.0},
        DataModelNodePosition{kFftId, 240.0, 20.0},
        DataModelNodePosition{kUserDisplayId, 470.0, 20.0},
    };
    document.data_model_layout.viewport = {
        45.0,
        35.0,
        0.85,
    };
    return document;
}

void check_sha256() {
    using pamguard::project::sha256_hex;
    require(
        sha256_hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
        "SHA-256 empty-input fixture changed");
    require(
        sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc fixture changed");
    require(
        sha256_hex(
            "abcdbcdecdefdefgefghfghighijhijk"
            "ijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039"
            "a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256 multi-block fixture changed");
    require(
        sha256_hex(std::string(1'000'000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67"
            "f1809a48a497200e046d39ccc7112cd0",
        "SHA-256 million-a fixture changed");
}

void check_uuid_and_java_names() {
    using pamguard::project::is_role_id;
    using pamguard::project::is_uuid_v4;
    using pamguard::project::is_valid_java_item_name;
    using pamguard::project::java_utf16_code_unit_length;
    using pamguard::project::trim_java_string;

    require(is_uuid_v4(kProjectId), "Valid UUIDv4 was rejected");
    require(
        is_uuid_v4(
            "00000000-0000-4000-8000-000000000000"),
        "UUIDv4 syntax rejected a valid zero-payload UUID");
    require(
        !is_uuid_v4(
            "11111111-1111-4111-8111-11111111111A") &&
            !is_uuid_v4(
                "11111111-1111-5111-8111-111111111111") &&
            !is_uuid_v4(
                "11111111-1111-4111-7111-111111111111"),
        "UUIDv4 syntax accepted case, version, or variant errors");

    require(
        is_role_id("rawAudio") &&
            is_role_id("noiseReducedFft") &&
            !is_role_id("RawAudio") &&
            !is_role_id("raw-audio") &&
            !is_role_id("raw_audio"),
        "Stable public-role lower-camel syntax changed");

    require(
        is_valid_java_item_name(std::string(50, 'a')) &&
            !is_valid_java_item_name(std::string(51, 'a')),
        "Java 50-code-unit name limit changed");
    const std::string emoji = "\xF0\x9F\x98\x80";
    std::string twenty_five_emoji;
    for (int index = 0; index < 25; ++index) {
        twenty_five_emoji += emoji;
    }
    require(
        java_utf16_code_unit_length(twenty_five_emoji) == 50 &&
            is_valid_java_item_name(twenty_five_emoji),
        "Astral UTF-8 did not count as two Java UTF-16 units");
    require(
        !is_valid_java_item_name(
            twenty_five_emoji + emoji) &&
            !is_valid_java_item_name(
                std::string(49, 'a') + emoji),
        "Java name helper accepted more than 50 UTF-16 units");
    require(
        trim_java_string("\t Name \n") == "Name",
        "Java U+0020 trim behavior changed");
    const std::string non_breaking_space = "\xC2\xA0";
    require(
        trim_java_string(
            non_breaking_space + "Name" +
            non_breaking_space) ==
            non_breaking_space + "Name" +
                non_breaking_space,
        "Java trim unexpectedly removed non-breaking spaces");
    require_project_error(
        [&] {
            (void) java_utf16_code_unit_length(
                std::string("\xC0\xAF", 2));
        },
        "Java name helper accepted malformed UTF-8");
}

void check_strict_parser() {
    using pamguard::project::project_document_from_json;
    using pamguard::project::project_document_to_canonical_json;
    using pamguard::project::validate_strict_json;

    require_project_error(
        [] {
            validate_strict_json(R"({"a":1,"a":2})");
        },
        "Strict parser accepted a duplicate object key");
    require_project_error(
        [] {
            validate_strict_json(
                R"({"name":1,"na\u006de":2})");
        },
        "Strict parser missed duplicate decoded keys");

    const auto canonical =
        project_document_to_canonical_json(example_project());
    auto unknown = canonical;
    unknown.insert(1, R"("unexpected":true,)");
    require_project_error(
        [&] {
            (void) project_document_from_json(unknown);
        },
        "Project parser accepted an unknown root field");

    auto nested_unknown = canonical;
    const auto needle = R"("descriptorVersion":1,)";
    const auto position = nested_unknown.find(needle);
    require(
        position != std::string::npos,
        "Could not construct nested unknown-field fixture");
    nested_unknown.insert(
        position + std::string(needle).size(),
        R"("unexpected":true,)");
    require_project_error(
        [&] {
            (void) project_document_from_json(nested_unknown);
        },
        "Project parser accepted an unknown controlled-unit field");

    std::string invalid_utf8 = "{\"value\":\"";
    invalid_utf8.append("\xC0\xAF", 2);
    invalid_utf8 += "\"}";
    require_project_error(
        [&] { validate_strict_json(invalid_utf8); },
        "Strict parser accepted malformed UTF-8");
    require_project_error(
        [] { validate_strict_json("1e400"); },
        "Strict parser accepted a non-finite number");
    validate_strict_json("9007199254740991.0");
    require_project_error(
        [] {
            validate_strict_json("9007199254740992.0");
        },
        "Strict parser accepted an unsafe integral floating-point number");
    require_project_error(
        [] { validate_strict_json("{\"a\":1} trailing"); },
        "Strict parser accepted trailing content");
    require_project_error(
        [] { validate_strict_json("{/*comment*/\"a\":1}"); },
        "Strict parser accepted a JSON comment");

    std::string excessive_depth;
    excessive_depth.append(65, '[');
    excessive_depth += "0";
    excessive_depth.append(65, ']');
    require_project_error(
        [&] { validate_strict_json(excessive_depth); },
        "Strict parser accepted excessive JSON nesting");

    auto non_finite = example_project();
    non_finite.data_model_layout.viewport.x =
        std::numeric_limits<double>::infinity();
    require_project_error(
        [&] {
            (void) project_document_to_canonical_json(
                non_finite);
        },
        "Serializer accepted a programmatic non-finite number");

    auto excessive_settings = example_project();
    excessive_settings.global_settings.components.clear();
    const std::string large_settings =
        std::string("{\"blob\":\"") +
        std::string(950'000, 'a') +
        "\"}";
    for (int index = 0; index < 9; ++index) {
        excessive_settings.global_settings.components.push_back({
            "pamguard.large" + std::to_string(index),
            1,
            large_settings,
        });
    }
    require_project_error(
        [&] {
            (void)project_document_to_canonical_json(
                excessive_settings);
        },
        "Serializer accepted an excessive aggregate settings payload");
}

void check_canonical_json_and_round_trip() {
    using pamguard::project::project_content_hash;
    using pamguard::project::project_document_from_json;
    using pamguard::project::project_document_to_canonical_json;
    using pamguard::project::project_document_to_json;

    const auto document = example_project();
    const auto canonical =
        project_document_to_canonical_json(document);
    require(
        canonical ==
            project_document_to_json(document, false),
        "Compact project JSON is not canonical JSON v1");
    require(
        canonical.find('\n') == std::string::npos &&
            canonical.find("\"controlledUnits\"") <
                canonical.find("\"dataModelLayout\"") &&
            canonical.find("\"dataModelLayout\"") <
                canonical.find("\"descriptorSet\""),
        "Canonical JSON is not compact with sorted object keys");

    const auto parsed = project_document_from_json(canonical);
    require(
        parsed == document,
        "Project serialize/parse did not preserve typed values");
    require(
        project_document_from_json(
            project_document_to_json(document, true)) ==
            document,
        "Pretty project JSON did not round-trip");
    require(
        project_document_to_canonical_json(parsed) ==
            canonical,
        "Project canonical bytes were not stable after round-trip");

    auto reordered_keys = document;
    reordered_keys.controlled_units[0].settings_json =
        R"({"sampleRateHz":48000.0,"channelCount":2})";
    reordered_keys.global_settings.components[0].settings_json =
        R"({"medium":"water"})";
    require(
        project_content_hash(reordered_keys) ==
            project_content_hash(document),
        "Object-key or equivalent-number spelling changed content hash");

    auto reordered_bindings = document;
    reordered_bindings.controlled_units[1].bindings.push_back(
        InputBinding{"optionalOverlay", {}});
    auto bindings_permuted = reordered_bindings;
    std::reverse(
        bindings_permuted.controlled_units[1].bindings.begin(),
        bindings_permuted.controlled_units[1].bindings.end());
    require(
        project_content_hash(reordered_bindings) ==
            project_content_hash(bindings_permuted),
        "Non-semantic binding entry order changed content hash");

    auto reordered_globals = document;
    reordered_globals.global_settings.components.push_back(
        GlobalSettingsComponent{
            "pamguard.clock",
            1,
            R"({"domain":"audio"})",
        });
    auto globals_permuted = reordered_globals;
    std::reverse(
        globals_permuted.global_settings.components.begin(),
        globals_permuted.global_settings.components.end());
    require(
        project_content_hash(reordered_globals) ==
            project_content_hash(globals_permuted),
        "Non-semantic global-component order changed content hash");

    auto positions_permuted = document;
    std::reverse(
        positions_permuted.data_model_layout.nodes.begin(),
        positions_permuted.data_model_layout.nodes.end());
    require(
        project_content_hash(positions_permuted) ==
            project_content_hash(document),
        "Non-semantic node-position order changed content hash");

    auto reordered_units = document;
    std::swap(
        reordered_units.controlled_units[0],
        reordered_units.controlled_units[1]);
    require(
        project_content_hash(reordered_units) !=
            project_content_hash(document),
        "Controlled-unit operator order did not affect content hash");

    auto reordered_displays = document;
    auto second = reordered_displays.display_tabs.front().displays.front();
    second.id = "44444444-4444-4444-8444-444444444444";
    reordered_displays.display_tabs.front().displays.push_back(second);
    reordered_displays.display_tabs.front().layout.items.push_back({
        second.id,
        0,
        6,
        12,
        6,
    });
    auto layout_items_permuted = reordered_displays;
    std::reverse(
        layout_items_permuted.display_tabs.front()
            .layout.items.begin(),
        layout_items_permuted.display_tabs.front()
            .layout.items.end());
    require(
        project_content_hash(reordered_displays) ==
            project_content_hash(layout_items_permuted),
        "Non-semantic display-layout item order changed content hash");

    auto displays_swapped = reordered_displays;
    std::swap(
        displays_swapped.display_tabs.front().displays[0],
        displays_swapped.display_tabs.front().displays[1]);
    require(
        project_content_hash(reordered_displays) !=
            project_content_hash(displays_swapped),
        "Display operator order did not affect content hash");

    require(
        project_content_hash(document).starts_with("sha256:") &&
            project_content_hash(document).size() == 71,
        "Project content hash shape changed");
}

} // namespace

int main() {
    try {
        check_sha256();
        check_uuid_and_java_names();
        check_strict_parser();
        check_canonical_json_and_round_trip();
        std::cout
            << "Project document canonical JSON checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Project document check failed: "
            << error.what() << "\n";
        return 1;
    }
}
