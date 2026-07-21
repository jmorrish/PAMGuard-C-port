#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pamguard::detectors {

/**
 * Growable bitset with java.util.BitSet content semantics: reads beyond the
 * stored length are false, and equality/intersection compare set bits only
 * (trailing zeros are ignored).
 */
class MhtBitset {
public:
    MhtBitset() = default;
    explicit MhtBitset(std::size_t size) : bits_(size, 0) {}

    [[nodiscard]] bool get(std::size_t index) const {
        return index < bits_.size() && bits_[index] != 0;
    }

    void set(std::size_t index, bool value) {
        if (index >= bits_.size()) {
            bits_.resize(index + 1, 0);
        }
        bits_[index] = value ? 1 : 0;
    }

    /** java.util.BitSet.get(0, to): the prefix as a new bitset. */
    [[nodiscard]] MhtBitset prefix(std::size_t to) const {
        MhtBitset result(to);
        for (std::size_t i = 0; i < to && i < bits_.size(); ++i) {
            result.bits_[i] = bits_[i];
        }
        return result;
    }

    /** java.util.BitSet.get(from, to): a sub-range as a new bitset. */
    [[nodiscard]] MhtBitset range(std::size_t from, std::size_t to) const {
        MhtBitset result(to > from ? to - from : 0);
        for (std::size_t i = from; i < to && i < bits_.size(); ++i) {
            result.bits_[i - from] = bits_[i];
        }
        return result;
    }

    /** Index of the first set bit, or size when none is set. */
    [[nodiscard]] std::size_t first_set_bit(std::size_t size) const {
        for (std::size_t i = 0; i < size; ++i) {
            if (get(i)) {
                return i;
            }
        }
        return size;
    }

    [[nodiscard]] bool equals(const MhtBitset& other) const {
        const auto common = std::min(bits_.size(), other.bits_.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (bits_[i] != other.bits_[i]) {
                return false;
            }
        }
        for (std::size_t i = common; i < bits_.size(); ++i) {
            if (bits_[i] != 0) {
                return false;
            }
        }
        for (std::size_t i = common; i < other.bits_.size(); ++i) {
            if (other.bits_[i] != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool intersects(const MhtBitset& other) const {
        const auto common = std::min(bits_.size(), other.bits_.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (bits_[i] != 0 && other.bits_[i] != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t cardinality() const {
        std::size_t count = 0;
        for (const auto bit : bits_) {
            count += bit;
        }
        return count;
    }

    [[nodiscard]] std::string to_string(std::size_t size) const {
        std::string s;
        s.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            s.push_back(get(i) ? '1' : '0');
        }
        return s;
    }

private:
    std::vector<std::uint8_t> bits_;
};

/** MHTChi2 equivalent: track quality scoring plugged into the kernel. */
template <typename T>
class MhtChi2 {
public:
    virtual ~MhtChi2() = default;
    [[nodiscard]] virtual double get_chi2() const = 0;
    [[nodiscard]] virtual int get_n_coasts() const = 0;
    virtual void update(const T& detection, const MhtBitset& track_bits, std::size_t kcount) = 0;
    [[nodiscard]] virtual std::unique_ptr<MhtChi2<T>> clone_chi2() const = 0;
    /** MHTChi2.clearKernelGarbage: StandardMHTChi2 is a no-op. */
    virtual void clear_kernel_garbage(std::size_t new_ref_index) { (void)new_ref_index; }
};

/** MHTChi2Provider equivalent. */
template <typename T>
class MhtChi2Provider {
public:
    virtual ~MhtChi2Provider() = default;
    virtual void add_detection(const T& detection, std::size_t kcount) = 0;
    [[nodiscard]] virtual std::unique_ptr<MhtChi2<T>> new_chi2() = 0;
    virtual void clear() = 0;
    /** MHTChi2Provider.clearKernelGarbage: trims shared series state. */
    virtual void clear_kernel_garbage(std::size_t new_ref_index) { (void)new_ref_index; }
};

struct MhtKernelParams {
    /** PAMGuard MHTKernelParams defaults. */
    std::size_t n_hold = 20;
    std::size_t n_pruneback = 4;
    std::size_t n_pruneback_start = 5;
    int max_coast = 3;
};

template <typename T>
struct MhtTrackBitset {
    static constexpr int kJunkTrack = 1;

    MhtBitset bits;
    std::unique_ptr<MhtChi2<T>> chi2;
    int flag = 0;

    [[nodiscard]] double get_chi2() const {
        return chi2 ? chi2->get_chi2() : 0.0;
    }
};

/**
 * Port of PAMGuard's MHTKernel: every detection branches all track
 * hypotheses into include/exclude pairs, chi2 values are updated per branch,
 * and the possibility mix is pruned to nHold branches by taking the
 * lowest-chi2 branch (stable sort, matching Collections.sort), keeping
 * branches with the same prefix up to kcount - nPruneback, removing branches
 * sharing detections, confirming branches whose coast count reaches maxCoast
 * (trimmed by one detection unless confirming all), and always re-adding an
 * all-coasts branch when absent.
 */
template <typename T>
class MhtKernel {
public:
    MhtKernel(std::unique_ptr<MhtChi2Provider<T>> provider, MhtKernelParams params = {})
        : provider_(std::move(provider)), params_(params) {}

    void add_detection(const T& detection) {
        data_units_.push_back(detection);
        ++kcount_;
        provider_->add_detection(detection, kcount_);
        grow_prob_matrix(detection);
        prune_prob_matrix(false);
    }

    void confirm_remaining_tracks() {
        if (!possible_tracks_.empty()) {
            prune_prob_matrix(true);
        }
    }

    /** MHTKernel.clearKernel: full reset for a fresh run. */
    void clear_kernel() {
        possible_tracks_.clear();
        active_tracks_.clear();
        confirmed_tracks_.clear();
        data_units_.clear();
        provider_->clear();
        kcount_ = 0;
        started_ = false;
    }

    /**
     * MHTKernel.clearKernelGarbage: drop all data before the new reference
     * index once no hypothesis references it. Deletes confirmed tracks, so
     * callers must drain them first.
     */
    void clear_kernel_garbage(std::size_t new_ref_index) {
        if (new_ref_index == 0) {
            return;
        }
        confirmed_tracks_.clear();
        data_units_.erase(data_units_.begin(),
                          data_units_.begin() + static_cast<std::ptrdiff_t>(new_ref_index));
        for (auto& track : possible_tracks_) {
            track.bits = track.bits.range(new_ref_index, kcount_);
            track.chi2->clear_kernel_garbage(new_ref_index);
        }
        kcount_ -= new_ref_index;
        provider_->clear_kernel_garbage(new_ref_index);
    }

    /** MHTKernel.getFirstDetectionIndex over the current possibility mix. */
    [[nodiscard]] std::size_t first_detection_index() const {
        std::size_t first = kcount_;
        bool any = false;
        for (const auto& track : possible_tracks_) {
            first = any ? std::min(first, track.bits.first_set_bit(kcount_)) : track.bits.first_set_bit(kcount_);
            any = true;
        }
        return any ? first : kcount_;
    }

    [[nodiscard]] const T* last_data_unit() const {
        return data_units_.empty() ? nullptr : &data_units_.back();
    }

    [[nodiscard]] std::size_t kcount() const { return kcount_; }
    [[nodiscard]] std::size_t possibility_count() const { return possible_tracks_.size(); }
    [[nodiscard]] std::size_t confirmed_track_count() const { return confirmed_tracks_.size(); }
    [[nodiscard]] const MhtTrackBitset<T>& confirmed_track(std::size_t index) const { return confirmed_tracks_[index]; }
    [[nodiscard]] std::size_t active_track_count() const { return active_tracks_.size(); }

private:
    std::unique_ptr<MhtChi2Provider<T>> provider_;
    MhtKernelParams params_;
    std::vector<T> data_units_;
    std::vector<MhtTrackBitset<T>> possible_tracks_;
    std::vector<MhtTrackBitset<T>> confirmed_tracks_;
    std::vector<MhtTrackBitset<T>> active_tracks_;
    std::size_t kcount_ = 0;
    bool started_ = false;

    void grow_prob_matrix(const T& detection) {
        std::vector<MhtTrackBitset<T>> new_possibilities;

        if (!started_) {
            started_ = true;
            MhtTrackBitset<T> with;
            with.bits = MhtBitset(1);
            with.bits.set(0, true);
            with.chi2 = provider_->new_chi2();
            MhtTrackBitset<T> without;
            without.bits = MhtBitset(1);
            without.bits.set(0, false);
            without.chi2 = provider_->new_chi2();
            new_possibilities.push_back(std::move(with));
            new_possibilities.push_back(std::move(without));
        }
        else {
            const std::size_t index = kcount_ - 1;
            for (auto& track : possible_tracks_) {
                MhtTrackBitset<T> include;
                include.bits = track.bits;
                include.bits.set(index, true);
                include.chi2 = track.chi2->clone_chi2();
                include.flag = track.flag;
                MhtTrackBitset<T> exclude;
                exclude.bits = include.bits;
                exclude.bits.set(index, false);
                exclude.chi2 = track.chi2->clone_chi2();
                exclude.flag = track.flag;
                new_possibilities.push_back(std::move(include));
                new_possibilities.push_back(std::move(exclude));
            }
        }

        for (auto& track : new_possibilities) {
            track.chi2->update(detection, track.bits, kcount_);
        }
        possible_tracks_ = std::move(new_possibilities);
    }

    void prune_prob_matrix(bool confirm_all) {
        if (kcount_ <= params_.n_pruneback_start && !confirm_all) {
            return;
        }

        std::stable_sort(possible_tracks_.begin(), possible_tracks_.end(),
                         [](const MhtTrackBitset<T>& left, const MhtTrackBitset<T>& right) {
                             return left.get_chi2() < right.get_chi2();
                         });

        std::vector<MhtTrackBitset<T>> pruned;
        std::vector<MhtTrackBitset<T>> newly_confirmed;
        std::vector<MhtTrackBitset<T>> active;

        const std::size_t pruneback = confirm_all ? 0 : params_.n_pruneback;
        auto pool = std::move(possible_tracks_);
        possible_tracks_.clear();

        for (std::size_t hold = 0; hold < params_.n_hold; ++hold) {
            if (pool.empty()) {
                break;
            }

            auto& current = pool.front();
            const MhtBitset current_prefix = current.bits.prefix(kcount_ - pruneback);

            std::vector<bool> index_confirm(pool.size(), false);
            std::vector<bool> index_remove(pool.size(), false);
            for (std::size_t j = 0; j < pool.size(); ++j) {
                const MhtBitset test_prefix = pool[j].bits.prefix(kcount_ - pruneback);
                if (test_prefix.equals(current_prefix)) {
                    index_confirm[j] = true;
                }
                else if (test_prefix.intersects(current_prefix)) {
                    index_remove[j] = true;
                }
            }

            const int n_coasts = current.chi2->get_n_coasts();
            if (n_coasts >= params_.max_coast || confirm_all || current.flag == MhtTrackBitset<T>::kJunkTrack) {
                MhtTrackBitset<T> confirmed;
                confirmed.bits = confirm_all ? current.bits : current.bits.prefix(kcount_ - 1);
                confirmed.chi2 = current.chi2->clone_chi2();
                confirmed.flag = current.flag;
                for (std::size_t j = 0; j < pool.size(); ++j) {
                    if (pool[j].bits.intersects(confirmed.bits)) {
                        index_remove[j] = true;
                    }
                }
                newly_confirmed.push_back(std::move(confirmed));
            }
            else {
                MhtTrackBitset<T> active_track;
                active_track.bits = current_prefix;
                active_track.chi2 = current.chi2->clone_chi2();
                active.push_back(std::move(active_track));
                for (std::size_t j = 0; j < pool.size(); ++j) {
                    if (index_confirm[j]) {
                        MhtTrackBitset<T> keep;
                        keep.bits = pool[j].bits;
                        keep.chi2 = pool[j].chi2->clone_chi2();
                        keep.flag = pool[j].flag;
                        pruned.push_back(std::move(keep));
                    }
                }
            }

            std::vector<MhtTrackBitset<T>> remaining;
            for (std::size_t j = 0; j < pool.size(); ++j) {
                if (!index_remove[j] && !index_confirm[j]) {
                    remaining.push_back(std::move(pool[j]));
                }
            }
            pool = std::move(remaining);
        }

        possible_tracks_ = std::move(pruned);
        active_tracks_ = std::move(active);

        MhtTrackBitset<T> coasts;
        coasts.bits = MhtBitset(kcount_);
        coasts.chi2 = provider_->new_chi2();
        bool add = true;
        for (std::size_t j = 0; j < possible_tracks_.size(); ++j) {
            if (possible_tracks_[possible_tracks_.size() - j - 1].bits.equals(coasts.bits)) {
                add = false;
            }
        }
        if (add) {
            possible_tracks_.push_back(std::move(coasts));
        }

        for (auto& confirmed : newly_confirmed) {
            confirmed_tracks_.push_back(std::move(confirmed));
        }
    }
};

} // namespace pamguard::detectors
