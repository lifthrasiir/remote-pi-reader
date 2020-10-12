CXXFLAGS=-O3 -march=native -mtune=native
LDFLAGS=-lcurl
BINS=display_pi self_locate_pi

.PHONY: all
all: $(BINS)

.PHONY: clean
clean:
	@$(RM) -f $(BINS)

%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

display_pi: remote_pi_reader.h
self_locate_pi: remote_pi_reader.h

