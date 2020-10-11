#pragma once

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <functional>
#include <curl/curl.h>

class RemotePiReader {
public:
	constexpr const char URL_TEMPLATE[] = "https://storage.googleapis.com/pi50t/Pi%%20-%%20Dec%%20-%%20Chudnovsky/Pi%%20-%%20Dec%%20-%%20Chudnovsky%%20-%%20%d.ycd";
	constexpr long long BLOCK_SIZE = 1000000000000ll;
	constexpr int BLOCK_COUNT = 50;

public:
	RemotePiReader() {
		curl_ = curl_easy_init();
		if (!curl_) throw "failed to initialize curl";
	}

	template <class Callback>
	void read(long long digit_offset, Callback&& callback) {
		constexpr int block_offset = static_cast<int>(digit_offset / BLOCK_SIZE);
		constexpr long long inblock_offset = digit_offset % BLOCK_SIZE;
		constexpr long long word_offset = inblock_offset / 19;
		constexpr long long inword_offset = inblock_offset % 19;

		if (block_offset_ != block_offset) {
			char url[256];
			std::snprintf(url, sizeof url, URL_TEMPLATE, block_offset);
			curl_easy_setopt(curl_, CURLOPT_URL, url);
			curl_easy_setopt(curl_, CURLOPT_RANGE, "0-1023");

			std::size_t off = 0;
			auto err = perform([=](char *ptr, std::size_t sz) {
				auto found = std::memchr(ptr, 0, sz);
				if (found) {
					block_offset_ = block_offset;
					data_offset_ = (found - ptr) + 1;
					return 0; // causes err to be CURLE_WRITE_ERROR
				}
				off += sz;
				return sz;
			});
			if (err == 0) {
				throw "no data marker in the first 1KB of the .ycd file";
			} else if (err != CURLE_WRITE_ERROR) {
				throw curl_easy_strerror(err);
			}
		}

		// CURLOPT_URL stays same
		char range[256];
		std::snprintf(range, sizeof range, "%lld-",
			data_offset_ + word_offset * 8,
			data_offset_ + word_offset
		curl_easy_setopt(curl_, CURLOPT_RANGE, "0-1023");

	}

private:
	RemotePiReader() = delete;
	RemotePiReader(const RemotePiReader&) = delete;

	typedef std::size_t WriteCallback(char *, std::size_t);

	template <class Callback>
	CURLcode perform(Callback&& callback) {
		std::function<WriteCallback> func{callback};
		curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &write_callback_wrapper);
		curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&func));
		return curl_easy_perform(curl_);
	}

	static std::size_t write_callback_wrapper(char *ptr, std::size_t size, std::size_t, void *data) {
		auto func = reinterpret_cast<std::function<WriteCallback>*>(data);
		return func(ptr, size);
	}

	int block_offset_ = -1;
	std::size_t data_offset_ = 0;

	std::unique_ptr<CURL, curl_deleter> curl_;
	struct curl_deleter {
		operator()(CURL *curl) const { curl_easy_cleanup(curl); }
	};
};

