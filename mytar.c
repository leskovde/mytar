#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <sys/queue.h>

#define nullptr NULL

#define EX_TARFAILURE 2
#define BLOCK_SIZE 512

/*
 * POSIX header.
 * (ripped straight from gnu.org) 
 *
 */

const char ustar_magic[] = { 'u', 's', 't', 'a', 'r', ' ', ' ', '\0'};

typedef struct posix_header header_object;
struct posix_header
{                              /* byte offset */
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
struct list_node
{
	char* item;
	STAILQ_ENTRY(list_node) entries;
};

/**
 * Launcher configuration - contains user options and the archive name.
 *
 */
struct config
{
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
validate_config()
{

#ifdef DEBUG
	printf("Validating config\n");
#endif

	if ((config_.file && (config_.archive_name != nullptr)) 
			|| config_.list)
	{
		return (true);
	}
	else
	{
		return (false);
	}
}

/**
 * Sets the members of the config structure to their default values.
 *
 */
void
initialize_config()
{

#ifdef DEBUG
        printf("Initializing config\n");
#endif

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
free_resources()
{

#ifdef DEBUG
        printf("Freeing resources\n");
#endif

	if (config_.archive_name != nullptr)
	{
		free(config_.archive_name);
	}

#ifdef DEBUG
        printf("Freeing file list\n");
#endif

	literal_entry* entry = STAILQ_FIRST(&file_list);
	literal_entry* temp;
	
	while (entry != nullptr)
	{
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
zero_block_is_present(int offset, int block_size, FILE* fp)
{

#ifdef DEBUG
        printf("Checking if zero block is present, offset: %d\n", offset);
#endif

	fseek(fp, offset, SEEK_SET);
	
	int differences = 0;
	char c;

	for (int i = 0; i < block_size; i++)
	{
		fread(&c, sizeof(c), 1, fp);

		if (c != '\0')
		{
			differences++;
		}
	}

#ifdef DEBUG
	printf("Differences: %d\n", differences);
#endif

	if (differences > 0)
	{
		return (false);
	}

	return (true);
}

/**
 * Converts an octal integer (NOT in the octal format, i.e.\n
 * the argument does not start with a zero) to decimal.
 *
 */
int
octal_to_decimal(int octal)
{
	int decimal = 0;
	int base = 1;

	while (octal)
	{
		int last_digit = octal % 10;
		octal = octal / 10;
		decimal += last_digit * base;
		base = base * 8;
	}

	return (decimal);
}

void
read_until_end(FILE* fp, const char* file_name)
{
	FILE *fpout;
        if ((fpout = fopen(file_name, "w+")) == nullptr)
        {
                warnx("File could not be created");
		fclose(fpout);
		return;
        }

	char buffer;

        while (fread(&buffer, sizeof(buffer), 1, fp) == sizeof(buffer))
        {
                fwrite(&buffer, sizeof(buffer), 1, fpout);
        }

        fclose(fpout);
}

/**
 * Traverses the archive given by config and looks for files in the file_list.
 * If the file_list is empty, therefore no files were specified, it lists\n
 * all files in the archive.
 *
 * @return 0 if the listing is successful, 2 if an error is encountered.
 */
int
list_files(FILE* fp)
{

#ifdef DEBUG
        printf("Listing files\n");
#endif

	fseek(fp, 0, SEEK_END);
	int total = ftell(fp);
	rewind(fp);

#ifdef DEBUG
        printf("Archive size: %d\n", total);
#endif

	header_object h;

	while (fread(&h, sizeof(header_object), 1, fp))
	{
		if (!strlen(h.name))
		{
			break;
		}

#ifdef DEBUG
	        printf("Type flag: %c, decimal: %d\n", h.typeflag, h.typeflag);
#endif

		if (strcmp(h.magic, ustar_magic))
                {
                        free_resources();
                        
			warnx("This does not look like a tar archive");
			
			errx(EX_TARFAILURE,
			"Exiting with failure status due to previous errors");
                }

		
		if (h.typeflag != '0')
                {
                        // block number should be added to the err msg
                        free_resources();
                        errx(EX_TARFAILURE,
                                "Unsupported header type\n");
                }
		
#ifdef DEBUG
        	printf("Current file: %s\n", h.name);
#endif

		if (STAILQ_EMPTY(&file_list))
		{
			fprintf(stdout, "%s\n", h.name);
			fflush(stdout);
		}
		else
		{
			literal_entry* entry;
			literal_entry* to_be_removed = nullptr;

#ifdef DEBUG
        		printf("FOREACH\n");
#endif

			// Search the file list for the current file,
			// if found, delete it from the list - this
			// will mark it as found.
			STAILQ_FOREACH(entry, &file_list, entries)
			{

#ifdef DEBUG
        			printf("File iteration: %s\n", entry->item);
#endif

				if (!strcmp(entry->item, h.name))
                        	{
                                        fprintf(stdout, "%s\n", h.name);
                                        fflush(stdout);
					to_be_removed = entry;
                                }
                        }
			
			// For some reason I couldn't remove the node
			// in a for cycle (which should be considered
			// SAFE), therefore I resorted to this option
                        //
			// I will look further into this during
			// the second phase.
			if (to_be_removed != nullptr)
                        {
                                STAILQ_REMOVE(&file_list, to_be_removed,
                                                        list_node, entries);
                                free(to_be_removed->item);
                                free(to_be_removed);
                        }

#ifdef DEBUG
        printf("END FOREACH\n");
#endif

		}

		int file_size = octal_to_decimal(atoi(h.size));
		int pos = ftell(fp);
		int file_size_with_padding = file_size;

		if (file_size % BLOCK_SIZE > 0)
		{
			file_size_with_padding = file_size + BLOCK_SIZE - 
				(file_size % BLOCK_SIZE);
		}

#ifdef DEBUG
        	printf("Current offset: %d\n", pos);
		printf("File size: %d\n", file_size);
		printf("Next seeking offset: %d\n", skip);
#endif

		// If the next seek is out of range, the file is truncated.
		if (total - pos < file_size_with_padding)
		{
			warnx("Unexpected EOF in archive");
			
			free_resources();
			errx(EX_TARFAILURE, 
				"Error is not recoverable: exiting now");
		}

		fseek(fp, file_size_with_padding, SEEK_CUR);
	}

	bool failure = false;

	literal_entry* entry = STAILQ_FIRST(&file_list);
	literal_entry* temp;
        
	// Traverse the remainder of the file list and print a warning
	// message. Delete the node while we are at it. Set a failure
	// flag to terminate later.
	while (entry != nullptr)
	{
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

	if ((!first_block) && (first_block + second_block > 0))
	{
		warnx("A lone zero block at %d", total / BLOCK_SIZE);
	}

	if (failure)
	{
		free_resources();
		errx(EX_TARFAILURE, 
		"Exiting with failure status due to previous errors");
	}

	return (0);
}

int
extract_files(FILE* fp)
{
	fseek(fp, 0, SEEK_END);
	long int total = ftell(fp);
	rewind(fp);

	header_object h;

	while (fread(&h, sizeof(header_object), 1, fp))
	{
		bool extract_file = false;

	//printf("File name: %s\n", h.name);

		if (!strlen(h.name))
		{
			break;
		}

	//printf("Magic: %s\n", h.magic);

		if (strcmp(h.magic, ustar_magic))
                {
                        free_resources();

			fprintf(stderr,
				"This does not look like a tar archive\n");
			fflush(stderr);

			errx(EX_TARFAILURE, 
			"Exiting with failure status due to previous errors");
                }

		if (STAILQ_EMPTY(&file_list))
		{
			extract_file = true;
		}
		else
		{
			literal_entry* entry;
			literal_entry* to_be_removed = nullptr;
			
			STAILQ_FOREACH(entry, &file_list, entries)
			{
				if (!strcmp(entry->item, h.name))
                        	{
					extract_file = true;
					to_be_removed = entry;
                                }
                        }

			if (to_be_removed != nullptr)
                        {
                                STAILQ_REMOVE(&file_list, to_be_removed,
                                                        list_node, entries);
                                free(to_be_removed->item);
                                free(to_be_removed);
                        }
		}

		long int file_size = strtol(h.size, nullptr, 8);
		long int pos = ftell(fp);
		long int file_size_with_padding = file_size;

	//printf("File size: %ld\n", file_size);
	//printf("Current pos: %ld\n", pos);

		if (file_size % BLOCK_SIZE > 0)
		{
			file_size_with_padding = file_size + BLOCK_SIZE - 
				(file_size % BLOCK_SIZE);
		}

	//printf("File size with padding: %ld\n", file_size_with_padding);

		if (extract_file == false)
                {
                        fseek(fp, file_size_with_padding, SEEK_CUR);
                        continue;
                }

	//printf("Extract file: true\n");

		if (config_.verbose)
                {
                        fprintf(stdout, "%s\n", h.name);
                        fflush(stdout);
                }


		if (total - pos < file_size_with_padding)
		{
			warnx("Unexpected EOF in archive");

			read_until_end(fp, h.name);

			free_resources();
			errx(EX_TARFAILURE, 
				"Error is not recoverable: exiting now");
		}

	//printf("Dumping file\n");

		FILE *fpout;
		if ((fpout = fopen(h.name, "w+")) == nullptr)
		{
			warnx("File could not be created");
			fseek(fp, file_size_with_padding, SEEK_CUR);
			continue;
		}

	//printf("Number of blocks: %d\n", file_size_with_padding / BLOCK_SIZE);

		char buffer;
	
		for (int i = 0; i < file_size; i++)
		{
			fread(&buffer, sizeof(buffer), 1, fp);
			fwrite(&buffer, sizeof(buffer), 1, fpout);
		}

		fseek(fp, file_size_with_padding - file_size, SEEK_CUR);
	
	//printf("Dumping done\n");
	//pos = ftell(fp);
	//printf("Pos after dumping: %ld\n", pos);

		fclose(fpout);
	}

	bool failure = false;

	literal_entry* entry = STAILQ_FIRST(&file_list);
	literal_entry* temp;
        
	while (entry != nullptr)
	{
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

	if ((!first_block) && (first_block + second_block > 0))
	{
		warnx("A lone zero block at %ld", total / BLOCK_SIZE);
	}

	if (failure)
	{
		free_resources();
		errx(EX_TARFAILURE, 
		"Exiting with failure status due to previous errors");
	}

	return (0);
}

/**
 * Entrypoint of the application after the input has been parsed.
 * Launches the function chosen by the user via input options.
 *
 * @return The return value of the launched function.
 */
int
launcher()
{

#ifdef DEBUG
        printf("Running the launcher\n");
#endif

	FILE* fp;

	if (config_.file == true)
	{
		if ((fp = fopen(config_.archive_name, "r")) == nullptr)
		{
			free_resources();
			errx(EX_TARFAILURE, "Archive could not be opened");
		}
	}
	else
	{
		// fp = TAPE...
		free_resources();
		errx(EX_TARFAILURE, "Not implemented");
	}

	int ret_val;

	if (config_.extract)
	{
		ret_val = extract_files(fp);
	}
	else
	{
		ret_val = list_files(fp);
	}
	
	fclose(fp);
	free_resources();

	return (ret_val);
}

int
main(int argc, char** argv)
{
	initialize_config();

	for (int i = 1; i < argc; i++)
	{

#ifdef DEBUG
        	printf("Parsing argument no. %d\n", i);
#endif

		if (!strcmp(i[argv], "-f"))
		{
			config_.file = true;
			
			i++;
			assert(i < argc);

			config_.archive_name = strdup(argv[i]);
                        assert(config_.archive_name != nullptr);

			continue;
		}

		if (!strcmp(argv[i], "-t"))
		{
			config_.list = true;
			continue;
		}

		if (!strcmp(argv[i], "-x"))
		{
			config_.extract = true;
			continue;
		}

		if (!strcmp(argv[i], "-v"))
                {
			config_.verbose = true;
			continue;
                }

		if (argv[i][0] == '-')
		{
			free_resources();
			errx(EX_TARFAILURE, "Unknown option: %s", argv[i]);
		}

		literal_entry* entry = malloc(sizeof(literal_entry));
		assert(entry != nullptr);

		entry->item = strdup(argv[i]);
		assert(entry->item != nullptr);
		
		if (STAILQ_EMPTY(&file_list))
		{
			 STAILQ_INSERT_HEAD(&file_list, entry, entries);
		}
		else
		{
			STAILQ_INSERT_TAIL(&file_list, entry, entries);
		}

		config_.list_is_empty = false;
	}

	if (validate_config())
	{

#ifdef DEBUG
        	printf("Archive name: %s\n", config_.archive_name);
#endif

		return (launcher());
	}

	free_resources();
	errx(EX_TARFAILURE, "Need at least one option");

	// unreachable

	return (0);
}
