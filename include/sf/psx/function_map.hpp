#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf::psx {

struct FunctionCandidate {
    std::uint32_t address{};
    std::size_t static_call_count{};
};

struct FunctionFingerprint {
    std::uint32_t address{};
    std::size_t static_call_count{};
    std::size_t instruction_count{};
    std::size_t direct_callee_count{};
    bool has_return{};
    std::string exact_sha256;
    std::string structural_sha256;
};

struct DirectCall {
    std::uint32_t site{};
    std::uint32_t target{};
    bool target_in_text{};
};

// Produces a conservative seed map from direct JAL targets. It is intended as
// deterministic input for later disassembly work, not as a final function map.
[[nodiscard]] std::vector<FunctionCandidate> discoverFunctionCandidates(
    std::span<const std::byte> text,
    std::uint32_t load_address,
    std::uint32_t entry_point,
    bool include_stack_prologues = false,
    std::span<const std::uint32_t> additional_entry_points = {});

// Fingerprints each direct-call seed through its first `jr ra` delay slot.
// Structural hashes erase instruction immediates and absolute jump targets so
// equivalent code can be compared across executables with shifted addresses.
// These spans remain analysis seeds: tail-called and hand-written routines may
// require control-flow refinement.
[[nodiscard]] std::vector<FunctionFingerprint> fingerprintFunctionCandidates(
    std::span<const std::byte> text,
    std::uint32_t load_address,
    std::uint32_t entry_point,
    bool include_stack_prologues = false,
    std::span<const std::uint32_t> additional_entry_points = {});

[[nodiscard]] std::vector<DirectCall> discoverDirectCalls(
    std::span<const std::byte> text,
    std::uint32_t load_address);

} // namespace sf::psx
