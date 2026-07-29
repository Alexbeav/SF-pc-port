#include "sf/psx/function_map.hpp"

#include "sf/core/error.hpp"
#include "sf/core/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

namespace sf::psx {
namespace {

std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

void appendLe32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
    bytes.push_back(static_cast<std::byte>(value >> 16U));
    bytes.push_back(static_cast<std::byte>(value >> 24U));
}

std::uint32_t structuralInstruction(std::uint32_t instruction) {
    const auto opcode = instruction >> 26U;
    if (opcode == 0x02U || opcode == 0x03U) {
        return instruction & 0xFC000000U;
    }
    if (opcode != 0U) {
        return instruction & 0xFFFF0000U;
    }
    return instruction;
}

bool isControlTransfer(std::uint32_t instruction) {
    const auto opcode = instruction >> 26U;
    if (opcode == 0x01U || (opcode >= 0x02U && opcode <= 0x07U)) {
        return true;
    }
    if (opcode != 0U) {
        return false;
    }
    const auto function = instruction & 0x3fU;
    return function == 0x08U || function == 0x09U;
}

} // namespace

std::vector<FunctionCandidate> discoverFunctionCandidates(
    std::span<const std::byte> text,
    std::uint32_t load_address,
    std::uint32_t entry_point,
    bool include_stack_prologues,
    std::span<const std::uint32_t> additional_entry_points) {
    if (text.size() % sizeof(std::uint32_t) != 0) {
        throw core::Error{core::ErrorCode::invalid_argument, "MIPS text size is not word-aligned"};
    }
    if (text.size() > std::numeric_limits<std::uint32_t>::max() - load_address) {
        throw core::Error{core::ErrorCode::invalid_argument, "MIPS text range overflows the address space"};
    }
    const auto text_end = load_address + static_cast<std::uint32_t>(text.size());
    if (entry_point < load_address || entry_point >= text_end || entry_point % 4U != 0) {
        throw core::Error{core::ErrorCode::invalid_argument, "Entry point is outside the MIPS text range"};
    }

    std::map<std::uint32_t, std::size_t> candidates;
    candidates.emplace(entry_point, 0);
    for (const auto address : additional_entry_points) {
        if (address < load_address || address >= text_end ||
            address % sizeof(std::uint32_t) != 0U) {
            throw core::Error{core::ErrorCode::invalid_argument,
                              "Additional function seed is outside MIPS text"};
        }
        candidates.try_emplace(address, 0U);
    }
    for (const auto& call : discoverDirectCalls(text, load_address)) {
        if (call.target_in_text) {
            ++candidates[call.target];
        }
    }
    if (include_stack_prologues) {
        const auto explicit_candidates = candidates;
        for (std::size_t offset = 0; offset < text.size(); offset += 4U) {
            const auto instruction = readLe32(text, offset);
            const auto opcode = instruction >> 26U;
            const auto source = (instruction >> 21U) & 0x1fU;
            const auto target = (instruction >> 16U) & 0x1fU;
            const auto immediate = instruction & 0xffffU;
            if ((opcode == 0x08U || opcode == 0x09U) && source == 29U &&
                target == 29U && (immediate & 0x8000U) != 0U) {
                const auto address =
                    load_address + static_cast<std::uint32_t>(offset);
                if (candidates.contains(address)) {
                    continue;
                }

                // Retail functions sometimes load a global in one or two
                // instructions before allocating their stack frame.  A direct
                // JAL targets that prefix, while a blind prologue scan used to
                // invent a second function at the later `addiu sp,sp,-N`.
                // Coalesce only a very small straight-line prefix; any branch,
                // jump, return, or wider gap keeps the prologue as an
                // independent conservative seed.
                const auto next = explicit_candidates.lower_bound(address);
                auto preceded_by_entry_prefix = false;
                if (next != explicit_candidates.begin()) {
                    const auto previous = std::prev(next)->first;
                    const auto distance = address - previous;
                    if (distance <= 4U * sizeof(std::uint32_t)) {
                        preceded_by_entry_prefix = true;
                        for (auto cursor = previous; cursor < address;
                             cursor += sizeof(std::uint32_t)) {
                            const auto prior_offset =
                                static_cast<std::size_t>(cursor - load_address);
                            if (isControlTransfer(
                                    readLe32(text, prior_offset))) {
                                preceded_by_entry_prefix = false;
                                break;
                            }
                        }
                    }
                }
                if (!preceded_by_entry_prefix) {
                    candidates.emplace(address, 0U);
                }
            }
        }
    }

    std::vector<FunctionCandidate> result;
    result.reserve(candidates.size());
    for (const auto& [address, call_count] : candidates) {
        result.push_back(FunctionCandidate{address, call_count});
    }
    return result;
}

std::vector<DirectCall> discoverDirectCalls(
    std::span<const std::byte> text,
    std::uint32_t load_address) {
    if (text.size() % sizeof(std::uint32_t) != 0) {
        throw core::Error{core::ErrorCode::invalid_argument,
                          "MIPS text size is not word-aligned"};
    }
    if (text.size() > std::numeric_limits<std::uint32_t>::max() -
                          load_address) {
        throw core::Error{core::ErrorCode::invalid_argument,
                          "MIPS text range overflows the address space"};
    }
    const auto text_end =
        load_address + static_cast<std::uint32_t>(text.size());
    std::vector<DirectCall> result;
    for (std::size_t offset = 0; offset < text.size(); offset += 4U) {
        const auto instruction = readLe32(text, offset);
        if ((instruction & 0xFC000000U) != 0x0C000000U) {
            continue;
        }
        const auto site = load_address + static_cast<std::uint32_t>(offset);
        const auto target = ((site + 4U) & 0xF0000000U) |
                            ((instruction & 0x03FFFFFFU) << 2U);
        result.push_back(
            DirectCall{site, target, target >= load_address &&
                                         target < text_end &&
                                         target % 4U == 0U});
    }
    return result;
}

std::vector<FunctionFingerprint> fingerprintFunctionCandidates(
    std::span<const std::byte> text,
    std::uint32_t load_address,
    std::uint32_t entry_point,
    bool include_stack_prologues,
    std::span<const std::uint32_t> additional_entry_points) {
    const auto candidates =
        discoverFunctionCandidates(text, load_address, entry_point,
                                   include_stack_prologues,
                                   additional_entry_points);
    std::vector<FunctionFingerprint> result;
    result.reserve(candidates.size());

    constexpr std::uint32_t return_instruction = 0x03E00008U;
    constexpr std::size_t maximum_instructions = 4096U;
    for (const auto& candidate : candidates) {
        const auto start =
            static_cast<std::size_t>(candidate.address - load_address);
        const auto candidate_index = static_cast<std::size_t>(
            &candidate - candidates.data());
        const auto next = candidate_index + 1U < candidates.size()
                              ? static_cast<std::size_t>(
                                    candidates[candidate_index + 1U].address -
                                    load_address)
                              : text.size();
        const auto maximum_end = std::min(
            text.size(), start + maximum_instructions * sizeof(std::uint32_t));
        const auto scan_end = std::min(next, maximum_end);

        auto end = scan_end;
        auto has_return = false;
        for (auto offset = start; offset + 4U <= scan_end; offset += 4U) {
            if (readLe32(text, offset) != return_instruction) {
                continue;
            }
            end = std::min(scan_end, offset + 8U);
            has_return = true;
            break;
        }
        if (end <= start) {
            end = std::min(text.size(), start + sizeof(std::uint32_t));
        }

        std::vector<std::byte> structural;
        structural.reserve(end - start);
        std::set<std::uint32_t> callees;
        for (auto offset = start; offset + 4U <= end; offset += 4U) {
            const auto instruction = readLe32(text, offset);
            appendLe32(structural, structuralInstruction(instruction));
            if ((instruction & 0xFC000000U) == 0x0C000000U) {
                const auto pc =
                    load_address + static_cast<std::uint32_t>(offset);
                const auto target =
                    ((pc + 4U) & 0xF0000000U) |
                    ((instruction & 0x03FFFFFFU) << 2U);
                callees.insert(target);
            }
        }

        const auto exact = text.subspan(start, end - start);
        result.push_back(FunctionFingerprint{
            candidate.address,
            candidate.static_call_count,
            (end - start) / sizeof(std::uint32_t),
            callees.size(),
            has_return,
            core::toHex(core::sha256(exact)),
            core::toHex(core::sha256(structural)),
        });
    }
    return result;
}

} // namespace sf::psx
