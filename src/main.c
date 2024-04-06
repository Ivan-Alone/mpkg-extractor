#include <stdio.h>
#include <string.h> // String manip
#include <stdlib.h> // malloc, free
#include <time.h>	// clock(), clocks_per_sec
#include <errno.h>

const char* PACKAGE_VERSION = "PKGM0014"; // Modify this if you want to use this on other formats / versions although it might not work.

#define _MAX_PATH 250

// Directory creation
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

typedef unsigned char u8;

// Header file structure
#pragma pack(1) /* Remove padding because the actual size comes out to 28 bytes, but the compiler will pad it out to 32 (8*4) \
				 & sizeof will report wrong (technically correct) values */
struct header_file_t
{
	int filename_length;
	char* filename;
	int start_pos;
	int file_size;
	char* final_path; // This is an additional argument added by me for storing the dump path of the file
};

typedef struct header_file_t header_file;
extern int mkdir_p(const char* path);

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("syntax: extract.exe <.mpkg> [optional: format override]\n");
		return 1;
	}

	if (argc == 3)
		PACKAGE_VERSION = argv[2];

	const char* pkg_path = argv[1];

	char* dst_folder = NULL;
	header_file** files = NULL;

	float open_time = (float)clock() / CLOCKS_PER_SEC;
	FILE* pkg = fopen(pkg_path, "rb"); // Open a handle to our package

	// Get package file size
	fseek(pkg, 0L, SEEK_END);
	int file_size = ftell(pkg);
	fseek(pkg, 4L, SEEK_SET);

	// Get header for valid package check (this should be equal to PACKAGE_VERSION)
	char header_buf[9];
	fread(header_buf, sizeof(char), 0x8, pkg);
	header_buf[8] = '\0';

	int dst_cutoff = 0, current_header_file = 0, header_end = 0;

	// Verify package file header
	if (strcmp(header_buf, PACKAGE_VERSION) != NULL)
	{
		printf("package header invalid, %s != %s\n", header_buf, PACKAGE_VERSION);
		goto end;
	}

	printf("file header: %s\n", header_buf);
	printf("opened package %s (%d bytes)\n", pkg_path, file_size);

	// Prepare package destination folder
	dst_cutoff = strrchr(pkg_path, '.') - pkg_path;
	dst_folder = (char*)malloc((dst_cutoff + 1) * sizeof(char));
	strncpy(dst_folder, pkg_path, dst_cutoff);
	dst_folder[dst_cutoff] = '\0';

	printf("dumping to %s\n", dst_folder);

	// Package file looks valid, time to start parsing the header
	int header_file_count;
	fread(&header_file_count, sizeof(int), 1, pkg);
	printf("mpkg has %d component files, parsing...\n", header_file_count);

	files = (header_file**)malloc(sizeof(header_file) * (header_file_count + 1));

	// Loop through all header files and get info

	while (current_header_file != header_file_count)
	{
		// Allocate holder for current header file
		header_file* cur = (header_file*)malloc(sizeof(header_file));

		fread(&cur->filename_length, sizeof(int), 1, pkg);				   // First element of the header file structure is the filename length
		cur->filename = (char*)malloc((cur->filename_length + 1) * sizeof(char)); // Allocate memory for filename

		int filename_mem_size = cur->filename_length * sizeof(char);

		fread(cur->filename, filename_mem_size, 1, pkg); // Read filename
		cur->filename[filename_mem_size] = '\0';		 // Null-terminate filename

		int structure_size_offset = sizeof(header_file) - sizeof(void*) * 2 - sizeof(int); // Calculate how many bytes of our header we still need to read (total size - filename
		// - filename length - additional param not present in original spec)

		fread(&cur->start_pos, structure_size_offset, 1, pkg); // Read remaining part of header

		// Calculate final file path
		int path_size = strlen(cur->filename) + strlen(dst_folder) + 1;

		cur->final_path = (char*)malloc((path_size + 1) * sizeof(char));
		strcpy(cur->final_path, dst_folder);
		strcat(cur->final_path, "/");
		strcat(cur->final_path, cur->filename);
		cur->final_path[path_size] = '\0';

		printf("parsed header file %s\n", cur->filename);

		files[current_header_file] = cur;
		current_header_file++;
	}

	// This works because of the structure of the mpkg file format (header and then raw file bytes),
	// with the file start position being relative to the header end position.
	// Basically, by knowing exactly where the content of the last file ends, we can subtract
	// that position from the full file size and get the header end position.
	header_end = file_size - (files[header_file_count - 1]->start_pos + files[header_file_count - 1]->file_size);
	printf("calculated header end @ byte %d\n", header_end);

	// Create directory structure
	// This was previously done while dumping the files, but it caused race conditions, so I'll just do it AOT.
	for (int i = 0; i < header_file_count; i++)
	{
		header_file* cur = files[i];

		// Our file is nested in one/multiple directory(ies), we have to create the directory structure ourselves
		if (strchr(cur->final_path, '/') != NULL)
		{
			// Find tree size
			int folder_tree_size = strrchr(cur->final_path, '/') - cur->final_path + 1;
			char* folder_tree = (char*)malloc((folder_tree_size + 1) * sizeof(char)); // Allocate tree string buffer

			// Get directory tree
			strncpy(folder_tree, cur->final_path, folder_tree_size);
			folder_tree[folder_tree_size] = '\0';

			mkdir_p(folder_tree);

			// Free tree buffer
			free(folder_tree);
		}
	}

	// Dump package files
	for (int i = 0; i < header_file_count; i++)
	{
		header_file* cur = files[i];

		int file_offset = cur->start_pos + header_end;
		//printf("dumping %s @ %d with size %d\n", cur->filename, file_offset, cur->file_size);

		// Dump!
		fseek(pkg, file_offset, SEEK_SET);

		// Open file handle
		FILE* current_file = fopen(cur->final_path, "wb");
		if (current_file == NULL) {
			printf("failed to open file %s\n", cur->final_path);
			continue;
		}

		// Read content from mpkg
		u8* content_buffer = (u8*)malloc(sizeof(u8) * cur->file_size);
		fread(content_buffer, sizeof(u8), cur->file_size, pkg);
		fwrite(content_buffer, sizeof(u8), cur->file_size, current_file); // Write data to dump file
		fclose(current_file);											  // Close file, cleanup and onto the next

		// Free temp buffer memory
		free(content_buffer);

		printf("dumped %s to file (%d bytes)\n", cur->final_path, cur->file_size);

		// Free allocated structure memory
		free(cur->filename);
		free(cur->final_path);
		cur->final_path = NULL;
		cur->filename = NULL;

		free(cur);
	}

end:
	fclose(pkg);
	free(files);
	free(dst_folder);

	float return_time = (float)clock() / CLOCKS_PER_SEC;
	printf("program executed in %.2f seconds, from file open to memory free and return (%d bytes)\n", return_time - open_time, file_size);
	return 0;
}

// Recursive mkdir
// (https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950)
int mkdir_p(const char* path)
{
	/* Adapted from http://stackoverflow.com/a/2336245/119527 */
	const size_t len = strlen(path);
	char _path[_MAX_PATH];
	char* p;

	errno = 0;

	/* Copy string so its mutable */
	if (len > sizeof(_path) - 1)
	{
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(_path, path);

	/* Iterate the string */
	for (p = _path + 1; *p; p++)
	{
		if (*p == '/')
		{
			/* Temporarily truncate */
			*p = '\0';

#ifdef _WIN32
			if (_mkdir(_path) != 0)
#else
			if (mkdir(_path, S_IRWXU) != 0)
#endif
			{
				if (errno != EEXIST)
					return -1;
			}

			*p = '/';
		}
	}

#ifdef _WIN32
	if (_mkdir(_path) != 0)
#else
	if (mkdir(_path, S_IRWXU) != 0)
#endif
	{
		if (errno != EEXIST)
			return -1;
	}

	return 0;
}
