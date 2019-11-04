#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>

/**
 * Macro factorisant dans le code le cout de suppression de l'équation de Bellman
 */
#define COST_CONSTANT 10

typedef unsigned int cost_t; /**< Type stockant les couts */

/**
 * Structure représentant un fichier en mémoire sous forme de tableau
 */
struct file_mapping {
	const char * const begin; /**< Début du fichier, pointeur vers le premier octet */
	const char * const end;   /**< Fin du fichier, pointeur suivant le dernier octet valide du fichier */
	size_t lines;             /**< Nombre de lignes du fichier */
};

/**
 * Structure représentant une action de patch et quelles lignes il concerne
 */
struct patch {
	struct patch *next;      /**< Prochain patch, ou NULL si fin de liste */
	size_t i;                /**< Ligne de la source concernée */
	size_t j;                /**< Ligne de la destination concernée */
	enum patch_action {
		PATCH_ACTION_NONE,   /**< Ne rien faire, les lignes sont identiques */
		PATCH_ACTION_ADD,    /**< Ajouter la ligne j de la destination après la ligne i de la source */
		PATCH_ACTION_REMOVE, /**< Supprimer la ligne i de la source */
		PATCH_ACTION_REPLACE /**< Remplacer la ligne i de la source par la ligne j de la destination */
	} action;
};

/**
 * Version de malloc(3) qui échoue en cas d'erreur
 * @param n Nombre d'octets à allouer
 * @return pointeur vers n octets, ou ne retourne pas
 */
static inline void *
xmalloc(size_t n) {
	void *ptr = malloc(n);

	if(ptr == NULL) {
		err(EXIT_FAILURE, "malloc %zu", n);
	}

	return ptr;
}

/**
 * Fonction d'aide à la création en série des patchs
 */
static struct patch *
patch_create(size_t i, size_t j, enum patch_action action, struct patch *next) {
	struct patch *patch = xmalloc(sizeof(*patch));

	patch->next = next;
	patch->i = i;
	patch->j = j;
	patch->action = action;

	return patch;
}

/**
 * Minimum de deux couts
 */
static inline cost_t
min(cost_t a, cost_t b) {
	return a < b ? a : b;
}

/**
 * Taille d'une ligne
 * @param line Pointeur du début de ligne
 * @param end Fin de validité du buffer de la ligne, supérieur ou égal à @line
 * @return la taille de la ligne, ou 0 si la fin a été atteinte
 */
static inline size_t
line_length(const char *line, const char *end) {
	const char * const next = memchr(line, '\n', end - line);

	return (next != NULL ? next : end) - line;
}

/**
 * Comparaison de deux lignes
 * @param line1 Première ligne
 * @param length1 Taille de la première ligne
 * @param line2 Deuxième ligne
 * @param length2 Taille de la deuxième ligne
 * @return true si égales, false sinon
 */
static inline bool
line_equals(const char *line1, size_t length1,
	const char *line2, size_t length2) {
	return length1 == length2 && memcmp(line1, line2, length1) == 0;
}

/**
 * Fonction d'aide afin d'atteindre la ligne limit
 * @param linep Pointeur vers la ligne (variable en entrée et de retour)
 * @param lengthp Longueur de la ligne (variable en entrée et de retour)
 * @param nolinep Numéro de la ligne (variable en entrée et de retour)
 * @param limit Numéro de ligne à atteindre, si on est déjà au delà on ne fait rien
 * @param end Fin du buffer de la ligne
 */
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

/**
 * Fonction mappant un fichier en mémoire
 * @param file Chemin du fichier
 * @return Un mapping du fichier, ou ne retourne pas
 */
static struct file_mapping
file_mapping_create(const char *file) {
	struct stat st;

	if(stat(file, &st) == -1) {
		err(EXIT_FAILURE, "file_mapping_create stat %s", file);
	}

	if(st.st_size != 0) { /* mmap(2) échoue si on lui fournit une taille de 0 */
		int fd = open(file, O_RDONLY);
		void *mmaped;

		if(fd == -1 || (mmaped = mmap(NULL, st.st_size,
			PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
			err(EXIT_FAILURE, "file_mapping_create open/mmap %s", file);
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
	} else {
		return (struct file_mapping) {
			.begin = NULL,
			.end = NULL,
			.lines = 0
		};
	}
}

/**
 * Version de fopen(3) qui échoue en cas d'erreur
 * @param file Chemin du fichier
 * @return le fichier ouvert en écriture, ou ne retourne pas
 */
static FILE *
patch_fopen(const char *file) {
	FILE *output = fopen(file, "w");

	if(output == NULL) {
		err(EXIT_FAILURE, "fopen %s", file);
	}

	return output;
}

/**
 * Calcul la première ligne de la matrice de couts
 * La première ligne correspond à tous les (0, j), soit la somme des couts
 * d'addition des lignes de la destination. Afin d'indexer rapidement ceux ci
 * éviter un cas de bordure (edge case?), elle est calculée avant les autres
 * @param costs Début de la première ligne de la matrice de couts
 * @param destination Mapping du fichier de destination
 * @return Début de la seconde ligne de la matrice de couts
 */
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

/**
 * Calcul de la matrice de couts
 * Sa représentation en mémoire est effectuée sur un espace continue de
 * taille (lignes de la source + 1) * lignes de la destination chaque ligne
 * correspond à la ligne (i, 1). En effet, la colonne (i, 0) n'existe par car
 * peut être calculée en cout constant (i * #COST_CONSTANT) et évite ainsi une
 * place mémoire supplémentaire au dépit de plus de cas de bordures dans le code.
 * @param source Mapping du fichier source
 * @param destination Mapping du fichier destination
 * @return Un tableau de taille (lignes de la source + 1) * lignes de la destination des couts
 */
static const cost_t *
patch_costs(const struct file_mapping *source,
	const struct file_mapping *destination) {
	cost_t *costs = xmalloc(destination->lines * (source->lines + 1) * sizeof(*costs));
	cost_t *iterator = patch_costs_empty_source(costs, destination); /* Première ligne calculée */
	const char *line = source->begin;
	size_t i = 1;

	/* Note sur la première ligne (0, j): Cette ligne est ici utilisée pour accéder aux couts,
	longueurs et lignes de la destination en O(1). En théorie cela semble être une mauvaise idée
	car on ajoute un potentiel défaut de cache en costs[0 ... destination->lines - 1] à chaque itération,
	ainsi que ceux de la comparaison de la ligne. Cependant la comparaison de la ligne compare
	d'abord les tailles avant d'accéder à la mémoire, et dans immense majorité des cas celles ci diffèrent
	dès le départ. Ainsi, en pratique on génère moins de défauts de caches qu'une itération totale de la destination.
	En effet, une version alternative du code avec itération de la destination doublait la durée du programme sur le benchmark 02.
	*/
	do {
		const size_t length = line_length(line, source->end);

		/* Calcul de (i, 1), car il dépend d'une colonne qui n'existe pas mais dont les éléments sont calculables en O(1) */
		*iterator = min(min(iterator[-destination->lines] + COST_CONSTANT, COST_CONSTANT * i + *costs),
			(line_equals(line, length, destination->begin, *costs - COST_CONSTANT) ? 0 : *costs) + COST_CONSTANT * (i - 1));
		iterator++;

		/* Attention! Ici j = 1 correspond à la ligne 2 de la destination, car la colonne (i, 0) n'existe pas! */
		for(size_t j = 1; j < destination->lines; j++) {
			const size_t costb = costs[j] - costs[j - 1];
			const cost_t append = iterator[-1] + costb;
			const cost_t removal = iterator[-destination->lines] + COST_CONSTANT;
			cost_t substitution = iterator[-destination->lines - 1];

			/* Ci dessous l'accès au pointeur de ligne en O(1), attention aux caractères '\n', d'où le - 1 */
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
			fprintf(output, "%4u ", costs[i * n + j]);
		}
		fputc('\n', output);
	}
}
#endif

/**
 * Remontée de la solution optimale
 * Nb: Cette algorithme aurait pu être fait de façon récursive afin d'éviter l'existence
 * de la struct patch, cependant ne pouvant déterminer efficacement la limite en profondeur de la pile,
 * avoir un algorithme itératif avec une pile semblait être un compromis acceptable.
 * Notons que son nombre de défaut de cache est à la fois négligeable car étant solution optimale
 * et inévitable car l'on doit bien retrouver le chemin optimal.
 * @costs Matrice de couts
 * @param source Mapping du fichier source
 * @param destination Mapping du fichier destination
 * @return Liste chainée de patches
 */
static struct patch *
patch_compute(const cost_t *costs,
	const struct file_mapping *source, const struct file_mapping *destination) {
	struct patch *patches = NULL;
	size_t i = source->lines;
	size_t j = destination->lines - 1; /* j ne correspond toujours pas aux lignes de la destination */

	while(i != 0 && j != 0) {
		const cost_t *current = costs + i * destination->lines + j;
		const cost_t cost = *current;

		/* On peut en effet remonter avec les couts seulements, sans savoir la
		décision prise auparavant en testant les valeurs précédentes avec le cout courant */
		if(cost == current[-destination->lines] + COST_CONSTANT) {
			patches = patch_create(i, j + 1, PATCH_ACTION_REMOVE, patches);
			i--;
		} else {
			const cost_t costb = costs[j] - costs[j - 1];

			if(cost == current[-1] + costb) {
				patches = patch_create(i, j + 1, PATCH_ACTION_ADD, patches);
				j--;
			} else {
				if(cost == current[-destination->lines - 1]) {
					patches = patch_create(i, j + 1, PATCH_ACTION_NONE, patches);
				} else {
					patches = patch_create(i, j + 1, PATCH_ACTION_REPLACE, patches);
				}
				i--;
				j--;
			}
		}
	}

	/* Ici on remonte la solution jusqu'à (0, 0) depuis (0, j) ou (i, 0) i!=0 et j!=0 */
	j++; /* On devient le numéro de ligne pour simplifier le tout */
	if(i == 0) {
		while(j > 0) {
			patches = patch_create(i, j, PATCH_ACTION_ADD, patches);
			j--;
		}
	} else {
		while(i > 1) {
			patches = patch_create(i, j, PATCH_ACTION_REMOVE, patches);
			i--;
		}
	}

	return patches;
}

/**
 * Écrit le patch à partir des données d'entrée
 * L'utilisation du FILE * de la libc peut sembler étrange,
 * mais la libc bufferise les appels, ce qui permet de gérer simplement ces appels
 * à cout de fprintf.
 * @param patch Fichier ouvert en écriture
 * @param source Mapping du fichier source
 * @param destination Mapping du fichier destination
 */
static void
patch_print(FILE *patch, struct patch *patches,
	const struct file_mapping *source, const struct file_mapping *destination) {
	const char *linea = source->begin;
	const char *lineb = destination->begin;
	size_t nolinea = 1, lengtha = line_length(linea, source->end);
	size_t nolineb = 1, lengthb = line_length(lineb, destination->end);

	while(patches != NULL) {
		switch(patches->action) {
		case PATCH_ACTION_ADD:
			line_reach(&lineb, &lengthb, &nolineb, patches->j, destination->end);
			fprintf(patch, "+ %zu\n", patches->i);
			fwrite(lineb, lengthb, 1, patch);
			fputc('\n', patch);
			break;
		case PATCH_ACTION_REMOVE:
			line_reach(&linea, &lengtha, &nolinea, patches->i, source->end);
			fprintf(patch, "- %zu\n", patches->i);
			fwrite(linea, lengtha, 1, patch);
			fputc('\n', patch);
			break;
		case PATCH_ACTION_REPLACE:
			line_reach(&linea, &lengtha, &nolinea, patches->i, source->end);
			line_reach(&lineb, &lengthb, &nolineb, patches->j, destination->end);
			fprintf(patch, "= %zu\n", patches->i);
			fwrite(linea, lengtha, 1, patch);
			fputc('\n', patch);
			fwrite(lineb, lengthb, 1, patch);
			fputc('\n', patch);
			break;
		default: /* PATCH_ACTION_NONE */
			break;
		}

		patches = patches->next;
	}
}

/**
 * Cas de bordure: la source est vide, ajout total de la destination
 * @param patch Fichier ouvert en écriture
 * @param destination Mapping du fichier destination
 */
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

/**
 * Cas de bordure: la destination est vide, destruction totale de la source
 * @param patch Fichier ouvert en écriture
 * @param source Mapping du fichier source
 */
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

#ifdef PATCH_COSTS_PRINT
				patch_costs_print(stdout, costs, source.lines, destination.lines);
#endif

				/* Par défaut on ne libère pas de mémoire ni rien à la fin,
				c'est relativement inutile et globalement une perte de temps
				comme nous n'avons pas monopolisé de resource système
				particulière hors mémoire vive et potentiel swap */
#ifdef FULL_CLEANUP
				free((void *)costs);
				while(patches != NULL) {
					struct patch *next = patches->next;
					free(patches);
					patches = next;
				}
				fclose(patch);
				munmap((void *)source.begin, source.end - source.begin);
				munmap((void *)destination.begin, destination.end - destination.begin);
#endif
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

