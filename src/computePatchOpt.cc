#include <stdexcept>
#include <fstream>
#include <string>

extern "C" {
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
}

typedef unsigned long cost_t;

class file_mapping final {
	char *begin;
	char *end;

public:
	file_mapping(const char *file) {
		struct stat st;
		void *mmaped;
		int fd;

		if((fd = open(file, O_RDONLY)) == -1
			|| fstat(fd, &st) == -1
			|| (mmaped = mmap(NULL, st.st_size,
				PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
			throw std::runtime_error(std::string("file_mapping: ", file));
		}

		this->begin = static_cast<char *>(mmaped);
		this->end = this->begin + st.st_size;

		close(fd);
	}

	~file_mapping() {
		munmap(this->begin, this->end - this->begin);
	}
};

struct patch final {
	const file_mapping source;
	const file_mapping destination;
	std::fstream output;

	patch(const char *outputfile, const char *sourcefile, const char *destinationfile) :
		source(file_mapping(sourcefile)), destination(file_mapping(destinationfile)) {
		this->output.open(outputfile, std::ios::out | std::ios::trunc); // throws on error
	}

	~patch() {
		this->output.close();
	}
};

int
main(int argc, char **argv) {
	if(argc == 4) {
		patch patch(argv[3], argv[1], argv[2]);
	} else {
		fprintf(stderr, "usage: %s <source> <destination> <patch>\n", *argv);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

