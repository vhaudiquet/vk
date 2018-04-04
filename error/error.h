/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

void _fatal_kernel_error(const char* error_message, const char* error_context, char* err_file, u32 err_line);
#define fatal_kernel_error(em, ec) _fatal_kernel_error(em, ec, __FILE__, __LINE__)

/* GLOBAL ERROR NUMBERS HANDLING */
typedef u32 error_t;

//global error numbers
#define ERROR_NONE 0 //no errors, everything went well
#define UNKNOWN_ERROR 1

//disk errors
#define ERROR_DISK_UNREACHABLE 2 //disk is unreachable (plugged out, ...)
#define ERROR_DISK_OUT 3 //tried to read/write out of disk bounds, disk has no space, ...
#define ERROR_DISK_BUSY 4 //disk is busy (try again later)
#define ERROR_DISK_INTERNAL 5 //internal disk driver error
//fs errors
#define ERROR_FILE_NOT_FOUND 10 //file not found
#define ERROR_FILE_SYSTEM_READ_ONLY 11 //file system is read only and a write op was intended
#define ERROR_FILE_IS_DIRECTORY 12 //a std file was excepted, but file was a directory
#define ERROR_FILE_IS_NOT_DIRECTORY 13 //a directory was excepted, but file was a std file
#define ERROR_FILE_NAME_ALREADY_EXISTS 14 //a file with this name already exists in the same place
#define ERROR_FILE_UNSUPPORTED_FILE_SYSTEM 15 //the filesystem of the file is not supported (at least for this action)
#define ERROR_FILE_FS_INTERNAL 16 //internal file system driver error
#define ERROR_FILE_OUT 17 //tried to read/write out of file/fs space
#define ERROR_FILE_CORRUPTED_FS 18 //the file system/file might be corrupted : pointer to void or like that
#define ERROR_DIRECTORY_NOT_EMPTY 19 //directory is not empty
#define ERROR_EOF 20
//process errors
#define ERROR_IS_NOT_ELF 21 //the file you tried to execute is not an elf file
#define ERROR_IS_64_BITS 22 //the elf file needs a 64 bits architecture
#define ERROR_IS_NOT_EXECUTABLE 23 //the elf file is not an executable (library, ...)
#define ERROR_WRONG_INSTRUCTION_SET 24 //the elf file is not using good instruction set
#define ERROR_INVALID_PID 25 //the pid was invalid (not assigned to a process)
#define ERROR_INVALID_SIGNAL 26 //the signal number was invalid (not assigned to any signal)
#define ERROR_IS_SESSION_LEADER 27 //the process is a session leader
#define ERROR_IS_ANOTHER_SESSION 28 //the group is in another session
//sync errors
#define ERROR_MUTEX_ALREADY_LOCKED 31 //trying to lock a mutex already locked
#define ERROR_MUTEX_OWNED_BY_OTHER 32 //trying to unlock a mutex that you don't own
//memory errors
#define ERROR_INVALID_PTR 36