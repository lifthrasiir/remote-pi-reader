#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <utility>
#include "remote_pi_reader.h"

using namespace std;
using namespace std::chrono;

//                          3.1 4 1 5 9 2 6 5 ...
// 0-based index (A064810):   0 1 2 3 4 5[6]7 ...
// 1-based index (A057680):  [1]2 3 4 5 6 7 8 ...
// 2-based index (A057679): 1 2 3 4[5]6 7 8 9 ...
// also try to locate "near-misses", i.e. `0..0<index>` located at <index>.

int main(int argc, char **argv) {
	try {
		const long long CONTEXT_SIZE = 16;
		RemotePiReader pi(static_cast<size_t>(CONTEXT_SIZE) * 2, 30);

		long long start = 0;
		if (argc > 1) start = std::atoll(argv[1]);
		if (start < 0) start = 0;

		pi.read(start, [&](long long offset, const char *digits, long long ndigits) {
			char buf[32], buf2[32];

			auto located = [&](long long i, int delta, size_t len) {
				fprintf(stderr, "self-locating at %lld (%d-based): %.*s[%.*s]%.*s\n",
					offset + i + delta, delta,
					int(CONTEXT_SIZE), digits + (i - CONTEXT_SIZE),
					int(len), digits + i,
					int(CONTEXT_SIZE), digits + (i + len));
			};

			if (offset < 1000000 + CONTEXT_SIZE) {
				// no optimization for first 10^6 digits
				const long long start = max(-CONTEXT_SIZE, -offset);
				size_t buflen = snprintf(buf, sizeof buf, "%lld", offset + start);
				size_t buflen2 = snprintf(buf2, sizeof buf2, "%lld", offset + start + 1);
				for (long long i = start; i < ndigits; ++i) {
					if (memcmp(digits + i, buf, buflen) == 0) located(i, 0, buflen);
					if (memcmp(digits + i, buf2, buflen2) == 0) located(i, 1, buflen2);
					buflen = buflen2;
					memcpy(buf, buf2, buflen2);
					buflen2 = snprintf(buf2, sizeof buf2, "%lld", offset + i + 2);
					if (memcmp(digits + i, buf2, buflen2) == 0) located(i, 2, buflen2);
					// XXX skip near-misses in this mode
				}
			} else {
				// ignore last 4 digits and use the fast string search to locate candidates
				const int GROUP_SIZE = 10000;
				long long group = (offset - CONTEXT_SIZE) / GROUP_SIZE;
				long long group_offset = group * GROUP_SIZE - offset;
				while (group_offset < ndigits) {
					const size_t groupbuflen = snprintf(buf, sizeof buf, "%lld", group);

					const long long next_group_offset = group_offset + GROUP_SIZE;
					const long long start = max(group_offset, -CONTEXT_SIZE);
					const long long end = min(next_group_offset + static_cast<long long>(groupbuflen), ndigits);

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
						const auto delta = lower_digits - ingroup_offset;
						if (delta >= 0 && delta <= 2) {
							// may print false positives, but can be filtered manually
							located(found - digits, delta, groupbuflen + 4);
						}
						// check preceding zeroes for near-misses
						for (int i = 1; delta + i <= 2; ++i) {
							if (i > CONTEXT_SIZE) {
								fprintf(stderr, "amazing! %d zeroes found right before %lld, unfortunately we can't handle this right now\n", int(CONTEXT_SIZE), (found - digits) + offset);
								break;
							}
							if (found[-i] != '0') break;
							if (delta + i >= 0) {
								located(found - digits - i, delta + i, groupbuflen + 4 + i);
							}
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

