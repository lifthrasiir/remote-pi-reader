#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include "remote_pi_reader.h"

using namespace std;

int main(int argc, char **argv) {
	try {
		RemotePiReader pi;

		long long start = 0;
		if (argc > 1) start = std::atoll(argv[1]);
		if (start < 0) start = 0;

		pi.read(start, [](long long offset, const char *digits, long long ndigits) {
			printf("%lld: %.*s\n", offset, static_cast<int>(ndigits), digits);
			return true;
		});
	} catch (const char *s) {
		fprintf(stderr, "Error: %s\n", s);
	}
}

