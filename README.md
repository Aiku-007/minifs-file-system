# MiniFS – File I/O and Random Access

Student Name: Amarjot  Amarjot
Student ID: 110158964
Course: System Programming  
Assignment: MiniFS – File I/O and Random Access

## Project Description

MiniFS is a simple file system written in C. It stores files inside one Linux image file. The image contains a header, a directory that can store up to 16 files, and the contents of those files.

The program supports creating an image, adding files, listing stored files, and extracting files.

## Files Included

- minifs.c – Contains the main program and MiniFS functions.
- minifs.h – Contains the structures, constants, and function declarations.
- README.md – Contains compilation instructions and usage examples.

## Compilation

Open a terminal in the folder containing minifs.c and minifs.h. Run:

    gcc -Wall -Wextra -Wpedantic -std=c11 minifs.c -o minifs

If compilation succeeds, an executable named minifs will be created.

## How to Use the Program

1. Create an image:

       ./minifs create disk.img

   Creates a new MiniFS image and initializes its header and directory.

2. Add a file:

       ./minifs add disk.img notes.txt

   Stores the contents of notes.txt inside the image.

3. List stored files:

       ./minifs list disk.img

   Displays the stored filenames, sizes, and offsets.

4. Extract a file:

       ./minifs extract disk.img notes.txt recovered.txt

   Extracts notes.txt from the image and saves it as recovered.txt.

## Testing

The following commands demonstrate the main operations:

    ./minifs create disk.img
    echo "Hello MiniFS!" > notes.txt
    ./minifs add disk.img notes.txt
    ./minifs list disk.img
    ./minifs extract disk.img notes.txt recovered.txt
    cmp notes.txt recovered.txt

If cmp produces no output, the original and extracted files are identical.

Additional tests were performed for duplicate filenames, missing files, filenames that are too long, and a full directory.

## Error Handling and Implementation

The program checks system-call results and reports errors when operations fail. It uses helper functions to handle partial reads and writes and retries interrupted system calls.

The program uses the POSIX system calls open(), read(), write(), lseek(), and close() for file operations.

## Submission Files

- minifs.c
- minifs.h
- README.md