#include <stdexcept>
#include <fstream>
#include <string>
#include <experimental/string_view>

#include <iostream>

extern "C" {
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
}

typedef unsigned long cost_t;

class file_mapping final {
	char *file_begin;
	char *file_end;

public:
	class iterator final {
		typedef std::experimental::string_view string_view;
		friend class file_mapping;
		const char * const end;
		string_view line;

		static size_t line_length(const char *line, const char *end) {
			const char * const next = static_cast<char *>(memchr(line, '\n', end - line));
			return (next != NULL ? next : end) - line;
		}

		iterator(const char *end) : end(end), line(end, 0) { }
	public:
		iterator &operator++() {
			const char * const next = this->line.data() + this->line.length() + 1;
			this->line = string_view(next, line_length(next, end));
			return *this;
		}
		const string_view &operator*() const { return this->line; }
		bool operator!=(iterator &operand) { return this->line != operand.line; }
	};

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

		this->file_begin = static_cast<char *>(mmaped);
		this->file_end = this->file_begin + st.st_size;

		close(fd);
	}

	iterator begin() const {
		iterator it(this->file_end);
		it.line = iterator::string_view(this->file_begin,
			iterator::line_length(this->file_begin, this->file_end));
		return it;
	}
	const iterator end() const { return iterator(this->file_end); }

	~file_mapping() {
		munmap(this->file_begin, this->file_end - this->file_begin);
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

		size_t j = 1;
		for(auto lineb : patch.destination) {
			size_t i = 1;
			for(auto linea : patch.source) {
				if(linea == lineb) {
					patch.output << "Line " + std::to_string(i) + " of source is equal to line " + std::to_string(j) + " of destination\n";
				}
				i++;
			}
			j++;
		}
	} else {
		fprintf(stderr, "usage: %s <source> <destination> <patch>\n", *argv);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

