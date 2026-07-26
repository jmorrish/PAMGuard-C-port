#include "pamguard/service/TrackedClickEvents.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pamguard::service {
namespace {

struct LeastSquaresPrerequisites {
    bool enough_clicks = false;
    bool complete_origins_and_headings = false;
    bool complete_equal_ambiguities = false;
    std::size_t ambiguity_count = 0;
};

bool finite_origin(const std::array<double, 3>& origin) {
    return std::all_of(
        origin.begin(),
        origin.end(),
        [](const double value) {
            return std::isfinite(value);
        });
}

LeastSquaresPrerequisites least_squares_prerequisites(
    const std::vector<TrackedClickObservation>& clicks) {
    LeastSquaresPrerequisites result;
    result.enough_clicks = clicks.size() >= 2;
    result.complete_origins_and_headings =
        std::all_of(
            clicks.begin(),
            clicks.end(),
            [](const auto& click) {
                return click.origin_metres &&
                    finite_origin(*click.origin_metres) &&
                    click.heading_radians &&
                    std::isfinite(*click.heading_radians) &&
                    !click.navigation_reference_id.empty();
            });
    if (result.complete_origins_and_headings &&
        !clicks.empty()) {
        const auto& reference =
            clicks.front().navigation_reference_id;
        result.complete_origins_and_headings =
            std::all_of(
                clicks.begin(),
                clicks.end(),
                [&](const auto& click) {
                    return click.navigation_reference_id ==
                        reference;
                });
    }

    std::optional<std::size_t> expected_ambiguity_count;
    result.complete_equal_ambiguities = std::all_of(
        clicks.begin(),
        clicks.end(),
        [&](const auto& click) {
            if (click.earth_bearing_ambiguities_radians.empty() ||
                !std::all_of(
                    click.earth_bearing_ambiguities_radians.begin(),
                    click.earth_bearing_ambiguities_radians.end(),
                    [](const double value) {
                        return std::isfinite(value);
                    })) {
                return false;
            }
            if (!expected_ambiguity_count) {
                expected_ambiguity_count =
                    click.earth_bearing_ambiguities_radians.size();
                return true;
            }
            return click.earth_bearing_ambiguities_radians.size() ==
                *expected_ambiguity_count;
        });
    result.ambiguity_count =
        result.complete_equal_ambiguities &&
            expected_ambiguity_count
        ? *expected_ambiguity_count
        : 0;
    return result;
}

std::vector<TrackedClickAlgorithmAvailability> algorithm_availability(
    const TrackedClickLocaliserSettings& settings,
    const std::string_view project_mode,
    const std::vector<TrackedClickObservation>& clicks) {
    const auto prerequisites =
        least_squares_prerequisites(clicks);
    std::string least_squares_reason;
    if (!prerequisites.enough_clicks) {
        least_squares_reason =
            "Least Squares requires at least two retained clicks";
    }
    else if (!prerequisites.complete_equal_ambiguities) {
        least_squares_reason =
            "Every click must provide the same nonzero number of finite "
            "earth-frame bearing ambiguities";
    }
    else if (!prerequisites.complete_origins_and_headings) {
        least_squares_reason =
            "Every click must provide a finite local Cartesian origin and "
            "finite time-specific array heading in the same navigation "
            "reference frame";
    }

    std::vector<TrackedClickAlgorithmAvailability> result{
        {
            0,
            "least-squares",
            "Least Squares",
            false,
            least_squares_reason.empty(),
            std::move(least_squares_reason),
        },
        {
            1,
            "simplex-2d",
            "2D Simplex Optimization",
            false,
            false,
            "The PAMGuard 2D Simplex Optimization algorithm is not "
            "implemented in the C++ tracked-event service",
        },
        {
            2,
            "simplex-3d",
            "3D Simplex Optimization",
            false,
            false,
            "The PAMGuard 3D Simplex Optimization algorithm is not "
            "implemented in the C++ tracked-event service",
        },
        {
            3,
            "mcmc",
            "MCMC",
            false,
            false,
            project_mode == "viewer"
                ? "Per-click hydrophone origins and time-delay geometry are "
                  "not present in the retained Click Detector stream"
                : "PAMGuard only registers MCMC in Viewer mode",
        },
    };
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].selected =
            index < settings.is_selected.size() &&
            settings.is_selected[index];
    }
    return result;
}

void validate_observation(const TrackedClickObservation& click) {
    if (click.uid == 0) {
        throw std::invalid_argument("Tracked click UID must be positive");
    }
    if (click.channel_bitmap == 0) {
        throw std::invalid_argument(
            "Tracked click channel bitmap must be nonzero");
    }
    if (click.bearing_radians &&
        !std::isfinite(*click.bearing_radians)) {
        throw std::invalid_argument(
            "Tracked click bearing must be finite when present");
    }
    if (click.origin_metres &&
        !finite_origin(*click.origin_metres)) {
        throw std::invalid_argument(
            "Tracked click origin must contain finite east, north, and "
            "height values when present");
    }
    if (click.heading_radians &&
        !std::isfinite(*click.heading_radians)) {
        throw std::invalid_argument(
            "Tracked click heading must be finite when present");
    }
    if ((!click.navigation_reference_id.empty()) !=
        click.origin_metres.has_value()) {
        throw std::invalid_argument(
            "Tracked click origin and navigation reference must be "
            "present together");
    }
    if (!std::all_of(
            click.earth_bearing_ambiguities_radians.begin(),
            click.earth_bearing_ambiguities_radians.end(),
            [](const double value) {
                return std::isfinite(value);
            })) {
        throw std::invalid_argument(
            "Tracked click earth-frame bearing ambiguities must be finite");
    }
}

TrackedClickLocalisationAssessment assess_event(
    const TrackedClickEvent& event,
    const TrackedClickLocaliserSettings& settings,
    const std::string_view project_mode) {
    TrackedClickLocalisationAssessment result;
    result.algorithms =
        algorithm_availability(settings, project_mode, event.clicks);
    if (event.clicks.size() < 2) {
        result.status =
            TrackedClickLocalisationStatus::NotEnoughClicks;
        result.code = "not_enough_clicks";
        result.message =
            "PAMGuard does not localise a tracked event until it has "
            "at least two clicks";
        return result;
    }

    const bool any_selected = std::any_of(
        result.algorithms.begin(),
        result.algorithms.end(),
        [](const auto& algorithm) {
            return algorithm.selected;
        });
    if (!any_selected) {
        result.status =
            TrackedClickLocalisationStatus::NoAlgorithmSelected;
        result.code = "no_algorithm_selected";
        result.message =
            "ClickLocParams has no selected group-localisation algorithm";
        return result;
    }

    const auto& least_squares = result.algorithms[0];
    if (least_squares.selected && least_squares.available) {
        result.status = TrackedClickLocalisationStatus::Available;
        result.code = "least_squares_available";
        result.message =
            "Least Squares has complete ordered ambiguity bearings and "
            "moving-array origin/heading snapshots for every click";
        return result;
    }

    if (least_squares.selected) {
        const auto prerequisites =
            least_squares_prerequisites(event.clicks);
        if (!prerequisites.complete_equal_ambiguities) {
            result.status =
                TrackedClickLocalisationStatus::MissingBearing;
            result.code = "missing_click_bearing";
            result.message =
                "Least Squares requires every retained click to provide "
                "the same nonzero number of finite earth-frame bearing "
                "ambiguities";
            return result;
        }
        result.status =
            TrackedClickLocalisationStatus::
                MovingArrayOriginUnavailable;
        result.code = "moving_array_origin_unavailable";
        result.message =
            "Least Squares requires a finite local Cartesian origin and "
            "time-specific array heading in one navigation reference frame "
            "for every retained click";
        return result;
    }

    if (result.algorithms[1].selected ||
        result.algorithms[2].selected) {
        result.status =
            TrackedClickLocalisationStatus::
                SelectedAlgorithmUnavailable;
        result.code = "selected_algorithm_unimplemented";
        result.message =
            "The selected PAMGuard Simplex group-localisation algorithm "
            "is not implemented in the C++ tracked-event service";
        return result;
    }

    if (result.algorithms[3].selected &&
        project_mode != "viewer") {
        result.status =
            TrackedClickLocalisationStatus::RunModeUnavailable;
        result.code = "algorithm_run_mode_unavailable";
        result.message =
            "PAMGuard only registers MCMC group localisation in Viewer mode";
        return result;
    }

    result.status =
        TrackedClickLocalisationStatus::TimeDelayOriginUnavailable;
    result.code = "time_delay_origin_unavailable";
    result.message =
        "MCMC requires per-click hydrophone origins and time-delay "
        "geometry, which the current Click Detector stream does not publish";
    return result;
}

} // namespace

void TrackedClickEventStore::reconcile(TrackedClickScope scope) {
    if (scope.project_id.empty() ||
        scope.click_detector_unit_id.empty() ||
        scope.working_revision == 0) {
        throw std::invalid_argument(
            "Tracked-click scope requires project ID, Click Detector ID, "
            "and positive working revision");
    }
    std::lock_guard lock(mutex_);
    if (scope_ == scope) {
        return;
    }
    scope_ = std::move(scope);
    events_.clear();
    event_by_click_.clear();
}

TrackedClickScope TrackedClickEventStore::scope() const {
    std::lock_guard lock(mutex_);
    return scope_;
}

std::vector<TrackedClickEvent> TrackedClickEventStore::events() const {
    std::lock_guard lock(mutex_);
    return events_;
}

std::optional<std::uint64_t>
TrackedClickEventStore::event_for_click(
    const std::uint64_t click_uid) const {
    std::lock_guard lock(mutex_);
    const auto found = event_by_click_.find(click_uid);
    if (found == event_by_click_.end()) {
        return std::nullopt;
    }
    return found->second;
}

TrackedClickEvent TrackedClickEventStore::assign(
    std::vector<TrackedClickObservation> clicks,
    std::optional<std::uint64_t> event_id) {
    if (clicks.empty()) {
        throw std::invalid_argument(
            "At least one retained click is required");
    }
    for (const auto& click : clicks) {
        validate_observation(click);
    }
    std::sort(
        clicks.begin(),
        clicks.end(),
        [](const auto& left, const auto& right) {
            if (left.start_sample != right.start_sample) {
                return left.start_sample < right.start_sample;
            }
            return left.uid < right.uid;
        });
    const auto unique_end = std::unique(
        clicks.begin(),
        clicks.end(),
        [](const auto& left, const auto& right) {
            return left.uid == right.uid;
        });
    clicks.erase(unique_end, clicks.end());

    std::lock_guard lock(mutex_);
    if (scope_.project_id.empty()) {
        throw std::logic_error(
            "Tracked-click store has not been reconciled");
    }
    const auto target_id =
        event_id.value_or(next_event_id_locked());
    if (target_id == 0) {
        throw std::invalid_argument(
            "Tracked event ID must be positive");
    }
    auto target = find_event_locked(target_id);
    if (target == events_.end()) {
        if (event_id) {
            throw std::out_of_range(
                "Target tracked event does not exist");
        }
        events_.push_back({target_id});
        std::sort(
            events_.begin(),
            events_.end(),
            [](const auto& left, const auto& right) {
                return left.event_id < right.event_id;
            });
        target = find_event_locked(target_id);
    }
    for (const auto& click : clicks) {
        const auto former = event_by_click_.find(click.uid);
        if (former != event_by_click_.end() &&
            former->second == target_id) {
            const auto existing = std::find_if(
                target->clicks.begin(),
                target->clicks.end(),
                [&](const auto& candidate) {
                    return candidate.uid == click.uid;
                });
            if (existing != target->clicks.end()) {
                *existing = click;
            }
            continue;
        }
        if (former != event_by_click_.end()) {
            remove_click_locked(click.uid);
            target = find_event_locked(target_id);
        }
        target->clicks.push_back(click);
        event_by_click_[click.uid] = target_id;
    }
    std::sort(
        target->clicks.begin(),
        target->clicks.end(),
        [](const auto& left, const auto& right) {
            if (left.start_sample != right.start_sample) {
                return left.start_sample < right.start_sample;
            }
            return left.uid < right.uid;
        });
    return *target;
}

bool TrackedClickEventStore::remove_click(
    const std::uint64_t click_uid) {
    std::lock_guard lock(mutex_);
    if (!event_by_click_.contains(click_uid)) {
        return false;
    }
    remove_click_locked(click_uid);
    return true;
}

TrackedClickEvent TrackedClickEventStore::reassign_event(
    const std::uint64_t source_event_id,
    const std::uint64_t target_event_id) {
    if (source_event_id == target_event_id) {
        throw std::invalid_argument(
            "Source and target tracked events must differ");
    }
    std::lock_guard lock(mutex_);
    auto source = find_event_locked(source_event_id);
    if (source == events_.end()) {
        throw std::out_of_range("Source tracked event does not exist");
    }
    auto target = find_event_locked(target_event_id);
    if (target == events_.end()) {
        throw std::out_of_range("Target tracked event does not exist");
    }
    const auto moved = source->clicks;
    for (const auto& click : moved) {
        target->clicks.push_back(click);
        event_by_click_[click.uid] = target_event_id;
    }
    std::sort(
        target->clicks.begin(),
        target->clicks.end(),
        [](const auto& left, const auto& right) {
            if (left.start_sample != right.start_sample) {
                return left.start_sample < right.start_sample;
            }
            return left.uid < right.uid;
        });
    target->clicks.erase(
        std::unique(
            target->clicks.begin(),
            target->clicks.end(),
            [](const auto& left, const auto& right) {
                return left.uid == right.uid;
            }),
        target->clicks.end());
    const auto result = *target;
    events_.erase(find_event_locked(source_event_id));
    return result;
}

bool TrackedClickEventStore::delete_event(
    const std::uint64_t event_id) {
    std::lock_guard lock(mutex_);
    const auto event = find_event_locked(event_id);
    if (event == events_.end()) {
        return false;
    }
    for (const auto& click : event->clicks) {
        event_by_click_.erase(click.uid);
    }
    events_.erase(event);
    return true;
}

TrackedClickLocalisationAssessment
TrackedClickEventStore::assess_localisation(
    const std::uint64_t event_id,
    const TrackedClickLocaliserSettings& settings,
    const std::string_view project_mode) const {
    std::lock_guard lock(mutex_);
    const auto event = find_event_locked(event_id);
    if (event == events_.end()) {
        throw std::out_of_range("Tracked event does not exist");
    }
    return assess_event(*event, settings, project_mode);
}

TrackedClickLocalisationRun
TrackedClickEventStore::run_localisation(
    const std::uint64_t event_id,
    const TrackedClickLocaliserSettings& settings,
    const std::string_view project_mode,
    const std::vector<TrackedClickNavigationSample>&
        navigation_track) const {
    TrackedClickEvent event;
    {
        std::lock_guard lock(mutex_);
        const auto found = find_event_locked(event_id);
        if (found == events_.end()) {
            throw std::out_of_range("Tracked event does not exist");
        }
        event = *found;
    }

    TrackedClickLocalisationRun run;
    run.assessment =
        assess_event(event, settings, project_mode);
    if (!run.assessment.available()) {
        run.status =
            TrackedClickLocalisationRunStatus::
                AssessmentUnavailable;
        run.code = run.assessment.code;
        run.message = run.assessment.message;
        return run;
    }
    if (navigation_track.empty()) {
        run.status =
            TrackedClickLocalisationRunStatus::
                NavigationTrackUnavailable;
        run.code = "navigation_track_unavailable";
        run.message =
            "Least Squares inputs are available, but PAMGuard post-fit "
            "range filtering requires at least one local navigation-track "
            "sample";
        return run;
    }
    for (const auto& sample : navigation_track) {
        if (!finite_origin(sample.origin_metres)) {
            throw std::invalid_argument(
                "Navigation-track origins must contain finite east, north, "
                "and height values");
        }
    }
    if (!std::isfinite(settings.max_range_m) ||
        !std::isfinite(settings.min_height_m) ||
        !std::isfinite(settings.max_height_m)) {
        throw std::invalid_argument(
            "Tracked-click localisation limits must be finite");
    }

    const auto prerequisites =
        least_squares_prerequisites(event.clicks);
    std::vector<std::vector<
        localisation::TrackedTargetMotionObservation>>
        ambiguity_sides(prerequisites.ambiguity_count);
    for (auto& side : ambiguity_sides) {
        side.reserve(event.clicks.size());
    }
    for (const auto& click : event.clicks) {
        for (std::size_t ambiguity = 0;
             ambiguity < prerequisites.ambiguity_count;
             ++ambiguity) {
            ambiguity_sides[ambiguity].push_back({
                *click.origin_metres,
                *click.heading_radians,
                click.earth_bearing_ambiguities_radians[ambiguity],
            });
        }
    }

    std::int32_t maximum_points =
        localisation::TrackedTargetMotionLeastSquares::
            java_unlimited_points;
    if (settings.limit_points) {
        maximum_points =
            settings.max_points >
                    static_cast<std::size_t>(
                        std::numeric_limits<std::int32_t>::max())
            ? -1
            : static_cast<std::int32_t>(settings.max_points);
    }
    auto fits =
        localisation::TrackedTargetMotionLeastSquares::
            localise_ambiguities(
                ambiguity_sides,
                maximum_points);

    const localisation::TrackedTargetMotionLimits limits{
        settings.max_range_m,
        settings.min_height_m,
        settings.max_height_m,
    };
    run.ambiguities.reserve(fits.size());
    for (std::size_t ambiguity = 0;
         ambiguity < fits.size();
         ++ambiguity) {
        TrackedClickAmbiguityLocalisationResult result;
        result.ambiguity_index = ambiguity;
        result.fit = std::move(fits[ambiguity]);

        if (result.fit.succeeded() &&
            std::isfinite(result.fit.position_metres[0]) &&
            std::isfinite(result.fit.position_metres[1])) {
            std::optional<std::size_t> beam_index;
            double beam_distance =
                std::numeric_limits<double>::infinity();
            for (std::size_t index = 0;
                 index < navigation_track.size();
                 ++index) {
                const double east =
                    result.fit.position_metres[0] -
                    navigation_track[index].origin_metres[0];
                const double north =
                    result.fit.position_metres[1] -
                    navigation_track[index].origin_metres[1];
                const double distance =
                    std::hypot(east, north);
                if (distance < beam_distance) {
                    beam_distance = distance;
                    beam_index = index;
                }
            }
            if (beam_index) {
                result.beam_sample_time_ms =
                    navigation_track[*beam_index].time_ms;
                result.beam_distance_metres =
                    beam_distance;
                result.filter_input =
                    localisation::TrackedTargetMotionFilterInput{
                        beam_distance,
                        result.fit.position_metres[2],
                    };
                result.filter_assessment =
                    localisation::TrackedTargetMotionLeastSquares::
                        assess_filters(
                            result.fit.succeeded(),
                            *result.filter_input,
                            limits);
            }
        }
        run.ambiguities.push_back(std::move(result));
    }
    run.status =
        TrackedClickLocalisationRunStatus::Executed;
    run.code = "least_squares_executed";
    run.message =
        "Least Squares executed for " +
        std::to_string(run.ambiguities.size()) +
        " ordered bearing ambiguity side(s)";
    return run;
}

std::uint64_t
TrackedClickEventStore::next_event_id_locked() const {
    std::uint64_t maximum = 0;
    for (const auto& event : events_) {
        maximum = std::max(maximum, event.event_id);
    }
    if (maximum == UINT64_MAX) {
        throw std::overflow_error(
            "Tracked event ID space is exhausted");
    }
    return maximum + 1;
}

std::vector<TrackedClickEvent>::iterator
TrackedClickEventStore::find_event_locked(
    const std::uint64_t event_id) {
    return std::find_if(
        events_.begin(),
        events_.end(),
        [event_id](const auto& event) {
            return event.event_id == event_id;
        });
}

std::vector<TrackedClickEvent>::const_iterator
TrackedClickEventStore::find_event_locked(
    const std::uint64_t event_id) const {
    return std::find_if(
        events_.begin(),
        events_.end(),
        [event_id](const auto& event) {
            return event.event_id == event_id;
        });
}

void TrackedClickEventStore::remove_click_locked(
    const std::uint64_t click_uid) {
    const auto membership = event_by_click_.find(click_uid);
    if (membership == event_by_click_.end()) {
        return;
    }
    const auto event_id = membership->second;
    event_by_click_.erase(membership);
    const auto event = find_event_locked(event_id);
    if (event == events_.end()) {
        return;
    }
    std::erase_if(
        event->clicks,
        [click_uid](const auto& click) {
            return click.uid == click_uid;
        });
    if (event->clicks.empty()) {
        events_.erase(event);
    }
}

bool TrackedClickLocalisationRun::accepted() const noexcept {
    return std::any_of(
        ambiguities.begin(),
        ambiguities.end(),
        [](const auto& ambiguity) {
            return ambiguity.accepted();
        });
}

const char* tracked_click_localisation_status_name(
    const TrackedClickLocalisationStatus status) noexcept {
    switch (status) {
    case TrackedClickLocalisationStatus::Available:
        return "available";
    case TrackedClickLocalisationStatus::NotEnoughClicks:
        return "notEnoughClicks";
    case TrackedClickLocalisationStatus::NoAlgorithmSelected:
        return "noAlgorithmSelected";
    case TrackedClickLocalisationStatus::MissingBearing:
        return "missingBearing";
    case TrackedClickLocalisationStatus::MovingArrayOriginUnavailable:
        return "movingArrayOriginUnavailable";
    case TrackedClickLocalisationStatus::TimeDelayOriginUnavailable:
        return "timeDelayOriginUnavailable";
    case TrackedClickLocalisationStatus::RunModeUnavailable:
        return "runModeUnavailable";
    case TrackedClickLocalisationStatus::
            SelectedAlgorithmUnavailable:
        return "selectedAlgorithmUnavailable";
    }
    return "unavailable";
}

const char* tracked_click_localisation_run_status_name(
    const TrackedClickLocalisationRunStatus status) noexcept {
    switch (status) {
    case TrackedClickLocalisationRunStatus::
            AssessmentUnavailable:
        return "assessmentUnavailable";
    case TrackedClickLocalisationRunStatus::
            NavigationTrackUnavailable:
        return "navigationTrackUnavailable";
    case TrackedClickLocalisationRunStatus::Executed:
        return "executed";
    }
    return "unavailable";
}

} // namespace pamguard::service
