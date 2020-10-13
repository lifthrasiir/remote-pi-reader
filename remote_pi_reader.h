#pragma once

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include <chrono>
#include <curl/curl.h>

class RemotePiReader {
private:
	static constexpr const char *URL_TEMPLATE = "https://storage.googleapis.com/pi50t/Pi%%20-%%20Dec%%20-%%20Chudnovsky/Pi%%20-%%20Dec%%20-%%20Chudnovsky%%20-%%20%d.ycd";
	static const long long BLOCK_SIZE = 1000000000000ll;
	static const int BLOCK_COUNT = 50;
	static const long long WORD_SIZE = 8;
	static const long long DIGITS_PER_WORD = 19;
	static const long long BLOCK_WORD_COUNT = (BLOCK_SIZE - 1) / DIGITS_PER_WORD + 1;

	static const std::size_t BUFFER_SIZE = std::max(1 << 20, CURL_MAX_READ_SIZE);
	// slight overestimation, should be enough
	static const long long MAX_WORDS_PER_WRITE =
		(static_cast<long long>(BUFFER_SIZE) - 2) / WORD_SIZE + 2;

public:
	static const long long NUM_DIGITS = BLOCK_SIZE * BLOCK_COUNT;

	typedef std::chrono::steady_clock::duration duration;

public:
	RemotePiReader(std::size_t context_size, int checkpoint_period_log2):
		curl_(curl_easy_init()),
		digits_(MAX_WORDS_PER_WRITE * DIGITS_PER_WORD + context_size, '0'),
		context_size_(context_size),
		checkpoint_period_log2_(checkpoint_period_log2)
	{
		if (!curl_) throw "failed to initialize curl";

		//curl_easy_setopt(curl(), CURLOPT_VERBOSE, 1l);
		curl_easy_setopt(curl(), CURLOPT_NOSIGNAL, 1l);
		curl_easy_setopt(curl(), CURLOPT_BUFFERSIZE, long(CURL_MAX_READ_SIZE));
		curl_easy_setopt(curl(), CURLOPT_NOPROGRESS, 0l);
		curl_easy_setopt(curl(), CURLOPT_XFERINFOFUNCTION, &check_stale);
		curl_easy_setopt(curl(), CURLOPT_XFERINFODATA, static_cast<void*>(this));

		buffer_.reserve(BUFFER_SIZE);
	}

	template <class Callback>
	void read(long long digit_offset, Callback&& callback) {
		int block_offset = static_cast<int>(digit_offset / BLOCK_SIZE);
		const long long inblock_offset = digit_offset % BLOCK_SIZE;

		if (block_offset >= BLOCK_COUNT) return;
		if (!read_block(block_offset++, inblock_offset, callback)) return;
		while (block_offset < BLOCK_COUNT) {
			if (!read_block(block_offset++, 0, callback)) return;
		}
	}

	bool checkpoint(long long offset) {
		using namespace std::chrono;

		if (checkpoint_period_log2_ < 0) return true;

		const auto now = steady_clock::now();
		const auto elapsed = now - last_checkpoint_;
		if (elapsed >= 10s || (checkpoint_ >> checkpoint_period_log2_) != (offset >> checkpoint_period_log2_)) {
			const auto mdigits_per_sec = checkpoint_ < 0 ? 0 : (offset - checkpoint_) / double(duration_cast<microseconds>(elapsed).count());
			checkpoint_ = offset;
			last_checkpoint_ = now;
			const auto network_secs = duration_cast<milliseconds>(network_time_).count() / 1000.0;
			const auto callback_secs = duration_cast<milliseconds>(callback_time_).count() / 1000.0;
			std::fprintf(stderr, "checkpoint: %lld [net %.1fs calc %.1fs rate %.2fM/s]\n", checkpoint_, network_secs, callback_secs, mdigits_per_sec);
			if (network_secs >= 30 && mdigits_per_sec < 10) {
				fprintf(stderr, "connection got stale, restarting...\n");
				return false;
			}
		}

		return true;
	}

private:
	static int check_stale(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
		using namespace std::chrono;

		const auto pi = reinterpret_cast<RemotePiReader*>(clientp);
		const auto now = steady_clock::now();
		const auto elapsed = now - pi->last_checkpoint_;
		if (elapsed >= 15s) {
			fprintf(stderr, "connection got stale, restarting...\n");
			return 1;
		}
		return 0;
	}

	template <class Callback>
	bool read_block(int block_offset, long long inblock_offset, Callback&& callback) {
		const long long start_word_offset = inblock_offset / DIGITS_PER_WORD;
		const std::size_t first_digits_to_skip = static_cast<std::size_t>(inblock_offset % DIGITS_PER_WORD);

		if (block_offset_ != block_offset) {
			char url[256];
			snprintf(url, sizeof url, URL_TEMPLATE, block_offset);
			curl_easy_setopt(curl(), CURLOPT_URL, url);
			curl_easy_setopt(curl(), CURLOPT_RANGE, "0-1023");

			std::size_t off = 0;
			auto err = perform([&](const char *ptr, std::size_t sz) {
				auto found = static_cast<const char*>(memchr(ptr, 0, sz));
				if (found) {
					block_offset_ = block_offset;
					data_offset_ = (found - ptr) + 1;
					return false;
				}
				off += sz;
				return true;
			});
			if (err == CURLE_OK) {
				throw "no data marker in the first 1KB of the .ycd file";
			}
			if (err != CURLE_WRITE_ERROR) {
				throw curl_easy_strerror(err);
			}
		}

		const long long byte_offset = data_offset_ + start_word_offset * WORD_SIZE;
		const long long byte_offset_limit = data_offset_ + BLOCK_WORD_COUNT * WORD_SIZE;

		{
			// CURLOPT_URL stays same
			char range[256];
			snprintf(range, sizeof range, "%lld-%lld", byte_offset, byte_offset_limit - 1);
			curl_easy_setopt(curl(), CURLOPT_RANGE, range);
		}

		char partial_word[WORD_SIZE];
		std::size_t last_word_offset = start_word_offset;
		std::size_t last_inword_offset = 0; // always < WORD_SIZE
		const long long block_digit_offset = block_offset * BLOCK_SIZE;
		long long last_digit_offset = inblock_offset;
		auto handle_words = [&]() {
			const char *ptr = buffer_.data();
			const char *end = buffer_.data() + buffer_.size();
			char *digits = digits_.data() + context_size_;
			long long ndigits = 0;

			auto word_offset = last_word_offset;
			auto inword_offset = last_inword_offset;
			auto digit_offset = last_digit_offset;

			while (inword_offset < WORD_SIZE) {
				if (ptr == end) {
					last_inword_offset = inword_offset;
					buffer_.clear();
					return true;
				}
				partial_word[inword_offset++] = *ptr++;
			}
			decode_word(partial_word, digits);
			ndigits += DIGITS_PER_WORD;
			if (word_offset == start_word_offset) {
				digits += first_digits_to_skip;
				ndigits -= first_digits_to_skip;
			}
			++word_offset;

			while (end - ptr >= WORD_SIZE) {
				decode_word(ptr, digits + ndigits);
				ptr += WORD_SIZE;
				ndigits += DIGITS_PER_WORD;
				++word_offset;
			}

			inword_offset = 0;
			while (ptr != end) {
				partial_word[inword_offset++] = *ptr++;
			}

			digit_offset += ndigits;
			if (digit_offset > BLOCK_SIZE) {
				// padded digits are not correct even for non-last blocks
				ndigits -= digit_offset - BLOCK_SIZE;
			}

			const auto old_digit_offset = block_digit_offset + last_digit_offset;
			bool keep_going = checkpoint(old_digit_offset);
			keep_going &= callback(old_digit_offset, digits, ndigits);

			last_word_offset = word_offset;
			last_inword_offset = inword_offset;
			last_digit_offset = digit_offset;
			if (context_size_ > 0) {
				std::memmove(digits_.data(), digits_.data() + ndigits, context_size_);
			}

			buffer_.clear();
			return keep_going;
		};

		auto err = perform([&](const char *ptr, std::size_t sz) {
			if (buffer_.size() + sz > BUFFER_SIZE && !handle_words()) return false;
			buffer_.append(ptr, sz);
			return true;
		});
		if (err != CURLE_OK && err != CURLE_WRITE_ERROR) {
			throw curl_easy_strerror(err);
		}
		if (err == CURLE_OK) {
			return handle_words();
		} else {
			return false;
		}
	}

	typedef std::size_t WriteCallback(const char *, std::size_t);

	template <class Callback>
	CURLcode perform(Callback&& callback) {
		using namespace std::chrono;

		auto before_curl_time = steady_clock::now();
		std::function<WriteCallback> func = [&](const char *ptr, std::size_t sz) {
			const auto after_curl_time = steady_clock::now();
			network_time_ += after_curl_time - before_curl_time;

			const bool keep_going = callback(ptr, sz);

			const auto after_callback_time = steady_clock::now();
			callback_time_ += after_callback_time - after_curl_time;
			before_curl_time = after_callback_time;

			return keep_going ? sz : 0; // causes err to be CURLE_WRITE_ERROR at the end
		};

		curl_easy_setopt(curl(), CURLOPT_WRITEFUNCTION, &write_callback_wrapper);
		curl_easy_setopt(curl(), CURLOPT_WRITEDATA, static_cast<void*>(&func));
		last_checkpoint_ = before_curl_time;
		return curl_easy_perform(curl());
	}

	static std::size_t write_callback_wrapper(char *ptr, std::size_t, std::size_t nmemb, void *data) {
		auto func = reinterpret_cast<std::function<WriteCallback>*>(data);
		return (*func)(ptr, nmemb);
	}

	static constexpr const char *DIGITS =
		"0001020304050607080910111213141516171819"
		"2021222324252627282930313233343536373839"
		"4041424344454647484950515253545556575859"
		"6061626364656667686970717273747576777879"
		"8081828384858687888990919293949596979899";

	static void decode_word(const char *buf, char *digits) {
		const std::uint8_t *word = reinterpret_cast<const std::uint8_t*>(buf);
		const std::uint64_t w =
			static_cast<std::uint64_t>(word[0]) |
			(static_cast<std::uint64_t>(word[1]) << 8) |
			(static_cast<std::uint64_t>(word[2]) << 16) |
			(static_cast<std::uint64_t>(word[3]) << 24) |
			(static_cast<std::uint64_t>(word[4]) << 32) |
			(static_cast<std::uint64_t>(word[5]) << 40) |
			(static_cast<std::uint64_t>(word[6]) << 48) |
			(static_cast<std::uint64_t>(word[7]) << 56);

		const std::uint32_t w0 = static_cast<std::uint32_t>(w % 100000000);
		const std::uint64_t w12 = w / 100000000;
		const std::uint32_t w1 = static_cast<std::uint32_t>(w12 % 100000000);
		const std::uint32_t w2 = static_cast<std::uint32_t>(w12 / 100000000);

		const std::uint32_t w2a = w2 / 100;
		const std::uint32_t w2b = w2 % 100;

		digits[0] = '0' + w2a;
		digits[1] = DIGITS[w2b * 2 + 0];
		digits[2] = DIGITS[w2b * 2 + 1];
		decode_eight(w1, digits + 3);
		decode_eight(w0, digits + 11);
	}

	static void decode_eight(std::uint32_t abcdefgh, char *digits) {
		const std::uint32_t abcd = abcdefgh / 10000;
		const std::uint32_t efgh = abcdefgh % 10000;

		const std::uint32_t ab = abcd / 100;
		const std::uint32_t cd = abcd % 100;
		const std::uint32_t ef = efgh / 100;
		const std::uint32_t gh = efgh % 100;

		digits[0] = DIGITS[ab * 2 + 0];
		digits[1] = DIGITS[ab * 2 + 1];
		digits[2] = DIGITS[cd * 2 + 0];
		digits[3] = DIGITS[cd * 2 + 1];
		digits[4] = DIGITS[ef * 2 + 0];
		digits[5] = DIGITS[ef * 2 + 1];
		digits[6] = DIGITS[gh * 2 + 0];
		digits[7] = DIGITS[gh * 2 + 1];
	}

private:
	RemotePiReader(const RemotePiReader&) = delete;
	RemotePiReader(RemotePiReader&&) = delete;
	const RemotePiReader& operator=(const RemotePiReader&) = delete;

	int block_offset_ = -1;
	std::size_t data_offset_ = 0;

	struct curl_deleter {
		void operator()(CURL *curl) const { curl_easy_cleanup(curl); }
	};
	std::unique_ptr<CURL, curl_deleter> curl_;

	CURL* curl() const { return curl_.get(); }

	std::string buffer_;
	std::vector<char> digits_;
	std::size_t context_size_;

	duration network_time_{};
	duration callback_time_{};

	int checkpoint_period_log2_ = -1;
	long long checkpoint_ = -1;
	std::chrono::steady_clock::time_point last_checkpoint_{};
};

