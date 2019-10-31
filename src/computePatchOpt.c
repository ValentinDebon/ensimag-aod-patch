#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>

struct file_mapping {
	const char * const begin;
	const char * const end;
	const char *current;
	size_t length;
};

static size_t
line_length(const char *line, const char *end) {
	const char *lineend = memchr(line, '\n', end - line);

	return (lineend != NULL ? lineend : end) - line;
}

static struct file_mapping
file_mapping_create(const char *file) {
	const char *begin, *end;
	struct stat st;
	int fd;

	if((fd = open(file, O_RDONLY)) == -1
		|| fstat(fd, &st) == -1
		|| (begin = mmap(NULL, st.st_size,
			PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		err(EXIT_FAILURE, "file_mapping_create '%s'", file);
	}

	close(fd);

	end = begin + st.st_size;

	return (struct file_mapping) {
		.begin = begin,
		.end = end,
		.current = begin,
		.length = line_length(begin, end)
	};
}

static inline FILE *
patch_fopen(const char *file) {
	FILE *patch = fopen(file, "w");

	if(patch == NULL) {
		err(EXIT_FAILURE, "fopen '%s'", file);
	}

	return patch;
}

static void
patch_compute_at(FILE *patch, struct file_mapping *source,
	struct file_mapping *destination, size_t index) {
	if(source->current < source->end) {
		fprintf(patch, "%zu (%zu): ", index, source->length);
		fwrite(source->current, source->length, 1, patch);
		fputc('\n', patch);

		source->current += source->length + 1;
		source->length = line_length(source->current, source->end);

		patch_compute_at(patch, source, destination, index + 1);
	}
}

int
main(int argc, char **argv) {

	if(argc == 4) {
		struct file_mapping source = file_mapping_create(argv[1]),
			destination = file_mapping_create(argv[2]);
		FILE *patch = patch_fopen(argv[3]);

		patch_compute_at(patch, &source, &destination, 0);
	} else {
		fprintf(stderr, "usage: %s <source> <destination> <patch>\n", *argv);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

