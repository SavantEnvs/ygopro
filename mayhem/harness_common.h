#ifndef MAYHEM_HARNESS_COMMON_H
#define MAYHEM_HARNESS_COMMON_H

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace mayhem {

// BufferIO::DecodeUTF8 goes through mbsrtowcs, which only decodes UTF-8 in a UTF-8 locale. Set once, never changed.
inline void InitLocale() {
	if (!std::setlocale(LC_CTYPE, "C.UTF-8"))
		std::setlocale(LC_CTYPE, "en_US.UTF-8");
}

// The replay and lflist loaders only take file paths. One scratch file per process, fully rewritten for
// every input, so each input's result depends only on that input.
class ScratchFile {
public:
	ScratchFile() {
		const char* dir = std::getenv("TMPDIR");
		path_ = std::string(dir && *dir ? dir : "/tmp") + "/ygopro-fuzz-XXXXXX";
		fd_ = mkstemp(&path_[0]);
	}
	~ScratchFile() {
		if (fd_ >= 0) {
			close(fd_);
			unlink(path_.c_str());
		}
	}
	bool Write(const uint8_t* data, size_t size) {
		if (fd_ < 0 || ftruncate(fd_, 0) != 0 || lseek(fd_, 0, SEEK_SET) != 0)
			return false;
		size_t off = 0;
		while (off < size) {
			ssize_t n = write(fd_, data + off, size - off);
			if (n <= 0)
				return false;
			off += static_cast<size_t>(n);
		}
		return true;
	}
	const std::string& path() const { return path_; }

private:
	int fd_{-1};
	std::string path_;
};

}

#endif
