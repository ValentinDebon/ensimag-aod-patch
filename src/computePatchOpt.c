#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

static cost_t *
patch_costs_empty_source(cost_t *costs,
	const struct file_mapping *destination) {
	const char *line = destination->begin;
	size_t costsum = 0;

	while(line < destination->end) {
		const size_t length = line_length(line, destination->end);

		costsum += 10 + length;
		*costs = costsum;
		costs++;

		line += length + 1;
	}

	return costs;
}

static inline cost_t
min(cost_t a, cost_t b) {
	return a < b ? a : b;
}

static bool
line_equals(const char *line1, size_t length1,
	const char *line2, size_t length2) {
	fputs("comparing: \"", stdout);
	fwrite(line1, length1, 1, stdout);
	fputs("\" to \"", stdout);
	fwrite(line2, length2, 1, stdout);
	fputs("\"\n", stdout);
	return length1 == length2 && memcmp(line1, line2, length1) == 0;
}

static const cost_t *
patch_costs(const struct file_mapping *source,
	const struct file_mapping *destination) {
	cost_t *costs = malloc(destination->lines * (source->lines + 1) * sizeof(*costs));
	cost_t *iterator = patch_costs_empty_source(costs, destination);
	const char *line = source->begin;
	size_t i = 1;

	do {
		const size_t length = line_length(line, source->end);
		*iterator = min(min(iterator[-destination->lines] + 10, 10 * i + *costs),
			(line_equals(line, length, destination->begin, *costs - 10) ? 0 : *costs) + 10 * (i - 1));
		iterator++;

		for(size_t j = 1; j < destination->lines; j++) {
			const size_t costb = costs[j] - costs[j - 1];
			const cost_t append = iterator[-1] + costb;
			const cost_t removal = iterator[-destination->lines] + 10;
			cost_t substitution = iterator[-destination->lines - 1];

			if(!line_equals(line, length, destination->begin + (costs[j - 1] - (10 - 1) * j), costb - 10)) {
				substitution += costb;
			}

			printf("(%zu, %zu): min(%zu, %zu, %zu) (Lb%zu: %zu)\n", i, j + 1, append, removal, substitution, j + 1, costb);

			*iterator = min(min(append, removal), substitution);
			iterator++;
		}

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

static void
patch_costs_print(FILE *output, const cost_t *costs, size_t m, size_t n) {

	fputs(" i\\j ", output);
	for(size_t j = 0; j <= n; j++) {
		fprintf(output, "%4zu ", j);
	}
	fputc('\n', output);

	for(size_t i = 0; i <= m; i++) {
		fprintf(output, "%4zu %4zu ", i, i * 10);
		for(size_t j = 0; j < n; j++) {
			fprintf(output, "%4zu ", costs[i * n + j]);
		}
		fputc('\n', output);
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
				const cost_t *costs = patch_costs(&source, &destination);

				patch_costs_print(stdout, costs, source.lines, destination.lines);
				//patch_print(patch, costs, source.lines, destination.lines);
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

