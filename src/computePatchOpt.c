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

struct patch_costs {
	cost_t *table;
	size_t capacity;
	size_t row;
};

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

	return (struct file_mapping) {
		.begin = mmaped,
		.end = (const char *)mmaped + st.st_size,
		.lines = 0
	};
}

static size_t
line_length(const char *line, const char *end) {
	const char * const next = memchr(line, '\n', end - line);

	return (next != NULL ? next : end) - line;
}

static cost_t
patch_costs_get(const struct patch_costs *costs, size_t i, size_t j) {

	if(j == 0) {
		return i * 10;
	} else {
		return costs->table[i * costs->row + j - 1];
	}
}

static cost_t
patch_costs_set(struct patch_costs *costs, size_t i, size_t j, cost_t cost) {

}

static const patch_costs
patch_costs_compute(struct file_mapping *source,
	struct file_mapping *destination) {
	struct patch_costs costs = { .table = NULL, .capacity = 0, .row = 0 };
	const char *dline = destination->begin;
	size_t i, j = 0;

	while(dline < destination->end) {
		const size_t dlength = line_length(dline, destination->end);
		const char *sline = source->begin;
		i = 0;
		j++;

		while(sline < source->end) {
			const size_t slength = line_length(sline, source->end);

			i++;
			sline += slength + 1;
		}

		dline += dlength + 1;
	}

	/* TODO: handle if destination 0 */

	source->lines = i;
	destination->lines = j;
	printf("%zu %zu\n", source->lines, destination->lines);

	return costs;
}

static FILE *
patch_fopen(const char *file) {
	FILE *output = fopen(file, "w");

	if(output == NULL) {
		err(EXIT_FAILURE, "fopen %s", file);
	}

	return output;
}

static void
patch_compute(FILE *patch, const struct patch_costs *costs,
	size_t sourcelines, size_t destinationlines) {
}

int
main(int argc, char **argv) {
	if(argc == 4) {
		struct file_mapping source = file_mapping_create(argv[1]);
		struct file_mapping destination = file_mapping_create(argv[2]);
		const struct patch_costs costs = patch_costs_compute(&source, &destination);
		FILE *patch = patch_fopen(argv[3]);

		patch_compute(patch, &costs, source.lines, destination.lines);

	} else {
		fprintf(stderr, "usage: %s <source> <destination> <patch>\n", *argv);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

