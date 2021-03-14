# Remote π reader

This code fetches decimal digits of π (≈ 3.141592...) on demand from Google Cloud Platform's [50 trillion digits of pi][pi50t]. It was specifically used to verify and subsequently extend self-locating strings within π (OEIS [A057679], [A057680], [A064810]).

Since the GCP dataset amounts to 21 TB, it is very hard to do anything without downloading it piece by piece (which is 250 GB each). It would be of course possible to launch Compute Engine instances and fetch them via gsutil, but I didn't feel like that; I don't have a GCP account and I couldn't easily guess which instance type I need. So I instead wrote some code to fetch them via HTTPS and ran it in the background for a month (which led me to conclude this would have taken about a week in GCP). It turned out to be a good stress test for my network environment as well.

[pi50t]: https://storage.googleapis.com/pi50t/index.html
[A057679]: https://oeis.org/A057679
[A057680]: https://oeis.org/A057680
[A064810]: https://oeis.org/A064810

## Usage

Simple. Bring your standard UNIXy C++14 environment, make sure libcurl is installed, and go type `make`—I hate `configure`. Two executables will come to life:

* `./display_pi N` prints digits starting from Nth digit. `1` is considered zeroth. It prints each block received in each line and will not be necessarily smooth. Deal with it.
* `./self_locate_pi N` tries to find self-locating digits starting from Nth digit. `self_locate_pi.sh` wraps this into a logged and restartable script. `self_locate_pi.log` is the actual work log.

Also you can adapt `remote_pi_reader.h` to suit your needs.

Disclaimer: Google can block the access to the GCP dataset for any reason at any moment.

## License

This code is put in the public domain, or alternatively [Creative Commons Zero 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/legalcode).

