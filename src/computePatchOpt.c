#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>

typedef unsigned long cost_t;

struct file_mapping {
	const char * const begin;
	const char * const end;
	size_t lines;
};

struct patch_cost {
	cost_t cost;
};

static size_t
line_length(const char *line, const char *end) {
	const char * const next = memchr(line, '\n', end - line);

	return (next != NULL ? next : end) - line;
}

static struct file_mapping
file_mapping_create(const char *file) {
	struct stat st;
	void *mmaped;
	int fd;

	if((fd = open(file, O_RDONLY)) == -1
		|| fstat(fd, &st) == -1
		|| (mmaped = mmap(NULL, st.st_size,
			PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		err(EXIT_FAILURE, "file_mapping_create %s", file);
	}

	close(fd);

	struct file_mapping mapping = {
		.begin = mmaped,
		.end = (const char *)mmaped + st.st_size,
		.lines = 0
	};

	const char *line = mapping.begin;
	while(line < mapping.end) {
		mapping.lines++;
		line += line_length(line, mapping.end) + 1;
	}

	return mapping;
}

static FILE *
patch_fopen(const char *file) {
	FILE *output = fopen(file, "w");

	if(output == NULL) {
		err(EXIT_FAILURE, "fopen %s", file);
	}

	return output;
}

static struct patch_cost *
patch_costs_empty_source(struct patch_cost *costs,
	const struct file_mapping *destination) {
	const char *line = destination->begin;
	size_t costsum = 0;

	while(line < destination->end) {
		const size_t length = line_length(line, destination->end);

		costsum += 10 + length;
		costs->cost = costsum;
		costs++;

		line += length + 1;
	}

	return costs;
}

static const struct patch_cost *
patch_costs(const struct file_mapping *source,
	const struct file_mapping *destination) {
	struct patch_cost *costs = malloc(destination->lines * (source->lines + 1) * sizeof(*costs));
	struct patch_cost *iterator = patch_costs_empty_source(costs, destination);
	const char *line = source->begin;
	size_t i = 1;

	do {
		const size_t length = line_length(line, source->end);

		i++;
		line += length + 1;
	} while(line < source->end);

	return costs;
}

static void
patch_print_case_empty_source(FILE *patch, const struct file_mapping *destination) {
	const char *line = destination->begin;

	while(line < destination->end) {
		const size_t length = line_length(line, destination->end);
		fputs("+ 0\n", patch);
		fwrite(line, length, 1, patch);
		fputc('\n', patch);
		line += length + 1;
	}
}

static void
patch_print_case_empty_destination(FILE *patch, const struct file_mapping *source) {
	const char *line = source->begin;
	size_t i = 1;

	while(line < source->end) {
		const size_t length = line_length(line, source->end);
		fprintf(patch, "- %zu\n", i);
		fwrite(line, length, 1, patch);
		fputc('\n', patch);
		line += length + 1;
	}
}

int
main(int argc, char **argv) {
	if(argc == 4) {
		struct file_mapping source = file_mapping_create(argv[1]);
		struct file_mapping destination = file_mapping_create(argv[2]);
		FILE *patch = patch_fopen(argv[3]);

		if(destination.lines != 0) {
			if(source.lines != 0) {
				const struct patch_cost *costs = patch_costs(&source, &destination);
			} else {
				patch_print_case_empty_source(patch, &destination);
			}
		} else {
			patch_print_case_empty_destination(patch, &source);
		}
	} else {
		fprintf(stderr, "usage: %s <source> <destination> <patch>\n", *argv);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

