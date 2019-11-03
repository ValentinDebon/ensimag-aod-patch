#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>

#define COST_CONSTANT 10

typedef unsigned long cost_t;

struct file_mapping {
	const char * const begin;
	const char * const end;
	size_t lines;
};

struct patch {
	struct patch *next;
	size_t i;
	size_t j;
	enum patch_action {
		PATCH_ACTION_NONE,
		PATCH_ACTION_ADD,
		PATCH_ACTION_REMOVE,
		PATCH_ACTION_REPLACE
	} action;
};

static struct patch *
patch_create(size_t i, size_t j, enum patch_action action, struct patch *next) {
	struct patch *patch = malloc(sizeof(*patch));

	patch->next = next;
	patch->i = i;
	patch->j = j;
	patch->action = action;

	return patch;
}

static inline cost_t
min(cost_t a, cost_t b) {
	return a < b ? a : b;
}

static inline size_t
line_length(const char *line, const char *end) {
	const char * const next = memchr(line, '\n', end - line);

	return (next != NULL ? next : end) - line;
}

static inline bool
line_equals(const char *line1, size_t length1,
	const char *line2, size_t length2) {
	return length1 == length2 && memcmp(line1, line2, length1) == 0;
}

static inline void
line_reach(const char **linep, size_t *lengthp, size_t *nolinep,
	size_t limit, const char *end) {
	const char *line = *linep;
	size_t length = *lengthp;
	size_t noline = *nolinep;

	while(line < end && noline < limit) {
		line += length + 1;
		length = line_length(line, end);
		noline++;
	}

	*linep = line;
	*lengthp = length;
	*nolinep = noline;
}

static struct file_mapping
file_mapping_create(const char *file) {
	struct stat st;
	void *mmaped;
	int fd;

	if((fd = open(file, O_RDONLY)) == -1
		|| fstat(fd, &st) == -1
		|| (mmaped = mmap(NULL, st.st_size,
			PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
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

		costsum += COST_CONSTANT + length;
		*costs = costsum;
		costs++;

		line += length + 1;
	}

	return costs;
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
		*iterator = min(min(iterator[-destination->lines] + COST_CONSTANT, COST_CONSTANT * i + *costs),
			(line_equals(line, length, destination->begin, *costs - COST_CONSTANT) ? 0 : *costs) + COST_CONSTANT * (i - 1));
		iterator++;

		for(size_t j = 1; j < destination->lines; j++) {
			const size_t costb = costs[j] - costs[j - 1];
			const cost_t append = iterator[-1] + costb;
			const cost_t removal = iterator[-destination->lines] + COST_CONSTANT;
			cost_t substitution = iterator[-destination->lines - 1];

			if(!line_equals(line, length, destination->begin + (costs[j - 1] - (COST_CONSTANT - 1) * j), costb - COST_CONSTANT)) {
				substitution += costb;
			}

			*iterator = min(min(append, removal), substitution);
			iterator++;
		}

		i++;
		line += length + 1;
	} while(line < source->end);

	return costs;
}

#ifdef PATCH_COSTS_PRINT
static void
patch_costs_print(FILE *output, const cost_t *costs, size_t m, size_t n) {

	fputs(" i\\j ", output);
	for(size_t j = 0; j <= n; j++) {
		fprintf(output, "%4zu ", j);
	}
	fputc('\n', output);

	for(size_t i = 0; i <= m; i++) {
		fprintf(output, "%4zu %4zu ", i, i * COST_CONSTANT);
		for(size_t j = 0; j < n; j++) {
			fprintf(output, "%4zu ", costs[i * n + j]);
		}
		fputc('\n', output);
	}
}
#endif

static struct patch *
patch_compute(const cost_t *costs,
	const struct file_mapping *source, const struct file_mapping *destination) {
	struct patch *patches = NULL;
	size_t i = source->lines;
	size_t j = destination->lines - 1;

	while(i != 0 && j != 0) {
		const cost_t *current = costs + i * destination->lines + j;
		const cost_t cost = *current;

		if(cost == *(current -= destination->lines + 1)) {
			patches = patch_create(i, j, PATCH_ACTION_NONE, patches);
			i--;
			j--;
		} else if(cost == *++current + COST_CONSTANT) {
			patches = patch_create(i, j, PATCH_ACTION_REMOVE, patches);
			i--;
		} else if(cost == *(current += destination->lines - 1) + costs[j] - costs[j - 1]) {
			patches = patch_create(i, j, PATCH_ACTION_ADD, patches);
			j--;
		} else {
			patches = patch_create(i, j, PATCH_ACTION_REPLACE, patches);
			i--;
			j--;
		}
	}

	if(i == 0) {
		while(j + 1 > 0) {
			patches = patch_create(i, j, PATCH_ACTION_ADD, patches);
			j--;
		}
	} else {
		while(i > 0) {
			patches = patch_create(i, j, PATCH_ACTION_REMOVE, patches);
			i--;
		}
	}

	return patches;
}

static void
patch_print(FILE *patch, struct patch *patches,
	const struct file_mapping *source, const struct file_mapping *destination) {
	const char *linea = source->begin;
	const char *lineb = destination->begin;
	size_t nolinea = 1, lengtha = line_length(linea, source->end);
	size_t nolineb = 0, lengthb = line_length(lineb, destination->end);

	while(patches != NULL) {
		switch(patches->action) {
		case PATCH_ACTION_ADD:
			// printf("destination line %zu is added after source line %zu\n", patches->j + 1, patches->i);
			line_reach(&lineb, &lengthb, &nolineb, patches->j, destination->end);
			fprintf(patch, "+ %zu\n", patches->i);
			fwrite(lineb, lengthb, 1, patch);
			fputc('\n', patch);
			break;
		case PATCH_ACTION_REMOVE:
			// printf("source line %zu is removed\n", patches->i);
			line_reach(&linea, &lengtha, &nolinea, patches->i, source->end);
			fprintf(patch, "- %zu\n", patches->i);
			fwrite(linea, lengtha, 1, patch);
			fputc('\n', patch);
			break;
		case PATCH_ACTION_REPLACE:
			// printf("destination line %zu replaces source line %zu\n", patches->j + 1, patches->i);
			line_reach(&linea, &lengtha, &nolinea, patches->i, source->end);
			line_reach(&lineb, &lengthb, &nolineb, patches->j, destination->end);
			fprintf(patch, "= %zu\n", patches->i);
			fwrite(linea, lengtha, 1, patch);
			fputc('\n', patch);
			fwrite(lineb, lengthb, 1, patch);
			fputc('\n', patch);
			break;
		default: /* PATCH_ACTION_NONE */
			// printf("source line %zu is identical to destination line %zu\n", patches->i, patches->j + 1);
			break;
		}

		patches = patches->next;
	}
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
		const struct file_mapping source = file_mapping_create(argv[1]);
		const struct file_mapping destination = file_mapping_create(argv[2]);
		FILE *patch = patch_fopen(argv[3]);

		if(destination.lines != 0) {
			if(source.lines != 0) {
				const cost_t *costs = patch_costs(&source, &destination);
				struct patch *patches = patch_compute(costs, &source, &destination);

				patch_print(patch, patches, &source, &destination);
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

