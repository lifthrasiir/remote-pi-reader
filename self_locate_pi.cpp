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
		RemotePiReader pi(static_cast<size_t>(CONTEXT_SIZE), 28);

		long long start = 0;
		if (argc > 1) start = std::atoll(argv[1]);
		if (start < 0) start = 0;

		pi.read(start, [&](long long offset, const char *digits, long long ndigits) {
			char buf[32];

			if (offset < 1000000 + CONTEXT_SIZE) {
				// no optimization for first 10^6 digits
				const long long start = max(-CONTEXT_SIZE, -offset);
				size_t buflen = snprintf(buf, sizeof buf, "%lld", offset + start);
				for (long long i = start; i < ndigits; ++i) {
					if (memcmp(digits + i, buf, buflen) == 0) {
						fprintf(stderr, "self-locating at %lld (0-based)\n", offset + i);
					}
					buflen = snprintf(buf, sizeof buf, "%lld", offset + i + 1);
					if (memcmp(digits + i, buf, buflen) == 0) {
						fprintf(stderr, "self-locating at %lld (1-based)\n", offset + i + 1);
					}
				}
			} else {
				// ignore last 4 digits and use the fast string search to locate candidates
				const long long GROUP_SIZE = 10000;
				long long group = (offset - CONTEXT_SIZE) / GROUP_SIZE;
				long long group_offset = group * GROUP_SIZE - offset;
				while (group_offset < ndigits) {
					const size_t groupbuflen = snprintf(buf, sizeof buf, "%lld", group);

					const long long next_group_offset = group_offset + GROUP_SIZE;
					const long long start = max(group_offset, -CONTEXT_SIZE);
					const long long end = min(next_group_offset + static_cast<long long>(groupbuflen) - 1, ndigits);

					const char *ptr = digits + start;
					const char *limit = digits + end;
					while (ptr != limit) {
						const auto found = static_cast<const char*>(memmem(ptr, limit - ptr, buf, groupbuflen));
						if (!found) break;

						const auto lower_digits =
							static_cast<int>(found[groupbuflen]) * 1000 +
							static_cast<int>(found[groupbuflen + 1]) * 100 +
							static_cast<int>(found[groupbuflen + 2]) * 10 +
							static_cast<int>(found[groupbuflen + 3]) -
							static_cast<int>('0') * 1111;
						const auto ingroup_offset = static_cast<int>((found - digits) - group_offset);
						if (lower_digits == ingroup_offset) {
							fprintf(stderr, "self-locating at %lld\n", group * GROUP_SIZE + ingroup_offset);
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

