#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>
#include <sys/queue.h>

#define nullptr NULL

#define EX_TARFAILURE 2
#define BLOCK_SIZE 512

/*
 * POSIX header.
 * (ripped straight from gnu.org) 
 *
 */

const char ustar_magic[] = {'u', 's', 't', 'a', 'r', ' ', ' ', '\0'};

typedef struct posix_header header_object;
struct posix_header {              /* byte offset */
	char name[100];               /*   0 */
	char mode[8];                 /* 100 */
	char uid[8];                  /* 108 */
	char gid[8];                  /* 116 */
	char size[12];                /* 124 */
	char mtime[12];               /* 136 */
	char chksum[8];               /* 148 */
  	char typeflag;                /* 156 */
  	char linkname[100];           /* 157 */
 	char magic[6];                /* 257 */
  	char version[2];              /* 263 */
  	char uname[32];               /* 265 */
  	char gname[32];               /* 297 */
  	char devmajor[8];             /* 329 */
  	char devminor[8];             /* 337 */
  	char prefix[155];	      /* 345 */
	char padding[12];	      /* 500 */
				      /* 512 */
};

/**
 * File name argument list for the '-t' option.
 *
 */
STAILQ_HEAD(slist_head, list_node) file_list =
	STAILQ_HEAD_INITIALIZER(file_list);

typedef struct list_node literal_entry;
struct list_node {
	char* item;
	STAILQ_ENTRY(list_node) entries;
};

/**
 * Launcher configuration - contains user options and the archive name.
 *
 */
struct config {
	bool file;			// -f
	char* archive_name;
	bool list;			// -t
	bool extract;			// -x
	bool verbose;			// -v
	bool list_is_empty;
};
struct config config_;

/**
 * Check whether user options are valid.
 *
 *
 * @return 'True' if the configuration is valid, 'False' if the program\n 
 * should terminate.
 */
bool
validate_config() {
	if ((config_.file && (config_.archive_name != nullptr)) 
			|| config_.list) {
		return (true);
	} else {
		return (false);
	}
}

/**
 * Sets the members of the config structure to their default values.
 *
 */
void
initialize_config() {
	config_.file = false;
	config_.list = false;
	config_.extract = false;
	config_.verbose = false;
	config_.list_is_empty = true;
	config_.archive_name = nullptr;
}

/**
 * Deallocates all globally used memory.
 *
 */
void
free_resources() {
	if (config_.archive_name != nullptr) {
		free(config_.archive_name);
	}

	literal_entry* entry = STAILQ_FIRST(&file_list);
	literal_entry* temp;
	
	while (entry != nullptr) {
		temp = STAILQ_NEXT(entry, entries);
		
		STAILQ_REMOVE(&file_list, entry, list_node, entries);
		free(entry->item);
		free(entry);

		entry = temp;
	}
}

/**
 * Checks whether a block of zeroes of the size 'block_size' is present from\n
 * the 'offset' forward in file handled by 'fp'.
 *
 * @return 'True' if the block is present, 'False' otherwise.
 */
bool
zero_block_is_present(int offset, int block_size, FILE* fp) {
	if (fseek(fp, offset, SEEK_SET) != 0) {
		errx(EX_IOERR, "Could not seek in the input file.");
	}
	
	int differences = 0;
	char c;

	for (int i = 0; i < block_size; i++) {
		if (fread(&c, sizeof(c), 1, fp) != 1) {
			errx(EX_IOERR, "Could not read the input file.");
		}
		if (c != '\0') {
			differences++;
		}
	}

	if (differences > 0) {
		return (false);
	}

	return (true);
}

/**
 * Traverses to the end of the file and returns its size, then traverses back\n
 * to the beginning.
 *
 * @return The size of the file with the descriptor 'fp'.
 */
int
get_archive_size(FILE* fp) {
	fseek(fp, 0, SEEK_END);
	int total = ftell(fp);
	
	if (total == -1) {
		errx(EX_IOERR, "Could not seek in the input file.");
	}

	rewind(fp);

	return (total);
}

/**
 * Checks whether the argument is the same as the ustar magic byte sequence,\n
 * upon failure terminates the program.
 */
void
check_magic(const char* magic) {
	if (strcmp(magic, ustar_magic)) {
                free_resources();
		warnx("This does not look like a tar archive");
		errx(EX_TARFAILURE,
			"Exiting with failure status due to previous errors");
        }
}

/**
 * Checks whether the argument is the same as the standard file flag\n
 * (i.e '0'), upon failure terminates the program.
 */
void
check_typeflag(char typeflag) {
	if (typeflag != '0') {
                free_resources();
                errx(EX_TARFAILURE,
                        "Unsupported header type: %d", typeflag);
        }
}

/**
 * Traverses the optional file list and checks for the argument in that list.\n
 * If no list was provided by the user, then the argument should be processed.
 *
 * @return 'True' if the file should be processed further, otherwise 'False'.
 */
bool
process_file(const char* file_name) {
	bool process_file = false;

	if (STAILQ_EMPTY(&file_list)) {
		process_file = true;
	} else {
		literal_entry* entry;
		literal_entry* to_be_removed = nullptr;

		// Search the file list for the current file,
		// if found, delete it from the list - this
		// will mark it as found.
		STAILQ_FOREACH(entry, &file_list, entries) {
			if (!strcmp(entry->item, file_name)) {
				process_file = true;
				to_be_removed = entry;
			}
		}
		
		if (to_be_removed != nullptr) {
			STAILQ_REMOVE(&file_list, to_be_removed,
						list_node, entries);
			free(to_be_removed->item);
			free(to_be_removed);
		}
	}

	return (process_file);
}

/**
 * Checks the files that have not been processed before and prints a warning\n
 * message if any of those file are present. Prints an error message if\n
 * the archive is incomplete.
 */
void
finish_processing(FILE* fp, int total) {
	bool failure = false;

	literal_entry* entry = STAILQ_FIRST(&file_list);
	literal_entry* temp;
        
	// Traverse the remainder of the file list and print a warning
	// message. Delete the node while we are at it. Set a failure
	// flag to terminate later.
	while (entry != nullptr) {
		failure = true;
                warnx("%s: Not found in archive", entry->item);
                
		temp = STAILQ_NEXT(entry, entries);
		STAILQ_REMOVE(&file_list, entry, list_node, entries);
		free(entry->item);
		free(entry);
		entry = temp;
        }

	bool first_block = zero_block_is_present(total - 2 * BLOCK_SIZE, 
			BLOCK_SIZE, fp);
	bool second_block = zero_block_is_present(total - BLOCK_SIZE, 
			BLOCK_SIZE, fp);

	if ((!first_block) && (first_block + second_block > 0)) {
		warnx("A lone zero block at %d", total / BLOCK_SIZE);
	}

	if (failure) {
		free_resources();
		errx(EX_TARFAILURE, 
		"Exiting with failure status due to previous errors");
	}
}

/**
 * Reads the given file descriptor and dumps its containts into a new file.\n
 * The output is terminated once the input is at the end of the file.
 */
void
dump_until_end(FILE* fp, const char* file_name) {
	FILE *fpout;
        if ((fpout = fopen(file_name, "w+")) == nullptr) {
                warnx("File could not be created");
		fclose(fpout);
		return;
        }

	char buffer;

        while (fread(&buffer, sizeof(buffer), 1, fp) == 1) {
                if (fwrite(&buffer, sizeof(buffer), 1, fpout) 
				!= sizeof(buffer)) {
                        errx(EX_IOERR, "Could not write to the output file.");
                }
        }

        fclose(fpout);
}

/**
 * Checks whether the next header is out of scope, i.e. the file is trucated.\n
 * If the archive is trucated and '-x' is active, it extracts the incomplete\n 
 * file fragment.
 */
void
check_eof(int total, int file_size_with_padding, FILE* fp, const char* name) {
        int pos = ftell(fp);

	if (pos == -1) {
		errx(EX_IOERR, "Could not seek in the input file.");
	}

        // If the next seek is out of range, the archive is truncated.
        if (total - pos < file_size_with_padding) {
                warnx("Unexpected EOF in archive");

                if (config_.extract) {
                        dump_until_end(fp, name);
                }

                free_resources();
                errx(EX_TARFAILURE,
                        "Error is not recoverable: exiting now");
        }
}

/**
 * Traverses the archive given by config and looks for files in the file_list.
 * If the file_list is empty, therefore no files were specified, it lists\n
 * or extracts all files in the archive.
 *
 * @return 0 if the listing or extracting is successful,\n
 * 2 if an error is encountered.
 */
int
process_archive(FILE* fp) {
	int total = get_archive_size(fp);

	header_object h;

	while (fread(&h, sizeof(header_object), 1, fp) 
			== 1) {
		if (h.name[0] == '\0') {
			break;
		}

		check_magic(h.magic);
		check_typeflag(h.typeflag);

		bool extract_or_list_file = process_file(h.name);

		int file_size = strtol(h.size, nullptr, 8);
		int file_size_with_padding = file_size;

		if (file_size % BLOCK_SIZE > 0) {
			file_size_with_padding = file_size + BLOCK_SIZE - 
				(file_size % BLOCK_SIZE);
		}

		if (extract_or_list_file == false) {
                        if (fseek(fp, file_size_with_padding, SEEK_CUR) != 0) {
                		errx(EX_IOERR, 
					"Could not seek in the input file.");
		        }

                        continue;
                }

		if (config_.verbose || config_.list) {
                        fprintf(stdout, "%s\n", h.name);
                        fflush(stdout);
                }

		check_eof(total, file_size_with_padding, fp, h.name);

		if (config_.extract) {
			FILE *fpout;

			if ((fpout = fopen(h.name, "w")) == nullptr) {
				warnx("File could not be created");
				
				if (fseek(fp, file_size_with_padding, 
							SEEK_CUR) != 0) {
               		 		errx(EX_IOERR,
					"Could not seek in the input file.");
        			}

				continue;
			}

			char buffer;
		
			for (int i = 0; i < file_size; i++) {
				if (fread(&buffer, sizeof(buffer), 1, fp)
						!= 1) {
                        		errx(EX_IOERR, 
					"Could not read the input file.");
                		}

				if (fwrite(&buffer, sizeof(buffer), 1, fpout)
						!= 1) {
                        		errx(EX_IOERR, 
					"Could not write to the output file.");
                		}
			}

			fclose(fpout);

			if (fseek(fp, file_size_with_padding - file_size,
					SEEK_CUR) != 0) {
                		errx(EX_IOERR, 
					"Could not seek in the input file.");
        		}
		} else {
			if (fseek(fp, file_size_with_padding, SEEK_CUR) != 0) {
				errx(EX_IOERR,
                                        "Could not seek in the input file.");
			}
		}
	}

	finish_processing(fp, total);
	
	return (0);
}

/**
 * Entrypoint of the application after the input has been parsed.
 * Launches the function chosen by the user via input options.
 *
 * @return The return value of the launched function.
 */
int
launcher() {

#ifdef DEBUG
        printf("Running the launcher\n");
#endif

	FILE* fp;

	if (config_.file == true) {
		if ((fp = fopen(config_.archive_name, "r")) == nullptr) {
			free_resources();
			errx(EX_TARFAILURE, "Archive could not be opened");
		}
	} else {
		free_resources();
		errx(EX_TARFAILURE, "Not implemented");
	}

	int ret_val = process_archive(fp);

	fclose(fp);
	free_resources();

	return (ret_val);
}

int
main(int argc, char** argv) {
	initialize_config();

	for (int i = 1; i < argc; i++) {
		if (!strcmp(i[argv], "-f")) {
			config_.file = true;
			
			i++;
			assert(i < argc);

			config_.archive_name = strdup(argv[i]);
                        assert(config_.archive_name != nullptr);

			continue;
		}

		if (!strcmp(argv[i], "-t")) {
			config_.list = true;
			continue;
		}

		if (!strcmp(argv[i], "-x")) {
			config_.extract = true;
			continue;
		}

		if (!strcmp(argv[i], "-v")) {
			config_.verbose = true;
			continue;
                }

		if (argv[i][0] == '-') {
			free_resources();
			errx(EX_TARFAILURE, "Unknown option: %s", argv[i]);
		}

		literal_entry* entry = malloc(sizeof(literal_entry));
		assert(entry != nullptr);

		entry->item = strdup(argv[i]);
		assert(entry->item != nullptr);
		
		if (STAILQ_EMPTY(&file_list)) {
			 STAILQ_INSERT_HEAD(&file_list, entry, entries);
		} else {
			STAILQ_INSERT_TAIL(&file_list, entry, entries);
		}

		config_.list_is_empty = false;
	}

	if (validate_config()) {
		return (launcher());
	}

	free_resources();
	errx(EX_TARFAILURE, "Need at least one option");

	// unreachable

	return (0);
}
