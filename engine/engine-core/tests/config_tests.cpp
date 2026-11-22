#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/render_config.hpp>
#include <pianolab/engine_core/tuning.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <tuple>

namespace ec = pianolab::engine_core;

namespace {

/// True when every field's built-in default lies inside its own declared range.
template <typename C, typename... Fs> bool defaultsWithinRanges(const std::tuple<Fs...>& fields) {
    const C defaults{};
    return std::apply(
        [&](const auto&... field) {
            return ((defaults.*field.member >= field.min && defaults.*field.member <= field.max) &&
                    ...);
        },
        fields);
}

} // namespace

TEST_CASE("built-in defaults are within their declared ranges") {
    CHECK(defaultsWithinRanges<ec::RenderConfig>(ec::renderConfigFields));
    CHECK(defaultsWithinRanges<ec::TuningConfig>(ec::tuningConfigFields));
}

TEST_CASE("validating the built-in defaults reports nothing") {
    CHECK(ec::validate(ec::RenderConfig{}).errors.empty());
    CHECK(ec::validate(ec::TuningConfig{}).errors.empty());
}

TEST_CASE("valid values pass through unchanged") {
    const auto result = ec::validate(ec::RenderConfig{.sampleRate = 96'000.0, .maxBlock = 64});
    CHECK(result.errors.empty());
    CHECK(result.config->sampleRate == 96'000.0);
    CHECK(result.config->maxBlock == 64);
}

TEST_CASE("an out-of-range value is replaced with its default and reported") {
    const auto result = ec::validate(ec::RenderConfig{.sampleRate = 0.0, .maxBlock = 0}, "render.");

    CHECK(result.config->sampleRate == ec::RenderConfig{}.sampleRate);
    CHECK(result.config->maxBlock == ec::RenderConfig{}.maxBlock);
    REQUIRE(result.errors.size() == 2);
    CHECK(result.errors[0].message ==
          "render.sampleRate: 0 Hz is outside 8000-384000 Hz; using the default 48000 Hz");
    CHECK(result.errors[1].message ==
          "render.maxBlock: 0 samples is outside 1-8192 samples; using the default 512 samples");
}

TEST_CASE("NaN is replaced with the default and reported") {
    const auto result =
        ec::validate(ec::TuningConfig{.a4Hz = std::numeric_limits<double>::quiet_NaN()});

    CHECK(result.config->a4Hz == 440.0);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].message.starts_with("a4Hz: nan Hz is outside"));
}

TEST_CASE("the limits of a range are valid") {
    CHECK(ec::validate(ec::TuningConfig{.a4Hz = 380.0}).errors.empty());
    CHECK(ec::validate(ec::TuningConfig{.a4Hz = 500.0}).errors.empty());
    CHECK(ec::validate(ec::TuningConfig{.a4Hz = 500.1}).errors.size() == 1);
}

TEST_CASE("a part of a valid configuration is valid") {
    struct Whole {
        ec::RenderConfig render;
        ec::TuningConfig tuning;
    };
    const auto render = ec::validate(ec::RenderConfig{.maxBlock = 32}).config;
    const auto tuning = ec::validate(ec::TuningConfig{}).config;
    const auto whole = ec::detail::makeValid(Whole{.render = *render, .tuning = *tuning});

    const auto part = ec::validPart(whole, &Whole::render);
    CHECK(part->maxBlock == 32);
}
