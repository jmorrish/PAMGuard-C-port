#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pamguard/localisation/TrackedTargetMotion.h"

namespace pamguard::service {

/**
 * Stable identity for one click in one prepared project runtime.
 *
 * PAMGuard's ClickDetection UID is the durable child identity used by its
 * OfflineEventDataUnit relationship. The C++ runtime restarts click UIDs when
 * a generated graph is replaced, so project ID, controlled-unit ID, and
 * working revision remain part of the store scope rather than the click key.
 */
struct TrackedClickObservation {
    std::uint64_t uid = 0;
    std::int64_t start_sample = 0;
    std::int64_t time_ms = 0;
    std::uint32_t channel_bitmap = 0;
    /** Legacy single bearing retained for the operator event table/UI. */
    std::optional<double> bearing_radians;
    /** Array-centroid origin in the local east/north/height metre frame. */
    std::optional<std::array<double, 3>> origin_metres;
    /** Time-specific array heading, clockwise from north. */
    std::optional<double> heading_radians;
    /**
     * Earth-frame bearings, clockwise from north, in PAMGuard ambiguity-side
     * order. Every click in one event must expose the same nonzero count before
     * Least Squares is available.
     */
    std::vector<double> earth_bearing_ambiguities_radians;
    /** Stable identity of the local navigation frame/source for this click. */
    std::string navigation_reference_id;

    bool operator==(const TrackedClickObservation&) const = default;
};

struct TrackedClickEvent {
    std::uint64_t event_id = 0;
    std::string comment = "Manual Click Train Detection";
    std::vector<TrackedClickObservation> clicks;

    bool operator==(const TrackedClickEvent&) const = default;
};

struct TrackedClickScope {
    std::string project_id;
    std::string click_detector_unit_id;
    std::uint64_t working_revision = 0;

    bool operator==(const TrackedClickScope&) const = default;
};

enum class TrackedClickLocalisationStatus {
    Available,
    NotEnoughClicks,
    NoAlgorithmSelected,
    MissingBearing,
    MovingArrayOriginUnavailable,
    TimeDelayOriginUnavailable,
    RunModeUnavailable,
    SelectedAlgorithmUnavailable,
};

struct TrackedClickAlgorithmAvailability {
    std::size_t java_index = 0;
    std::string id;
    std::string java_name;
    bool selected = false;
    bool available = false;
    std::string unavailable_reason;
};

struct TrackedClickLocalisationAssessment {
    TrackedClickLocalisationStatus status =
        TrackedClickLocalisationStatus::NotEnoughClicks;
    std::string code;
    std::string message;
    std::vector<TrackedClickAlgorithmAvailability> algorithms;

    [[nodiscard]] bool available() const noexcept {
        return status == TrackedClickLocalisationStatus::Available;
    }
};

/**
 * Java-authoritative ClickLocParams values.
 *
 * is_selected uses GeneralGroupLocaliser.generateLocList ordering:
 * Least Squares, 2D Simplex Optimization, 3D Simplex Optimization, MCMC.
 * MCMC exists only in PAMGuard Viewer mode. Four explicit entries are used in
 * portable JSON to avoid Java's lazy/null private-array serialization.
 */
struct TrackedClickLocaliserSettings {
    std::vector<bool> is_selected{true, false, false, false};
    double max_range_m = 20000.0;
    double max_height_m = 5.0;
    double min_height_m = -5000.0;
    std::uint64_t max_time_milliseconds = 200;
    bool limit_points = false;
    std::size_t max_points = 30;
};

/**
 * One local Cartesian navigation/GPS-track sample. The run adapter searches
 * the supplied samples in their supplied order and retains the first sample
 * at the minimum horizontal distance, matching Java's strict-less-than scan.
 */
struct TrackedClickNavigationSample {
    std::int64_t time_ms = 0;
    std::array<double, 3> origin_metres{};

    bool operator==(const TrackedClickNavigationSample&) const = default;
};

enum class TrackedClickLocalisationRunStatus {
    AssessmentUnavailable,
    NavigationTrackUnavailable,
    Executed,
};

/**
 * One Least Squares ambiguity-side result. `fit.status` is the unmodified
 * status returned by TrackedTargetMotionLeastSquares; run completion never
 * promotes a failed numerical fit to success.
 */
struct TrackedClickAmbiguityLocalisationResult {
    std::size_t ambiguity_index = 0;
    localisation::TrackedTargetMotionResult fit;
    std::optional<std::int64_t> beam_sample_time_ms;
    std::optional<double> beam_distance_metres;
    std::optional<localisation::TrackedTargetMotionFilterInput> filter_input;
    std::optional<localisation::TrackedTargetMotionFilterAssessment>
        filter_assessment;

    [[nodiscard]] bool accepted() const noexcept {
        return filter_assessment &&
            filter_assessment->accepted;
    }
};

/**
 * Complete service-domain run of the implemented Least Squares algorithm.
 *
 * An Available assessment says that Least Squares has all per-click inputs.
 * A navigation track is additionally required to execute the complete Java
 * post-fit filtering stage. `accepted()` follows GeneralGroupLocaliser: one
 * accepted ambiguity accepts the algorithm result.
 */
struct TrackedClickLocalisationRun {
    TrackedClickLocalisationRunStatus status =
        TrackedClickLocalisationRunStatus::AssessmentUnavailable;
    std::string code;
    std::string message;
    TrackedClickLocalisationAssessment assessment;
    std::vector<TrackedClickAmbiguityLocalisationResult> ambiguities;

    [[nodiscard]] bool executed() const noexcept {
        return status == TrackedClickLocalisationRunStatus::Executed;
    }

    [[nodiscard]] bool accepted() const noexcept;
};

/**
 * In-memory equivalent of TrackedClickLocaliser's event membership layer.
 *
 * The store deliberately owns runtime scientific data, not project
 * configuration. reconcile() clears it whenever the project, Click Detector,
 * or working revision changes because the corresponding retained-click
 * DataBlock and its UIDs have also been replaced.
 */
class TrackedClickEventStore {
public:
    void reconcile(TrackedClickScope scope);

    [[nodiscard]] TrackedClickScope scope() const;
    [[nodiscard]] std::vector<TrackedClickEvent> events() const;
    [[nodiscard]] std::optional<std::uint64_t> event_for_click(
        std::uint64_t click_uid) const;

    /**
     * Assign clicks to an existing event or create the next Java-style event
     * ID when event_id is null. A click is removed from its former event first;
     * empty former events are deleted.
     */
    [[nodiscard]] TrackedClickEvent assign(
        std::vector<TrackedClickObservation> clicks,
        std::optional<std::uint64_t> event_id);

    /** Remove a click and delete its event if that event becomes empty. */
    [[nodiscard]] bool remove_click(std::uint64_t click_uid);

    /** Move every click in one event to another, then delete the source. */
    [[nodiscard]] TrackedClickEvent reassign_event(
        std::uint64_t source_event_id,
        std::uint64_t target_event_id);

    /** Delete an event and all of its membership relationships. */
    [[nodiscard]] bool delete_event(std::uint64_t event_id);

    [[nodiscard]] TrackedClickLocalisationAssessment
    assess_localisation(
        std::uint64_t event_id,
        const TrackedClickLocaliserSettings& settings,
        std::string_view project_mode) const;

    /**
     * Execute the implemented Least Squares path and its Java post-fit filters.
     * Other selected algorithms remain visible in `assessment.algorithms` but
     * are never substituted for, or allowed to block, available Least Squares.
     */
    [[nodiscard]] TrackedClickLocalisationRun run_localisation(
        std::uint64_t event_id,
        const TrackedClickLocaliserSettings& settings,
        std::string_view project_mode,
        const std::vector<TrackedClickNavigationSample>&
            navigation_track) const;

private:
    [[nodiscard]] std::uint64_t next_event_id_locked() const;
    [[nodiscard]] std::vector<TrackedClickEvent>::iterator
    find_event_locked(std::uint64_t event_id);
    [[nodiscard]] std::vector<TrackedClickEvent>::const_iterator
    find_event_locked(std::uint64_t event_id) const;
    void remove_click_locked(std::uint64_t click_uid);

    mutable std::mutex mutex_;
    TrackedClickScope scope_;
    std::vector<TrackedClickEvent> events_;
    std::unordered_map<std::uint64_t, std::uint64_t> event_by_click_;
};

[[nodiscard]] const char* tracked_click_localisation_status_name(
    TrackedClickLocalisationStatus status) noexcept;

[[nodiscard]] const char* tracked_click_localisation_run_status_name(
    TrackedClickLocalisationRunStatus status) noexcept;

} // namespace pamguard::service
