#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <ostream>

namespace lob {

// Linear-bucketed nanosecond latency histogram. The first kLinearMax buckets
// each cover one nanosecond; everything above that goes into log buckets so
// the tail is also visible without ballooning memory.
//
// For sub-microsecond engines this gives full 1ns resolution where we live
// (0-100 us) and one-bucket-per-octave resolution out to seconds for outliers.
class LatencyHistogram {
public:
    static constexpr std::size_t kLinearMax  = 100'000;  // 0..99,999 ns at 1 ns
    static constexpr std::size_t kLogBuckets = 30;       // up to ~2^30 ns ~= 1 s

    LatencyHistogram() {
        linear_.fill(0);
        log_.fill(0);
    }

    void record(std::uint64_t ns) noexcept {
        if (ns < kLinearMax) {
            ++linear_[ns];
        } else {
            std::size_t bucket = 0;
            std::uint64_t v = ns;
            while (v > 1 && bucket < kLogBuckets - 1) {
                v >>= 1;
                ++bucket;
            }
            ++log_[bucket];
        }
        ++total_;
        if (ns > max_) max_ = ns;
        sum_ns_ += ns;
    }

    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }
    [[nodiscard]] std::uint64_t max() const noexcept   { return max_; }
    [[nodiscard]] double mean_ns() const noexcept {
        return total_ == 0 ? 0.0 : static_cast<double>(sum_ns_) / static_cast<double>(total_);
    }

    // Returns the nanosecond value at the requested percentile (e.g. 0.99).
    [[nodiscard]] std::uint64_t percentile(double p) const noexcept {
        if (total_ == 0) return 0;
        std::uint64_t threshold =
            static_cast<std::uint64_t>(static_cast<double>(total_) * p);
        if (threshold == 0) threshold = 1;
        std::uint64_t running = 0;
        for (std::size_t i = 0; i < kLinearMax; ++i) {
            running += linear_[i];
            if (running >= threshold) return i;
        }
        for (std::size_t b = 0; b < kLogBuckets; ++b) {
            running += log_[b];
            if (running >= threshold) {
                // Report the lower edge of the log bucket.
                return std::uint64_t{1} << b;
            }
        }
        return max_;
    }

    // HdrHistogram-style summary line.
    void write_summary(std::ostream& os) const {
        os << "count=" << total_
           << " mean_ns="   << mean_ns()
           << " p50="       << percentile(0.50)
           << " p90="       << percentile(0.90)
           << " p99="       << percentile(0.99)
           << " p999="      << percentile(0.999)
           << " p9999="     << percentile(0.9999)
           << " max_ns="    << max_ << '\n';
    }

    // Pipe-separated CSV of (ns, count) for non-empty linear buckets, then
    // (log_bucket_lower, count) for non-empty log buckets. Plot from this.
    void write_csv(std::ostream& os) const {
        os << "ns,count\n";
        for (std::size_t i = 0; i < kLinearMax; ++i) {
            if (linear_[i]) os << i << ',' << linear_[i] << '\n';
        }
        for (std::size_t b = 0; b < kLogBuckets; ++b) {
            if (log_[b]) os << (std::uint64_t{1} << b) << ',' << log_[b] << '\n';
        }
    }

private:
    std::array<std::uint64_t, kLinearMax>  linear_;
    std::array<std::uint64_t, kLogBuckets> log_;
    std::uint64_t total_{0};
    std::uint64_t max_{0};
    std::uint64_t sum_ns_{0};
};

}  // namespace lob
