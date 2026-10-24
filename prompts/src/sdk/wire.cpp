#include "wire.hpp"
#include "codec.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace prompt {

namespace {

constexpr std::uint32_t wire_no_clipboard = 1U << 0U;
constexpr std::uint32_t wire_clear = 1U << 1U;
constexpr std::uint32_t wire_redacted = 1U << 2U;
constexpr std::uint32_t wire_failed = 1U << 3U;
constexpr std::uint32_t wire_stdin_consumed = 1U << 4U;
constexpr std::uint32_t wire_stdout_streamed = 1U << 5U;
constexpr std::uint32_t wire_stderr_streamed = 1U << 6U;

} // namespace

std::string encode_outcome(segment_outcome const& outcome) {
    std::string facts_blob = outcome.published.encode();
    std::uint32_t flags = 0;
    if (outcome.no_clipboard) flags |= wire_no_clipboard;
    if (outcome.clear) flags |= wire_clear;
    if (outcome.redacted) flags |= wire_redacted;
    if (outcome.failed) flags |= wire_failed;
    if (outcome.stdin_consumed) flags |= wire_stdin_consumed;
    if (outcome.stdout_streamed) flags |= wire_stdout_streamed;
    if (outcome.stderr_streamed) flags |= wire_stderr_streamed;

    std::string blob;
    blob.reserve(outcome.out.size() + outcome.err.size() + outcome.stderr_buffer.size() + facts_blob.size() + 32);
    codec::append_u32(blob, outcome.out.size());
    codec::append_u32(blob, outcome.err.size());
    codec::append_u32(blob, outcome.stderr_buffer.size());
    codec::append_u32(blob, facts_blob.size());
    codec::append_u32(blob, flags);
    codec::append_u32(blob, static_cast<std::uint32_t>(outcome.exit_code));
    blob += outcome.out;
    blob += outcome.err;
    blob += outcome.stderr_buffer;
    blob += facts_blob;
    return blob;
}

std::optional<segment_outcome> decode_outcome(std::string_view blob) {
    std::size_t offset = 0;
    std::uint32_t out_size = 0;
    std::uint32_t err_size = 0;
    std::uint32_t stderr_size = 0;
    std::uint32_t facts_size = 0;
    std::uint32_t flags = 0;
    std::uint32_t exit_code = 0;
    if (!codec::read_u32(blob, offset, out_size) || !codec::read_u32(blob, offset, err_size) ||
        !codec::read_u32(blob, offset, stderr_size) || !codec::read_u32(blob, offset, facts_size) ||
        !codec::read_u32(blob, offset, flags) || !codec::read_u32(blob, offset, exit_code)) {
        return std::nullopt;
    }

    segment_outcome outcome;
    auto take = [&](std::uint32_t size, std::string& target) {
        if (blob.size() - offset < size) return false;
        target.assign(blob.data() + offset, size);
        offset += size;
        return true;
    };
    std::string facts_blob;
    if (!take(out_size, outcome.out) || !take(err_size, outcome.err) || !take(stderr_size, outcome.stderr_buffer) ||
        !take(facts_size, facts_blob)) {
        return std::nullopt;
    }

    outcome.published = facts::decode(facts_blob);
    outcome.no_clipboard = (flags & wire_no_clipboard) != 0;
    outcome.clear = (flags & wire_clear) != 0;
    outcome.redacted = (flags & wire_redacted) != 0;
    outcome.failed = (flags & wire_failed) != 0;
    outcome.stdin_consumed = (flags & wire_stdin_consumed) != 0;
    outcome.stdout_streamed = (flags & wire_stdout_streamed) != 0;
    outcome.stderr_streamed = (flags & wire_stderr_streamed) != 0;
    outcome.exit_code = static_cast<int>(exit_code);
    return outcome;
}

std::vector<std::string> drain_all(std::span<int const> fds) {
    std::vector<std::string> blobs(fds.size());
    struct watched {
        pollfd fd;
        std::size_t index;
    };
    std::vector<watched> polled;
    polled.reserve(fds.size());
    for (std::size_t i = 0; i < fds.size(); ++i) {
        if (fds[i] < 0) continue;
        polled.push_back(watched{.fd = {.fd = fds[i], .events = POLLIN, .revents = 0}, .index = i});
    }

    std::array<char, 16384> buffer{};
    // `raw` mirrors `polled` for the duration of the whole drain; an entry
    // removed from one must leave the other, or `revents` would describe a
    // different descriptor and the read below could block on a pipe that is
    // not ready yet.
    std::vector<pollfd> raw;
    raw.reserve(polled.size());
    for (auto const& w : polled) raw.push_back(w.fd);

    while (!polled.empty()) {
        if (::poll(raw.data(), raw.size(), -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (std::size_t p = 0; p < polled.size();) {
            if (raw[p].revents == 0) {
                ++p;
                continue;
            }
            auto const n = ::read(polled[p].fd.fd, buffer.data(), buffer.size());
            if (n > 0) {
                blobs[polled[p].index].append(buffer.data(), static_cast<std::size_t>(n));
                ++p;
                continue;
            }
            if (n < 0 && errno == EINTR) {
                ++p;
                continue;
            }
            // EOF (or a read error): stop watching this fd. It stays open until
            // the single close loop below, which closes every fd exactly once.
            raw.erase(raw.begin() + static_cast<std::ptrdiff_t>(p));
            polled.erase(polled.begin() + static_cast<std::ptrdiff_t>(p));
        }
    }

    for (int fd : fds) {
        if (fd >= 0) ::close(fd);
    }
    return blobs;
}

} // namespace prompt
