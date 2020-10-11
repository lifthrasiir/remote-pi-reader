#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <utility>
#include "remote_pi_reader.h"

using namespace std;
using namespace std::chrono;

int main(int argc, char **argv) {
	try {
		const long long CONTEXT_SIZE = 16;
		RemotePiReader pi(static_cast<size_t>(CONTEXT_SIZE));

		long long start = 0;
		if (argc > 1) start = std::atoll(argv[1]);
		if (start < 0) start = 0;

		const long long CHECKPOINT_PERIOD_LOG2 = 28;
		long long checkpoint = -1;
		pi.read(start, [&](long long offset, const char *digits, long long ndigits) {
			if ((checkpoint >> CHECKPOINT_PERIOD_LOG2) != (offset >> CHECKPOINT_PERIOD_LOG2)) {
				checkpoint = offset;
				const auto network_secs = duration_cast<milliseconds>(pi.network_time()).count() / 1000.0;
				const auto callback_secs = duration_cast<milliseconds>(pi.callback_time()).count() / 1000.0;
				fprintf(stderr, "checkpoint: %lld [net %.1fs calc %.1fs]\n", checkpoint, network_secs, callback_secs);
			}

			if (offset < 10000000 + CONTEXT_SIZE) {
				// no optimization for first 10^7 digits
				for (long long i = max(-CONTEXT_SIZE, -offset); i < ndigits; ++i) {
					char buf[32];
					const size_t buflen = snprintf(buf, sizeof buf, "%lld", offset + i);
					if (memcmp(digits + i, buf, buflen) == 0) {
						fprintf(stderr, "self-locating at %lld\n", offset + i);
					}
				}
			} else {
				// ignore last 4 digits and use the fast string search to locate candidates
				const long long GROUP_SIZE = 10000;
				long long group = (offset - CONTEXT_SIZE) / GROUP_SIZE;
				long long group_offset = group * GROUP_SIZE - offset;
				while (group_offset < ndigits) {
					char buf[32];
					const size_t groupbuflen = snprintf(buf, sizeof buf, "%lld", group);

					const long long next_group_offset = group_offset + GROUP_SIZE;
					const long long start = max(group_offset, -CONTEXT_SIZE);
					const long long end = min(next_group_offset + static_cast<long long>(groupbuflen) - 1, ndigits);

					const char *ptr = digits + start;
					const char *limit = digits + end;
					while (ptr != limit) {
						const auto found = static_cast<const char*>(memmem(ptr, limit - ptr, buf, groupbuflen));
						if (!found) break;
						const long long found_offset = (found - digits) + offset;
						const size_t buflen = snprintf(buf, sizeof buf, "%lld", found_offset);
						if (memcmp(found, buf, buflen) == 0) {
							fprintf(stderr, "self-locating at %lld\n", found_offset);
						}
						ptr = found + 1;
					}

					++group;
					group_offset += GROUP_SIZE;
				}
			}
			return true;
		});
	} catch (const char *s) {
		fprintf(stderr, "Error: %s\n", s);
	}
}

